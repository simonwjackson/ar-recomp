#include "present/display_geometry.h"

#include <stdint.h>

bool DisplayGeometry_ResolveAutoCanvas(
    int drawable_width, int drawable_height, bool crt_pixel_aspect,
    ActRaiserAutoCanvas *canvas) {
  if (!canvas || drawable_width <= 0 || drawable_height <= 0) return false;
  const int64_t par_x = crt_pixel_aspect ? 7 : 1;
  const int64_t par_y = crt_pixel_aspect ? 6 : 1;
  const int64_t width = drawable_width;
  const int64_t height = drawable_height;
  *canvas = (ActRaiserAutoCanvas){0};
  /* Nearest symmetric whole-pixel budget. Aspect fit absorbs rounding; never
   * stretch the native image, crop it, or exceed the capture/streaming caps. */
  if (width * 224 * par_y > height * 256 * par_x) {
    const int64_t denominator = 2 * height * par_x;
    int64_t extra = (width * 224 * par_y - height * 256 * par_x +
                     denominator / 2) / denominator;
    canvas->extra_columns = extra > kActRaiserWidescreenExtraMax
        ? kActRaiserWidescreenExtraMax : (int)extra;
  } else {
    const int64_t denominator = 2 * width * par_y;
    int64_t extra = (height * 256 * par_x - width * 224 * par_y +
                     denominator / 2) / denominator;
    canvas->extra_rows = extra > 64 ? 64 : (int)extra;
  }
  return true;
}

static ActRaiserDisplayGeometry s_geometry;

const ActRaiserDisplayGeometry *const g_actraiser_display_geometry =
    &s_geometry;

void DisplayGeometry_SetHorizontal(int render_extra, int display_extra) {
  s_geometry.render_extra = render_extra;
  s_geometry.display_extra = display_extra;
  s_geometry.widescreen_active =
      render_extra > 0 || s_geometry.auto_vertical_budget > 0;
}

void DisplayGeometry_SetAutoVerticalBudget(int budget) {
  s_geometry.auto_vertical_budget = budget;
}

void DisplayGeometry_SetVertical(int extra_top, int extra_bottom) {
  s_geometry.extra_top = extra_top;
  s_geometry.extra_bottom = extra_bottom;
}
