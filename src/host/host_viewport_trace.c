#include "host/host_viewport_trace.h"

#include <stdio.h>
#include "app/settings.h" /* Enum constants only. */
#include "app/session_fatal.h"
#include "present/presentation_view.h"

static int ClipToDisplay(int live, int budget) {
  return live < budget ? live : budget;
}

static const char *AspectName(int aspect) {
  switch (aspect) {
    case kScreenAspect_43: return "4:3";
    case kScreenAspect_169: return "16:9";
    case kScreenAspect_1610: return "16:10";
    case kScreenAspect_Stretch: return "Stretch";
    case kScreenAspect_Auto: return "Auto";
    default: return "unknown";
  }
}

bool HostDisplay_FormatViewportTrace(
    char *buffer, size_t capacity, const FrameSlot *slot,
    RenderComparisonView comparison, ArRenderRectI final_viewport,
    int readback_width, int readback_height) {
  if (!buffer || !capacity || !slot || readback_width <= 0 ||
      readback_height <= 0 || final_viewport.w <= 0 || final_viewport.h <= 0)
    return false;
  const PresentationViewDecision view = PresentationView_Resolve(slot, comparison);
  const char *scene = "Native";
  bool perspective = false;
  switch (view.scene) {
    case kPerformanceScene_ActionFlat: scene = "Action2D"; break;
    case kPerformanceScene_Action: scene = "Action3D"; perspective = true; break;
    case kPerformanceScene_Town: scene = "Town3D"; perspective = true; break;
    case kPerformanceScene_World: scene = "World3D"; perspective = true; break;
    case kPerformanceScene_Palace: scene = "Palace"; break;
    default: break;
  }
  if (!view.visible) scene = "Blank";
  const bool authentic = comparison == kRenderComparison_Authentic;
  const int width = authentic ? kFrameSlotAuthenticWidth : slot->visible_width;
  const int height = authentic ? kFrameSlotAuthenticHeight : FrameSlot_VisibleHeight(slot);
  const int left = authentic ? 0 : slot->ws_extra - slot->visible_x0;
  const int right = width - kFrameSlotAuthenticWidth - left;
  const int top = authentic ? 0 : slot->visible_top;
  const int bottom = height - kFrameSlotAuthenticHeight - top;
  if (left < 0 || right < 0 || top < 0 || bottom < 0) return false;
  const int written = snprintf(buffer, capacity,
      "[viewport-check] gf=%u aspect=%s par=%s scene=%s map=%02x/%02x "
      "drawable=%dx%d budget=%d/%d/%d/%d live=%d/%d/%d/%d "
      "source=0/0/%d/%d native=%d/%d/256/224 dest=%d/%d/%d/%d "
      "source_kind=logical-canvas mapping=%s "
      "capture=0/0/%d/%d capture_native=%d/%d/256/224 "
      "render_live=%u/%u/%d/%d comparison=%s visible=%d",
      (unsigned)slot->action_effects.game_frame, AspectName(slot->extended_aspect),
      authentic || slot->pixel_aspect == kPixelAspect_Crt43 ? "crt" : "square",
      scene, (unsigned)slot->diorama_map_group, (unsigned)slot->diorama_map_number,
      readback_width, readback_height, left, right, top, bottom,
      ClipToDisplay(slot->extra_left_cur, left),
      ClipToDisplay(slot->extra_right_cur, right),
      ClipToDisplay(slot->ws_extra_top, top),
      ClipToDisplay(slot->ws_extra_bottom, bottom),
      width, height, left, top,
      final_viewport.x, final_viewport.y, final_viewport.w, final_viewport.h,
      !view.visible ? "blank" : perspective ? "perspective" : "flat",
      slot->snes_width, FrameSlot_CaptureHeight(slot), slot->ws_extra, slot->ws_extra_top,
      (unsigned)slot->extra_left_cur, (unsigned)slot->extra_right_cur,
      slot->ws_extra_top, slot->ws_extra_bottom,
      authentic ? "authentic" : comparison == kRenderComparison_SideBySide
          ? "pip" : "enhanced", view.visible ? 1 : 0);
  return written >= 0 && (size_t)written < capacity;
}

void HostDisplay_TraceCompositeCapture(
    const FrameSlot *slot, RenderComparisonView comparison,
    ArRenderRectI final_viewport, int readback_width, int readback_height) {
  /* A failed compositor can unwind with a nonempty viewport. It must never
   * become successful capture-time evidence, even if readback itself worked. */
  if (SessionFatal_Requested()) return;
  char line[768];
  if (HostDisplay_FormatViewportTrace(line, sizeof(line), slot, comparison,
          final_viewport, readback_width, readback_height))
    fprintf(stderr, "%s\n", line);
}
