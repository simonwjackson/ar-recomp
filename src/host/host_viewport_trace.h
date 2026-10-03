#ifndef AR_HOST_VIEWPORT_TRACE_H
#define AR_HOST_VIEWPORT_TRACE_H

#include <stddef.h>
#include "present/present.h"
#include "present/render_comparison.h"

/* Capture-only evidence. The caller supplies the slot it actually drew,
 * PresentFrame's returned final viewport, and successful full-output readback
 * dimensions. No live settings, geometry solving, or FrameSlot capture here.
 * source/native describe the logical canvas/band, NOT a perspective footprint.
 * capture/capture_native describe the PPU display buffer excluding OBJ apron. */
bool HostDisplay_FormatViewportTrace(
    char *buffer, size_t capacity, const FrameSlot *slot,
    RenderComparisonView comparison, ArRenderRectI final_viewport,
    int readback_width, int readback_height);
void HostDisplay_TraceCompositeCapture(
    const FrameSlot *slot, RenderComparisonView comparison,
    ArRenderRectI final_viewport, int readback_width, int readback_height);

#endif
