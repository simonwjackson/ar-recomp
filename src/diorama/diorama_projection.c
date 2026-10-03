#include "diorama.h"

#include <math.h>

#include "diorama_depth_shapes.h"
#include "render/scene3d_math.h"

/* A projected triangle reaches its Y extrema at its vertices while entirely
 * in front of the camera. Across each mesh row only the two side vertices are
 * needed. At an authentic-band boundary between rows, interpolate the rendered
 * mesh's depth rather than resampling its underlying curve. */
static bool CameraVerticalBounds(
    const float matrix[16], float aspect_x, float height_scale,
    float z_world, float rake, float bow,
    float t0, float t1, float *top, float *bottom) {
  const int subdiv_y = kDioramaPlaneSubdivY;
  *top = INFINITY;
  *bottom = -INFINITY;
  for (int row = -1; row <= subdiv_y + 1; row++) {
    const float t = row < 0 ? t0 : row > subdiv_y ? t1
        : (float)row / (float)subdiv_y;
    if (t < t0 || t > t1) continue;
    float z;
    if (row >= 0 && row <= subdiv_y) {
      z = DioramaTiltedRowDepth(z_world, rake, bow, t);
    } else {
      const int lower = (int)floorf(t * (float)subdiv_y);
      const int upper = lower < subdiv_y ? lower + 1 : lower;
      const float a = DioramaTiltedRowDepth(
          z_world, rake, bow, (float)lower / (float)subdiv_y);
      const float b = DioramaTiltedRowDepth(
          z_world, rake, bow, (float)upper / (float)subdiv_y);
      z = a + (b - a) * (t * (float)subdiv_y - (float)lower);
    }
    for (int side = 0; side < 2; side++) {
      Scene3DPoint p;
      /* A unit viewport gives normalized screen coordinates, independent of
       * resolution, pixel aspect, or the viewport's output origin. */
      if (!Scene3D_ProjectWorldPoint(
              matrix, ((float)side - 0.5f) * aspect_x,
              (0.5f - t) * height_scale, z, 1, 1, &p))
        return false;
      *top = fminf(*top, p.y);
      *bottom = fmaxf(*bottom, p.y);
    }
  }
  return true;
}

bool Diorama_CenterCameraVertically(
    float matrix[16], float aspect_x, float height_scale,
    float z_world, float rake, float bow,
    float authentic_t0, float authentic_t1) {
  if (!matrix || !isfinite(aspect_x) || aspect_x <= 0.0f ||
      !isfinite(height_scale) || height_scale <= 0.0f ||
      !isfinite(z_world) || !isfinite(rake) || !isfinite(bow) ||
      !isfinite(authentic_t0) || !isfinite(authentic_t1) ||
      authentic_t0 < 0.0f || authentic_t1 > 1.0f ||
      authentic_t0 >= authentic_t1)
    return false;
  for (int i = 0; i < 16; i++)
    if (!isfinite(matrix[i])) return false;

  float top, bottom;
  if (!CameraVerticalBounds(matrix, aspect_x, height_scale,
                           z_world, rake, bow,
                           0.0f, 1.0f, &top, &bottom))
    return false;
  float shift = 0.5f - 0.5f * (top + bottom);
  if (bottom - top > 1.0f) {
    float native_top, native_bottom;
    if (!CameraVerticalBounds(matrix, aspect_x, height_scale,
                             z_world, rake, bow,
                             authentic_t0, authentic_t1,
                             &native_top, &native_bottom))
      return false;
    if (native_bottom - native_top <= 1.0f)
      shift = fmaxf(-native_top, fminf(shift, 1.0f - native_bottom));
    else
      shift = 0.5f - 0.5f * (native_top + native_bottom);
  }

  /* Clip Y += offset * clip W is a uniform screen translation after the
   * perspective divide. Apply it to the shared matrix so every depth plane,
   * skirt, aperture and attached effect receives exactly the same shift. */
  const float clip_shift = -2.0f * shift;
  for (int c = 0; c < 4; c++)
    matrix[c * 4 + 1] += clip_shift * matrix[c * 4 + 3];
  return true;
}

bool Diorama_PlaneEligible(int plane, bool visible, bool has_texture,
                           bool has_pixels, bool hud_flat, bool skybox_only) {
  if (!visible || !has_texture || !has_pixels) return false;
  if (plane == SR_PPU_OVERLAY_BG3 && hud_flat) return false;
  /* Skybox-only replaces the distant backdrop, not BG2's foreground water
   * and other high-priority scenery. Those retain their native depth, alpha
   * and attached effects (including when their low-priority band is empty). */
  if (skybox_only &&
      (plane == SR_PPU_OVERLAY_BG2 ||
       plane == kDioramaPlane_Bg2Far ||
       plane == kDioramaPlane_Backdrop))
    return false;
  return true;
}

bool Diorama_PlaneProjectable(int plane, bool visible, bool has_texture,
                              bool has_pixels, bool has_attached_effect,
                              bool hud_flat, bool skybox_only) {
  const bool accepts_attached_effect =
      DioramaPlaneIsObjectPriority(plane) ||
      plane == SR_PPU_OVERLAY_BG1 ||
      plane == SR_PPU_OVERLAY_BG2 ||
      plane == kDioramaPlane_Bg1Hi || plane == kDioramaPlane_Bg2Hi;
  const bool has_effect_content =
      has_attached_effect && accepts_attached_effect;
  return Diorama_PlaneEligible(
      plane, visible, has_texture || has_effect_content,
      has_pixels || has_effect_content,
      hud_flat, skybox_only);
}

uint8_t Diorama_FilterObjEffectProjectionMask(
    uint8_t required_priorities, uint32_t requested_planes,
    uint32_t content_planes, uint32_t uploaded_planes) {
  uint8_t filtered = 0;
  for (unsigned priority = 0;
       priority < kDioramaObjectPriorityCount; priority++) {
    const uint8_t priority_bit = (uint8_t)(1u << priority);
    if (!(required_priorities & priority_bit)) continue;
    const int plane = DioramaPlaneForObjectPriority(priority);
    if (plane < 0) continue;
    const uint32_t plane_bit = 1u << (unsigned)plane;
    if (!(requested_planes & plane_bit)) continue;
    if ((content_planes & plane_bit) && !(uploaded_planes & plane_bit))
      continue;
    filtered |= priority_bit;
  }
  return filtered;
}

uint32_t Diorama_FilterBgEffectProjectionMask(
    uint32_t required_planes, uint32_t requested_planes,
    uint32_t content_planes, uint32_t uploaded_planes) {
  const uint32_t valid_planes =
      (1u << SR_PPU_OVERLAY_BG1) |
      (1u << SR_PPU_OVERLAY_BG2) |
      (1u << kDioramaPlane_Bg1Hi) | (1u << kDioramaPlane_Bg2Hi);
  const uint32_t failed_content = content_planes & ~uploaded_planes;
  return required_planes & valid_planes & requested_planes & ~failed_content;
}

static bool ProjectCapturedPlanePoint(
    const DioramaProjection *projection, float capture_x, float capture_y,
    const DioramaPlaneProjection *plane, ArRenderPointF *point,
    float *scale_x, float *scale_y) {
  /* A published valid projection already guarantees non-zero texture/output
   * dimensions; public entry points own pointer validation once per call. */
  if (!projection->valid || !plane->valid)
    return false;
  float du = plane->u1 - plane->u0;
  float dv = plane->v1 - plane->v0;
  if (du == 0.0f || dv == 0.0f) return false;

  ArRenderPointF projected[3];
  int sample_count = (scale_x || scale_y) ? 3 : 1;
  for (int sample = 0; sample < sample_count; sample++) {
    float x = capture_x + plane->capture_offset.x + (sample == 1 ? 1.0f : 0.0f);
    float y = capture_y + plane->capture_offset.y + (sample == 2 ? 1.0f : 0.0f);
    float u = (x + (float)projection->texture_x_origin) /
        (float)projection->texture_width;
    float v = y / (float)projection->texture_height;
    float s = (u - plane->u0) / du;
    float t = (v - plane->v0) / dv;
    float wx = (s - 0.5f) * projection->aspect_x;
    float wy = (0.5f - t) * projection->height_scale;
    float wz = DioramaTiltedRowDepth(
        plane->z_world, plane->rake, plane->bow, t);
    if (plane->overflow_valid && t > plane->overflow_fold_t &&
        plane->overflow_height > 0.0f) {
      const float overflow_t =
          (t - plane->overflow_fold_t) * projection->height_scale /
          plane->overflow_height;
      const float y_top =
          (0.5f - plane->overflow_fold_t) * projection->height_scale;
      const float z_top = DioramaTiltedRowDepth(
          plane->z_world, plane->rake, plane->bow,
          plane->overflow_fold_t);
      DioramaOverflowFoldPoint(
          overflow_t, y_top, z_top, plane->overflow_handoff_z,
          plane->overflow_height, plane->overflow_overlap_t,
          plane->overflow_front_z, plane->overflow_front_drop,
          &wy, &wz);
    }
    Scene3DPoint projected_point;
    if (!Scene3D_ProjectWorldPoint(
            projection->matrix, wx, wy, wz,
            projection->output_width, projection->output_height,
            &projected_point))
      return false;
    projected[sample] = (ArRenderPointF){
      (float)projection->output_x + projected_point.x,
      (float)projection->output_y + projected_point.y,
    };
  }
  *point = projected[0];
  if (scale_x)
    *scale_x = hypotf(projected[1].x - projected[0].x,
                      projected[1].y - projected[0].y);
  if (scale_y)
    *scale_y = hypotf(projected[2].x - projected[0].x,
                      projected[2].y - projected[0].y);
  return true;
}

bool Diorama_ProjectCapturedPoint(const DioramaProjection *projection,
                                  float capture_x, float capture_y,
                                  unsigned obj_priority, ArRenderPointF *point,
                                  float *scale_x, float *scale_y) {
  if (!projection || !point ||
      obj_priority >= kDioramaObjectPriorityCount) return false;
  return ProjectCapturedPlanePoint(
      projection, capture_x, capture_y,
      &projection->object_planes[obj_priority], point, scale_x, scale_y);
}

bool Diorama_ProjectCapturedBg1Point(const DioramaProjection *projection,
                                     float capture_x, float capture_y,
                                     ArRenderPointF *point,
                                     float *scale_x, float *scale_y) {
  if (!projection || !point) return false;
  return ProjectCapturedPlanePoint(
      projection, capture_x, capture_y,
      &projection->bg1_plane, point, scale_x, scale_y);
}

bool Diorama_ProjectCapturedBg1HighPoint(
    const DioramaProjection *projection,
    float capture_x, float capture_y, ArRenderPointF *point,
    float *scale_x, float *scale_y) {
  if (!projection || !point) return false;
  return ProjectCapturedPlanePoint(
      projection, capture_x, capture_y,
      &projection->bg1_high_plane, point, scale_x, scale_y);
}

bool Diorama_ProjectCapturedBg2HighPoint(
    const DioramaProjection *projection,
    float capture_x, float capture_y, ArRenderPointF *point,
    float *scale_x, float *scale_y) {
  if (!projection || !point) return false;
  return ProjectCapturedPlanePoint(
      projection, capture_x, capture_y,
      &projection->bg2_high_plane, point, scale_x, scale_y);
}

static bool ValidSkyboxBand(const DioramaSkyboxBandProjection *band) {
  return isfinite(band->x0) && isfinite(band->x1) &&
      isfinite(band->y0) && isfinite(band->y1) &&
      isfinite(band->output_y0) && isfinite(band->output_y1) &&
      band->x1 > band->x0 && band->y1 > band->y0 &&
      band->output_y0 >= 0 && band->output_y1 <= 1 &&
      band->output_y1 > band->output_y0;
}

bool Diorama_SkyboxCaptureBounds(const DioramaProjection *projection,
                                 float *x0, float *y0, float *x1, float *y1) {
  if (!projection || !projection->valid || !x0 || !y0 || !x1 || !y1)
    return false;
  const DioramaSkyboxProjection *sky = &projection->bg2_skybox;
  if (!sky->count || sky->count > kDioramaBgMaxValidSpans ||
      sky->active_band < -1 || sky->active_band >= (int)sky->count)
    return false;
  const unsigned first = sky->active_band < 0 ? 0 : (unsigned)sky->active_band;
  const unsigned end = sky->active_band < 0 ? sky->count : first + 1;
  float left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
  for (unsigned i = first; i < end; i++) {
    const DioramaSkyboxBandProjection *band = &sky->bands[i];
    if (!ValidSkyboxBand(band)) return false;
    left = fminf(left, band->x0);
    right = fmaxf(right, band->x1);
    top = fminf(top, band->y0);
    bottom = fmaxf(bottom, band->y1);
  }
  *x0 = left; *y0 = top; *x1 = right; *y1 = bottom;
  return true;
}

static bool ProjectSkyboxPoint(const DioramaProjection *projection,
    float x, float y, ArRenderPointF *point, float *scale_x, float *scale_y) {
  const DioramaSkyboxProjection *sky = &projection->bg2_skybox;
  if (!projection->valid || !sky->count || sky->count > kDioramaBgMaxValidSpans ||
      sky->active_band < -1 || sky->active_band >= (int)sky->count ||
      projection->output_width <= 0 || projection->output_height <= 0 ||
      !isfinite(x) || !isfinite(y)) return false;
  unsigned selected = sky->active_band < 0 ? 0 : (unsigned)sky->active_band;
  if (sky->active_band < 0) {
    /* Direction probes may extrapolate beyond the visible rectangle. Extend
     * the nearest band; geometry itself is clipped before projection. */
    float distance = INFINITY;
    for (unsigned i = 0; i < sky->count; i++) {
      const DioramaSkyboxBandProjection *band = &sky->bands[i];
      if (!ValidSkyboxBand(band)) return false;
      const float d = fmaxf(0, fmaxf(band->y0 - y, y - band->y1));
      if (d < distance) {
        distance = d;
        selected = i;
      }
    }
  }
  const DioramaSkyboxBandProjection *band = &sky->bands[selected];
  if (!ValidSkyboxBand(band)) return false;
  const float sx = projection->output_width / (band->x1 - band->x0);
  const float sy = projection->output_height *
      (band->output_y1 - band->output_y0) / (band->y1 - band->y0);
  *point = (ArRenderPointF){
      projection->output_x + (x - band->x0) * sx,
      projection->output_y + band->output_y0 * projection->output_height +
          (y - band->y0) * sy};
  if (scale_x) *scale_x = sx;
  if (scale_y) *scale_y = sy;
  return true;
}

bool Diorama_ProjectCapturedBg2Point(const DioramaProjection *projection,
                                     float capture_x, float capture_y,
                                     ArRenderPointF *point,
                                     float *scale_x, float *scale_y) {
  if (!projection || !point) return false;
  if (projection->bg2_skybox.count)
    return ProjectSkyboxPoint(projection, capture_x, capture_y,
                               point, scale_x, scale_y);
  return ProjectCapturedPlanePoint(
      projection, capture_x, capture_y,
      &projection->bg2_plane, point, scale_x, scale_y);
}

/* Clip in texture space and interpolate the submitted 2D positions. The GPU
 * consumes these same affine triangles, not a continuously resampled bow. */
typedef struct EvidenceVertex { float u, v, x, y; } EvidenceVertex;

static void EvidenceInclude(DioramaEvidenceBounds *bounds, float x, float y) {
  if (!bounds->valid) {
    *bounds = (DioramaEvidenceBounds){true, x, y, x, y};
    return;
  }
  bounds->x0 = fminf(bounds->x0, x);
  bounds->x1 = fmaxf(bounds->x1, x);
  bounds->y0 = fminf(bounds->y0, y);
  bounds->y1 = fmaxf(bounds->y1, y);
}

static void EvidenceClipTriangle(
    const EvidenceVertex triangle[3], float u0, float v0, float u1, float v1,
    int output_x, int output_y, DioramaEvidenceBounds *bounds) {
  EvidenceVertex a[12], b[12];
  for (int i = 0; i < 3; i++) a[i] = triangle[i];
  int count = 3;
  const float edges[4] = {u0, v0, u1, v1};
  for (int edge = 0; edge < 4 && count; edge++) {
    int next_count = 0;
    EvidenceVertex previous = a[count - 1];
    float previous_d = (edge % 2 ? previous.v : previous.u) - edges[edge];
    if (edge >= 2) previous_d = -previous_d;
    for (int i = 0; i < count; i++) {
      const EvidenceVertex current = a[i];
      float d = (edge % 2 ? current.v : current.u) - edges[edge];
      if (edge >= 2) d = -d;
      if ((d >= 0) != (previous_d >= 0)) {
        const float t = previous_d / (previous_d - d);
        b[next_count++] = (EvidenceVertex){
          previous.u + t * (current.u - previous.u),
          previous.v + t * (current.v - previous.v),
          previous.x + t * (current.x - previous.x),
          previous.y + t * (current.y - previous.y)};
      }
      if (d >= 0) b[next_count++] = current;
      previous = current;
      previous_d = d;
    }
    count = next_count;
    for (int i = 0; i < count; i++) a[i] = b[i];
  }
  if (count < 3) return;
  for (int i = 0; i < count; i++)
    EvidenceInclude(bounds, output_x + a[i].x, output_y + a[i].y);
}

void Diorama_CaptureMeshEvidence(
    DioramaCaptureEvidence *evidence, const DioramaProjection *projection,
    int plane, const ArRenderVertex2D *vertices, int vertex_count,
    const int32_t *indices, int index_count) {
  if (!evidence || !projection || !projection->valid) return;
  if (projection->texture_width <= 0 || projection->texture_height <= 0 ||
      vertex_count < 0 || index_count < 0 || index_count % 3 ||
      (index_count && (!vertices || !indices))) {
    evidence->failed = true;
    return;
  }
  const DioramaPlaneProjection *shape;
  int p;
  switch (plane) {
    case SR_PPU_OVERLAY_BG1: p = 0; shape = &projection->bg1_plane; break;
    case kDioramaPlane_Bg1Hi: p = 1; shape = &projection->bg1_high_plane; break;
    case SR_PPU_OVERLAY_BG2: p = 2; shape = &projection->bg2_plane; break;
    case kDioramaPlane_Bg2Hi: p = 3; shape = &projection->bg2_high_plane; break;
    default: return;
  }
  if (!shape->valid || !index_count) return;
  evidence->mesh[p] = true;
  for (int i = 0; i + 2 < index_count; i += 3) {
    EvidenceVertex triangle[3];
    for (int j = 0; j < 3; j++) {
      const int index = indices[i + j];
      if (index < 0 || index >= vertex_count) { evidence->failed = true; return; }
      const ArRenderVertex2D *v = &vertices[index];
      triangle[j] = (EvidenceVertex){v->tex_coord.x, v->tex_coord.y,
                                     v->position.x, v->position.y};
      if (!isfinite(triangle[j].u) || !isfinite(triangle[j].v) ||
          !isfinite(triangle[j].x) || !isfinite(triangle[j].y)) {
        evidence->failed = true;
        return;
      }
    }
    for (int band = 0; band < kDioramaEvidenceBands; band++) {
      const DioramaEvidenceBounds source = evidence->source[band];
      if (!source.valid) continue;
      const DioramaBgValidSpanPlan *spans = band ? evidence->spans[p] : NULL;
      if (spans && spans->count > kDioramaBgMaxValidSpans) {
        evidence->failed = true;
        return;
      }
      const unsigned count = spans ? spans->count : 1;
      for (unsigned span = 0; span < count; span++) {
        float x0 = source.x0, y0 = source.y0, x1 = source.x1, y1 = source.y1;
        if (spans) {
          const DioramaBgValidSpan *s = &spans->spans[span];
          x0 = fmaxf(x0, s->x0 - projection->texture_x_origin);
          x1 = fminf(x1, s->x1 - projection->texture_x_origin);
          y0 = fmaxf(y0, s->y0);
          y1 = fminf(y1, s->y1);
        }
        if (x0 >= x1 || y0 >= y1) continue;
        EvidenceClipTriangle(triangle,
            (x0 + projection->texture_x_origin + shape->capture_offset.x) /
                projection->texture_width,
            (y0 + shape->capture_offset.y) / projection->texture_height,
            (x1 + projection->texture_x_origin + shape->capture_offset.x) /
                projection->texture_width,
            (y1 + shape->capture_offset.y) / projection->texture_height,
            projection->output_x, projection->output_y, &evidence->output[p][band]);
      }
    }
  }
}

void Diorama_CaptureSkyboxEvidence(
    DioramaCaptureEvidence *evidence, const DioramaProjection *projection) {
  if (!evidence || !projection || !projection->valid) return;
  const DioramaSkyboxProjection *sky = &projection->bg2_skybox;
  for (unsigned i = 0; i < sky->count; i++) {
    if (i >= kDioramaBgMaxValidSpans || !ValidSkyboxBand(&sky->bands[i])) {
      evidence->failed = true;
      return;
    }
    evidence->skybox = true;
    DioramaProjection active = *projection;
    active.bg2_skybox.active_band = (int)i;
    for (int band = 0; band < kDioramaEvidenceBands; band++) {
      const DioramaEvidenceBounds source = evidence->source[band];
      const DioramaSkyboxBandProjection *s = &sky->bands[i];
      const float x0 = fmaxf(source.x0, s->x0), x1 = fminf(source.x1, s->x1);
      const float y0 = fmaxf(source.y0, s->y0), y1 = fminf(source.y1, s->y1);
      if (!source.valid || x0 >= x1 || y0 >= y1) continue;
      for (int corner = 0; corner < 4; corner++) {
        ArRenderPointF point;
        if (!Diorama_ProjectCapturedBg2Point(&active, corner & 1 ? x1 : x0,
                corner & 2 ? y1 : y0, &point, NULL, NULL)) {
          evidence->failed = true;
          return;
        }
        EvidenceInclude(&evidence->output[2][band], point.x, point.y);
      }
    }
  }
}
