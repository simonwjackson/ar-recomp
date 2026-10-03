#include "host/host_viewport_trace.h"
#include "app/settings.h"
#include "actraiser_game.h"
#include "support/test_assert.h"
#include <stdio.h>
#include <string.h>

static FrameSlot frame;
static char line[768];

static void Format(RenderComparisonView comparison) {
  /* Deliberately not the desired aspect-fit rectangle. Evidence must retain
   * the caller's real returned rectangle, never solve a replacement for it. */
  assert(HostDisplay_FormatViewportTrace(line,sizeof(line),&frame,comparison,
      (ArRenderRectI){17,19,911,713},1240,1080));
  assert(strstr(line,"drawable=1240x1080"));
  assert(strstr(line,"dest=17/19/911/713"));
  assert(strstr(line,"gf=1400 aspect=Auto"));
  assert(strstr(line,"map=04/04"));
  assert(!strchr(line,'\n'));
}

int main(void) {
  frame=(FrameSlot){.snes_width=256,.snes_height=224,
      .extended_aspect=kScreenAspect_Auto,.pixel_aspect=kPixelAspect_Crt43,
      .visible_width=256,.visible_height=298,.visible_top=37,
      .ws_extra_top=3,.ws_extra_bottom=37,.inidisp=15,
      .diorama_map_group=kActRaiserMapGroup_Aitos,.diorama_map_number=4};
  frame.action_effects.game_frame=1400;
  Format(kRenderComparison_Enhanced);
  assert(strstr(line,"par=crt scene=Action2D"));
  assert(strstr(line,"budget=0/0/37/37 live=0/0/3/37"));
  assert(strstr(line,"source=0/0/256/298 native=0/37/256/224"));
  assert(strstr(line,"capture=0/0/256/264 capture_native=0/3/256/224"));
  assert(strstr(line,"source_kind=logical-canvas mapping=flat"));

  frame.diorama_active=true;
  frame.snes_width=496;
  frame.ws_extra=120;
  frame.visible_x0=120;
  frame.extra_left_cur=120;
  frame.extra_right_cur=80;
  Format(kRenderComparison_Enhanced);
  assert(strstr(line,"scene=Action3D"));
  assert(strstr(line,"source_kind=logical-canvas mapping=perspective"));
  assert(strstr(line,"budget=0/0/37/37 live=0/0/3/37"));
  assert(strstr(line,"capture=0/0/496/264 capture_native=120/3/256/224"));
  assert(strstr(line,"render_live=120/80/3/37"));

  frame.visible_width=398;
  frame.visible_height=224;
  frame.visible_x0=49;
  frame.visible_top=0;
  frame.extra_left_cur=15;
  frame.pixel_aspect=kPixelAspect_Square;
  Format(kRenderComparison_SideBySide);
  assert(strstr(line,"par=square scene=Action3D"));
  assert(strstr(line,"budget=71/71/0/0 live=15/71/0/0"));
  assert(strstr(line,"source=0/0/398/224 native=71/0/256/224"));
  assert(strstr(line,"comparison=pip visible=1"));

  Format(kRenderComparison_Authentic);
  assert(strstr(line,"par=crt scene=Native"));
  assert(strstr(line,"budget=0/0/0/0 live=0/0/0/0"));
  assert(strstr(line,"source=0/0/256/224 native=0/0/256/224"));
  assert(strstr(line,"comparison=authentic"));
  frame.inidisp=0x80;
  Format(kRenderComparison_Enhanced);
  assert(strstr(line,"scene=Blank"));
  assert(strstr(line,"mapping=blank"));
  assert(strstr(line,"visible=0"));

  assert(!HostDisplay_FormatViewportTrace(line,8,&frame,
      kRenderComparison_Enhanced,(ArRenderRectI){0,0,20,20},20,20));
  assert(!HostDisplay_FormatViewportTrace(line,sizeof(line),&frame,
      kRenderComparison_Enhanced,(ArRenderRectI){0},20,20));
  assert(!HostDisplay_FormatViewportTrace(line,sizeof(line),&frame,
      kRenderComparison_Enhanced,(ArRenderRectI){0,0,20,20},0,20));
  puts("host_viewport_trace_test: PASS");
  return 0;
}
