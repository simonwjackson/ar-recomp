/* Exercise the actual PPM capture function and callback boundary. Only its
 * producer/presenter/readback dependencies are stubs; no GPU claim is made. */
#include "dev/dev_tools.h"
#include "dev/host_dev_tools.h"
#include "render/present_hud.h"
#include "host/host_display.h"
#include "present/presentation_frame_generation.h"
#include "support/test_assert.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int captured, uploaded, presented, readbacks, released, traced;
static int readback_mode;
static int viewport_traced, hud_built, hud_count;
static bool fatal, tilted_hud, blank, fatal_present;
static bool empty_viewport;
static bool projection_trace;
static RenderComparisonView comparison;
static const FrameSlot *drawn_slot;
static const ArRenderRectI actual_viewport={1,0,2,2};
static uint8_t rgb[24];

void FrameSlot_Capture(FrameSlot *slot, const SimFrameData *sim) {
  assert(!sim);
  ++captured;
  *slot=(FrameSlot){.snes_width=256,.snes_height=224,
      .visible_width=256,.visible_height=298,.visible_top=37,
      .inidisp=blank ? 0x80 : 15,.diorama_active=true,.diorama_hud_flat=!tilted_hud,
      .hud_split_height=40};
  slot->action_effects.game_frame=1400;
}
void PresentUpload(const FrameSlot *slot) {
  ++uploaded;
  drawn_slot=slot;
  projection_trace=slot->trace_viewport_projection;
}
ArRenderRectI PresentFrame(const FrameSlot *slot, float alpha, double fps) {
  assert(slot==drawn_slot && alpha==kPresentationFrameGenerationPhaseNone && fps==60);
  ++presented;
  if (fatal_present) fatal=true; /* Renderer latches fatal, still returns a rect. */
  return empty_viewport ? (ArRenderRectI){0} : actual_viewport;
}
double HostDisplay_FramesPerSecond(void) { return 60; }
RenderComparisonView RenderComparison_PresentView(void) { return comparison; }
int Settings_VisibleWidth(void) { return 2; }
int Settings_VisibleX0(void) { return 0; }

static void Release(void *owner) {
  assert(owner==rgb);
  ++released;
}
static bool Readback(void *context, DevToolsRgb24Capture *out) {
  assert(context==rgb && presented==1);
  ++readbacks;
  if (readback_mode==1) return false;
  *out=(DevToolsRgb24Capture){
    .pixels=rgb,.width=readback_mode==2 ? 0 : 4,.height=2,.pitch_bytes=12,
    .owner=rgb,.release=Release,
  };
  return true;
}
bool SessionFatal_Requested(void) { return fatal; }
void HostDisplay_TraceCompositeCapture(const FrameSlot *slot, RenderComparisonView view,
    ArRenderRectI viewport, int width, int height) {
  assert(slot==drawn_slot && view==comparison && width==4 && height==2);
  assert(viewport.x==1 && viewport.y==0 && viewport.w==2 && viewport.h==2);
  ++viewport_traced;
}
int PresentHud_BuildChunks(const FrameSlot *slot, ArRenderRectI viewport,
    HudPresentationChunk chunks[kHudPresentationChunkCapacity]) {
  assert(slot==drawn_slot && slot->hud_split_height==40);
  assert(viewport.x==1 && viewport.y==0 && viewport.w==2 && viewport.h==2);
  ++hud_built;
  chunks[0]=(HudPresentationChunk){.output_destination={-5,77,32,40}};
  chunks[1]=(HudPresentationChunk){.output_destination={83,19,16,21}};
  return hud_count;
}
static void Trace(const FrameSlot *slot, RenderComparisonView view,
                  ArRenderRectI viewport, int width, int height) {
  assert(slot==drawn_slot && captured==1 && uploaded==1 && presented==1);
  assert(readbacks==1 && released==1); /* Successful readback, not a resize. */
  assert(slot->action_effects.game_frame==1400 && slot->visible_height==298);
  assert(slot->trace_viewport_projection && projection_trace);
  assert(view==comparison && width==4 && height==2);
  assert(viewport.x==1 && viewport.y==0 && viewport.w==2 && viewport.h==2);
  HostDevTools_TraceCompositeCapture(slot,view,viewport,width,height);
  assert(viewport_traced==1);
  assert(hud_built==(view==kRenderComparison_Authentic || tilted_hud || blank ? 0 : 1));
  ++traced;
}
static void Reset(void) {
  captured=uploaded=presented=readbacks=released=traced=0;
  readback_mode=0;
  empty_viewport=false;
  projection_trace=false;
  comparison=kRenderComparison_Enhanced;
  viewport_traced=hud_built=0;
  hud_count=2;
  fatal=tilted_hud=blank=fatal_present=false;
}

static int Occurrences(const char *text, const char *needle) {
  int count=0;
  for (const char *p=text; (p=strstr(p,needle)); p+=strlen(needle)) ++count;
  return count;
}

int main(void) {
  FILE *evidence_log=tmpfile();
  assert(evidence_log);
  const int saved_stderr=dup(STDERR_FILENO);
  assert(saved_stderr>=0 && dup2(fileno(evidence_log),STDERR_FILENO)>=0);
  static uint8_t native[16];
  DevToolsContext context={
    .readback={.capture_rgb24=Readback,.context=rgb},
    .trace_composite_capture=Trace,.hud_bg_texture={1},
    .framebuffer_pixels=native,.framebuffer_pitch=8,.snes_height=2,
  };
  memset(rgb,0x73,sizeof(rgb));
  for (int mode=0;mode<7;++mode) {
    Reset();
    if (mode==4) hud_count=0;
    tilted_hud=mode==5;
    blank=mode==6;
    readback_mode=mode<3 ? mode : 0;
    comparison=mode==3 ? kRenderComparison_Authentic : kRenderComparison_Enhanced;
    FILE *file=tmpfile();
    assert(file);
    const DevToolsCaptureResult result=DevTools_WriteFramebufferPpm(file,&context,true);
    assert(captured==1 && uploaded==1 && presented==1 && projection_trace);
    if (readback_mode==0) {
      assert(result.kind==kDevToolsCapture_Composite && traced==1);
      rewind(file);
      char ppm[64]={0};
      const size_t bytes=fread(ppm,1,sizeof(ppm),file);
      assert(bytes==35 && !memcmp(ppm,"P6\n4 2\n255\n",11));
      assert(!memcmp(ppm+11,rgb,24));
    } else assert(!result.width && !traced);
    assert(fclose(file)==0);
  }
  Reset();
  FILE *file=fopen("/dev/full","wb");
  assert(file);
  DevToolsCaptureResult result=DevTools_WriteFramebufferPpm(file,&context,true);
  assert(!result.width && !traced); /* Buffered I/O failure must not log evidence. */
  fclose(file);

  Reset();
  empty_viewport=true;
  file=tmpfile();
  assert(file);
  result=DevTools_WriteFramebufferPpm(file,&context,true);
  assert(!result.width && !readbacks && !traced && captured==1);
  assert(fclose(file)==0);

  Reset();
  context.hud_bg_texture=ArRenderTexture_Invalid();
  file=tmpfile();
  assert(file);
  result=DevTools_WriteFramebufferPpm(file,&context,false);
  assert(result.kind==kDevToolsCapture_NativeFramebuffer);
  assert(!captured && !presented && !readbacks && !traced);
  assert(fclose(file)==0);

  Reset();
  context.hud_bg_texture=(ArRenderTexture){1};
  context.trace_composite_capture=NULL;
  file=tmpfile();
  assert(file);
  result=DevTools_WriteFramebufferPpm(file,&context,true);
  assert(result.kind==kDevToolsCapture_Composite && captured==1 && !traced);
  assert(!projection_trace && !viewport_traced && !hud_built);
  assert(fclose(file)==0);
  /* Fatal render with a nonempty viewport: failed, no readback, no native
   * fallback (even when allowed), no evidence, and no PPM bytes written. */
  for (int allow_native=0; allow_native<2; ++allow_native) {
    Reset();
    context.trace_composite_capture=Trace;
    fatal_present=true;
    file=tmpfile();
    assert(file);
    result=DevTools_WriteFramebufferPpm(file,&context,!allow_native);
    assert(result.kind==kDevToolsCapture_Failed && !result.width && !result.height);
    assert(captured==1 && uploaded==1 && presented==1 && fatal);
    assert(!readbacks && !released && !traced && !viewport_traced && !hud_built);
    assert(fflush(file)==0 && ftell(file)==0);
    assert(fclose(file)==0);
  }
  Reset();
  fatal=true;
  const FrameSlot failed={.action_effects={.game_frame=9999}};
  HostDevTools_TraceCompositeCapture(&failed,kRenderComparison_Enhanced,
      actual_viewport,4,2);
  assert(!viewport_traced && !hud_built);
  assert(fflush(stderr)==0 && dup2(saved_stderr,STDERR_FILENO)>=0);
  close(saved_stderr);
  rewind(evidence_log);
  char evidence[4096]={0};
  assert(fread(evidence,1,sizeof(evidence)-1,evidence_log)>0);
  assert(Occurrences(evidence,"[viewport-hud] gf=1400 count=")==5);
  assert(Occurrences(evidence,"[viewport-hud] gf=1400 count=0 ")==4);
  assert(strstr(evidence,"[viewport-hud] gf=1400 count=2 coords=xywh "
      "precision=chunk-layout-pre-crt viewport=1/0/2/2\n"));
  assert(strstr(evidence,"[viewport-hud] gf=1400 index=0 rect=-5/77/32/40\n"));
  assert(strstr(evidence,"[viewport-hud] gf=1400 index=1 rect=83/19/16/21\n"));
  assert(Occurrences(evidence,"[viewport-hud] gf=1400 index=")==2);
  assert(!strstr(evidence,"gf=9999"));
  assert(fclose(evidence_log)==0);
  puts("dev_tools_capture_trace_test: PASS");
  return 0;
}
