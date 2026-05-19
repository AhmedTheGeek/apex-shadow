#include <pebble.h>
#include <stdlib.h>
#include <string.h>

// Apex Shadow — a sundial-styled watchface.
//
// Two physical layers, separated by exactly one cut.
//
//   Top face: a dithered radial gradient lit from an off-screen point source
//   that orbits the dial opposite the current hour. Only black-and-white pixels
//   land in the framebuffer; mid-tones come from 4×4 Bayer ordered dither. A
//   minimum floor keeps even the gradient's dim end visibly lit, so the wedge
//   reads as a deeper plane below it rather than as more of the same gradient.
//
//   Cut (shadow wedge): a 66° wedge originating at a virtual gnomon point and
//   pointing toward the current hour. The mouth opens onto the screen because
//   the virtual gnomon is pulled backward by a camera offset, so the wedge
//   reads as a zoomed-in view of a larger imagined dial. A bright single-pixel
//   lip just inside the wedge boundary catches the light at the cut edge.
//
//   Under face (inside the cut): a sparse near-black stipple is the surface,
//   one Bayer band below the top-face floor. The digital HH:MM is rasterized
//   as a rotated 7-segment mask aligned with the wedge axis; each glyph pixel
//   gets a directional rim/body/back shading. Non-glyph pixels ray-trace back
//   toward the gnomon, and if a glyph blocks the under-face's light source
//   they sit in its drop shadow: pure black for a four-pixel core, then a six-
//   pixel linear fade back to the under-face base.
//
// Pixels are written directly to the framebuffer. Glyphs are drawn as bitmasks,
// not via Pebble system fonts (system fonts cannot rotate with the wedge and
// their antialiasing would betray the 1-bit medium).

static Window *s_window;
static Layer  *s_canvas_layer;
static int     s_hour   = 12;
static int     s_minute = 0;

#define SHADOW_HALF_TAN_x1000  650   // wide reveal wedge, ~33° half-angle
#define WEDGE_NEAR_GAP           9   // pixels of dead-zone at the wedge origin
#define LIGHT_OFFSCREEN_MARGIN  38   // keep the light source center off-screen
#define LIGHT_FALLOFF_EXTRA     70
#define CAMERA_PULL_MIN         14
#define CAMERA_PULL_MAX         34
#define CAMERA_REVEAL_MARGIN    18
#define TIME_SCALE_NEAR          4
#define TIME_SCALE_FAR           2
#define TIME_WEDGE_MARGIN        5    // vertical clearance from glyph top/bottom to wedge edge
#define TIME_WIDTH_PAD           8    // horizontal clearance from glyph end to wedge tip
#define TIME_MIN_RADIUS_BUMP     3    // bump above the geometric minimum reveal radius
#define REVEAL_RADIUS_FALLBACK  28    // used when no scale step fits the wedge
#define REVEAL_RADIUS_MIN       38    // smallest reveal-screen-radius accepted

// Three-band tonal architecture. The eye reads each band as a distinct depth
// because each lands in a different Bayer-threshold band on the 4×4 matrix:
//
//   Top-face lit zone (outside wedge)
//   ├── lit core:   80–255  →  5–16 white dots per 4×4   (lit surface)
//   └── dim floor:  56–80   →  3–5 dots                  (still surface)
//   Wedge cut
//   ├── edge lip:   210     →  13 dots                   (light catching the cut)
//   ├── under-face: 16–31   →  1–2 dots                  (the plane below)
//   ├── shadow fade: 0→base →  0→1 dots                  (returning to plane)
//   └── shadow core: 0      →  0 dots                    (the void itself)
#define TOP_FACE_FLOOR          56    // dim end of the gradient never dithers below this
// Diurnal light. The off-screen sun's intensity varies through the 24-hour
// cycle: brightest at noon (full), dimmest at midnight (half). Only the top
// face dims; the wedge, lip, and shadows are unaffected, so the cut metaphor
// holds at every hour while the mood quietly shifts from noir to harsh-noon.
#define DAY_BRIGHT_x256        256    // peak top-face intensity multiplier at noon
#define NIGHT_DIM_x256         128    // peak top-face intensity multiplier at midnight
#define WEDGE_EDGE_GLOW        210    // bright lip just inside the wedge boundary
#define WEDGE_EDGE_LIP           1    // thickness of the lip in pixels
#define UNDER_BASE_INTENSITY    16    // wedge interior, the deepest readable surface
#define UNDER_BASE_NOISE        15    // power-of-two-minus-one mask for stipple
#define DROP_SHADOW_LENGTH      10    // total reach of a digit's shadow trail
#define DROP_SHADOW_CORE         4    // pixels of pure black before the fade begins

// Bayer 4x4 ordered-dither matrix, normalized to 0..240 (×16).
static const uint8_t s_bayer[4][4] = {
  {   0, 128,  32, 160 },
  { 192,  64, 224,  96 },
  {  48, 176,  16, 144 },
  { 240, 112, 208,  80 },
};

static inline bool fb_is_black(const uint8_t *row, int x, GBitmapFormat fmt) {
  if (fmt == GBitmapFormat1Bit) {
    return !(row[x >> 3] & (1 << (x & 7)));
  }
  // 8Bit (basalt/chalk): black is 0xC0, white is 0xFF.
  return row[x] == 0xC0;
}

static inline void fb_set(uint8_t *row, int x, GBitmapFormat fmt, bool white) {
  if (fmt == GBitmapFormat1Bit) {
    if (white) row[x >> 3] |=  (1 << (x & 7));
    else       row[x >> 3] &= ~(1 << (x & 7));
  } else {
    row[x] = white ? 0xFF : 0xC0;
  }
}

static int isqrt_approx(int v) {
  // Cheap integer square root good enough for distance shading.
  if (v <= 0) return 0;
  int x = v, y = (x + 1) >> 1;
  while (y < x) { x = y; y = (x + v / x) >> 1; }
  return x;
}

static inline int clamp_int(int v, int min, int max) {
  if (v < min) return min;
  if (v > max) return max;
  return v;
}

static int ray_to_edge(GRect bounds, GPoint p, int32_t dir_x, int32_t dir_y) {
  int best = 10000;
  if (dir_x > 0) {
    best = ((bounds.size.w - 1 - p.x) * TRIG_MAX_RATIO) / dir_x;
  } else if (dir_x < 0) {
    best = (p.x * TRIG_MAX_RATIO) / -dir_x;
  }

  if (dir_y > 0) {
    int y_dist = ((bounds.size.h - 1 - p.y) * TRIG_MAX_RATIO) / dir_y;
    if (y_dist < best) best = y_dist;
  } else if (dir_y < 0) {
    int y_dist = (p.y * TRIG_MAX_RATIO) / -dir_y;
    if (y_dist < best) best = y_dist;
  }

  return best;
}

static inline bool mask_get(const uint8_t *mask, int w, int h, int x, int y) {
  if (x < 0 || y < 0 || x >= w || y >= h) return false;
  int index = y * w + x;
  return (mask[index >> 3] & (1 << (index & 7))) != 0;
}

static inline void mask_set(uint8_t *mask, int w, int x, int y) {
  int index = y * w + x;
  mask[index >> 3] |= (1 << (index & 7));
}

static inline void mask_set_safe(uint8_t *mask, int w, int h, int x, int y) {
  if (x < 0 || y < 0 || x >= w || y >= h) return;
  mask_set(mask, w, x, y);
}

// Snap a direction vector to one of 8 step directions (N, NE, E, … NW).
// Threshold ≈ tan(22.5°) so ±22.5° around an axis stays cardinal.
static void snap_step8(int32_t vx, int32_t vy, int *sx, int *sy) {
  int avx = vx < 0 ? -vx : vx;
  int avy = vy < 0 ? -vy : vy;
  int sxp = vx > 0 ? 1 : (vx < 0 ? -1 : 0);
  int syp = vy > 0 ? 1 : (vy < 0 ? -1 : 0);
  // 414/1000 ≈ tan(22.5°)
  if (avy * 1000 < avx * 414) {
    *sx = sxp; *sy = 0;
  } else if (avx * 1000 < avy * 414) {
    *sx = 0; *sy = syp;
  } else {
    *sx = sxp; *sy = syp;
  }
}

static int under_time_intensity(const uint8_t *mask, int w, int h, int x, int y,
                                int lx, int ly, int under_x, int under_y) {
  // Rim-light the edge facing the underlayer light; darken the opposite edge.
  bool lit_edge = !mask_get(mask, w, h, x + lx, y + ly);
  if (!lit_edge && lx) lit_edge = !mask_get(mask, w, h, x + lx, y);
  if (!lit_edge && ly) lit_edge = !mask_get(mask, w, h, x, y + ly);

  bool dark_edge = !mask_get(mask, w, h, x - lx, y - ly);
  if (!dark_edge && lx) dark_edge = !mask_get(mask, w, h, x - lx, y);
  if (!dark_edge && ly) dark_edge = !mask_get(mask, w, h, x, y - ly);

  if (lit_edge) return 255;
  if (dark_edge) return 78 + ((under_x * 3 + under_y) & 15);
  return 150 + ((under_x + under_y * 3) & 31);
}

// Trace from (x,y) toward the underlayer light. If a digit pixel blocks the
// ray within DROP_SHADOW_LENGTH steps, (x,y) sits in that digit's drop shadow;
// return the distance to the blocker (1 = closest, deepest shadow).
static int wedge_drop_shadow_steps(const uint8_t *mask, int w, int h,
                                   int x, int y, int lx, int ly) {
  if (lx == 0 && ly == 0) return 0;
  for (int s = 1; s <= DROP_SHADOW_LENGTH; s++) {
    int tx = x + lx * s;
    int ty = y + ly * s;
    if (mask_get(mask, w, h, tx, ty)) return s;
  }
  return 0;
}

static uint8_t seven_seg_mask(char c) {
  switch (c) {
    case '0': return 0x3F;
    case '1': return 0x06;
    case '2': return 0x5B;
    case '3': return 0x4F;
    case '4': return 0x66;
    case '5': return 0x6D;
    case '6': return 0x7D;
    case '7': return 0x07;
    case '8': return 0x7F;
    case '9': return 0x6F;
  }
  return 0;
}

static int glyph_width(char c, int scale) {
  if (c == ':') return 2 * scale;
  return 5 * scale;
}

static int text_width(const char *txt, int scale) {
  int width = 0;
  int spacing = scale;
  for (int i = 0; txt[i]; i++) {
    if (i) width += spacing;
    width += glyph_width(txt[i], scale);
  }
  return width;
}

static void mask_draw_axis_pixel(uint8_t *mask, int w, int h, GPoint origin,
                                 int32_t x_axis_x, int32_t x_axis_y,
                                 int32_t y_axis_x, int32_t y_axis_y,
                                 int lx, int ly) {
  int sx = origin.x + (x_axis_x * lx + y_axis_x * ly) / TRIG_MAX_RATIO;
  int sy = origin.y + (x_axis_y * lx + y_axis_y * ly) / TRIG_MAX_RATIO;
  mask_set_safe(mask, w, h, sx, sy);
}

static void mask_draw_axis_rect(uint8_t *mask, int w, int h, GPoint origin,
                                int32_t x_axis_x, int32_t x_axis_y,
                                int32_t y_axis_x, int32_t y_axis_y,
                                int rx, int ry, int rw, int rh) {
  for (int y = ry; y < ry + rh; y++) {
    for (int x = rx; x < rx + rw; x++) {
      mask_draw_axis_pixel(mask, w, h, origin, x_axis_x, x_axis_y,
                           y_axis_x, y_axis_y, x, y);
    }
  }
}

static void mask_draw_digit(uint8_t *mask, int w, int h, GPoint origin,
                            int32_t x_axis_x, int32_t x_axis_y,
                            int32_t y_axis_x, int32_t y_axis_y,
                            char c, int scale, int x_offset) {
  uint8_t seg = seven_seg_mask(c);
  int gw = 5 * scale;
  int gh = 9 * scale;
  int t = scale;
  int mid = gh / 2;

  if (seg & 0x01) mask_draw_axis_rect(mask, w, h, origin, x_axis_x, x_axis_y, y_axis_x, y_axis_y, x_offset + t, 0, gw - 2 * t, t);
  if (seg & 0x02) mask_draw_axis_rect(mask, w, h, origin, x_axis_x, x_axis_y, y_axis_x, y_axis_y, x_offset + gw - t, t, t, mid - t);
  if (seg & 0x04) mask_draw_axis_rect(mask, w, h, origin, x_axis_x, x_axis_y, y_axis_x, y_axis_y, x_offset + gw - t, mid, t, gh - mid - t);
  if (seg & 0x08) mask_draw_axis_rect(mask, w, h, origin, x_axis_x, x_axis_y, y_axis_x, y_axis_y, x_offset + t, gh - t, gw - 2 * t, t);
  if (seg & 0x10) mask_draw_axis_rect(mask, w, h, origin, x_axis_x, x_axis_y, y_axis_x, y_axis_y, x_offset, mid, t, gh - mid - t);
  if (seg & 0x20) mask_draw_axis_rect(mask, w, h, origin, x_axis_x, x_axis_y, y_axis_x, y_axis_y, x_offset, t, t, mid - t);
  if (seg & 0x40) mask_draw_axis_rect(mask, w, h, origin, x_axis_x, x_axis_y, y_axis_x, y_axis_y, x_offset + t, mid - t / 2, gw - 2 * t, t);
}

static void mask_draw_colon(uint8_t *mask, int w, int h, GPoint origin,
                            int32_t x_axis_x, int32_t x_axis_y,
                            int32_t y_axis_x, int32_t y_axis_y,
                            int scale, int x_offset) {
  mask_draw_axis_rect(mask, w, h, origin, x_axis_x, x_axis_y,
                      y_axis_x, y_axis_y, x_offset, 2 * scale, scale, scale);
  mask_draw_axis_rect(mask, w, h, origin, x_axis_x, x_axis_y,
                      y_axis_x, y_axis_y, x_offset, 6 * scale, scale, scale);
}

static void mask_draw_segment_text(uint8_t *mask, int w, int h, GPoint center,
                                   const char *txt, int scale,
                                   int32_t x_axis_x, int32_t x_axis_y,
                                   int32_t y_axis_x, int32_t y_axis_y) {
  int tw = text_width(txt, scale);
  int th = 9 * scale;
  int spacing = scale;
  GPoint origin = {
    .x = center.x - (x_axis_x * (tw / 2) + y_axis_x * (th / 2)) / TRIG_MAX_RATIO,
    .y = center.y - (x_axis_y * (tw / 2) + y_axis_y * (th / 2)) / TRIG_MAX_RATIO,
  };

  int x = 0;
  for (int i = 0; txt[i]; i++) {
    if (i) x += spacing;
    if (txt[i] >= '0' && txt[i] <= '9') {
      mask_draw_digit(mask, w, h, origin, x_axis_x, x_axis_y,
                      y_axis_x, y_axis_y, txt[i], scale, x);
    } else if (txt[i] == ':') {
      mask_draw_colon(mask, w, h, origin, x_axis_x, x_axis_y,
                      y_axis_x, y_axis_y, scale, x);
    }
    x += glyph_width(txt[i], scale);
  }
}

static void draw_under_time_mask(uint8_t *time_mask, int w, int h, GPoint center,
                                 int32_t line_x, int32_t line_y,
                                 const char *time_txt, int reveal_radius,
                                 int time_scale) {
  int32_t text_x = line_x;
  int32_t text_y = line_y;
  int32_t text_down_x = -line_y;
  int32_t text_down_y = line_x;

  if (text_x < 0) {
    text_x = -text_x;
    text_y = -text_y;
    text_down_x = -text_down_x;
    text_down_y = -text_down_y;
  }

  GPoint time_center = {
    .x = center.x + (line_x * reveal_radius) / TRIG_MAX_RATIO,
    .y = center.y + (line_y * reveal_radius) / TRIG_MAX_RATIO,
  };

  mask_draw_segment_text(time_mask, w, h, time_center, time_txt,
                         time_scale, text_x, text_y,
                         text_down_x, text_down_y);
}

static void canvas_update(Layer *layer, GContext *ctx) {
  GRect  bounds = layer_get_bounds(layer);
  GPoint screen_center = grect_center_point(&bounds);

  // ── Angles ────────────────────────────────────────────────────────────
  int hour12 = s_hour % 12;
  int32_t hour_angle = (TRIG_MAX_ANGLE * (hour12 * 60 + s_minute)) / (12 * 60);
  int32_t sun_angle  = (hour_angle + TRIG_MAX_ANGLE / 2) & 0xFFFF;
  int32_t sin_h = sin_lookup(hour_angle);
  int32_t cos_h = cos_lookup(hour_angle);

  int forward_space = ray_to_edge(bounds, screen_center, sin_h, -cos_h);
  int backward_space = ray_to_edge(bounds, screen_center, -sin_h, cos_h);
  int camera_pull = clamp_int(backward_space / 3, CAMERA_PULL_MIN,
                              CAMERA_PULL_MAX);
  int reveal_screen_radius = (forward_space * 2) / 3;
  reveal_screen_radius = clamp_int(reveal_screen_radius, REVEAL_RADIUS_MIN,
                                   forward_space - CAMERA_REVEAL_MARGIN);
  int reveal_radius = reveal_screen_radius + camera_pull;

  // Treat the screen like a zoomed-in crop of a larger dial. Pulling the
  // virtual center backward gives the reveal wedge more visible length.
  GPoint center = {
    .x = screen_center.x - (sin_h * camera_pull) / TRIG_MAX_RATIO,
    .y = screen_center.y + (cos_h * camera_pull) / TRIG_MAX_RATIO,
  };

  int half_diag = isqrt_approx(bounds.size.w * bounds.size.w +
                               bounds.size.h * bounds.size.h) / 2;

  int32_t to_light_x = sin_lookup(sun_angle);
  int32_t to_light_y = -cos_lookup(sun_angle);

  // Underlayer light emits from the gnomon (the wedge's origin) and shines
  // outward along the wedge. From any wedge pixel, the direction back toward
  // that source is the same direction as the off-screen top-face sun lies in
  // (both are on the gnomon side, opposite the hour). So tracing toward
  // `to_light` finds digits between the pixel and the wedge's light source,
  // and drop shadows fall outward toward the wedge tip.
  int under_light_step_x, under_light_step_y;
  snap_step8(to_light_x, to_light_y,
             &under_light_step_x, &under_light_step_y);

  int light_radius = half_diag + LIGHT_OFFSCREEN_MARGIN;
  GPoint sun = {
    .x = center.x + (to_light_x * light_radius) / TRIG_MAX_RATIO,
    .y = center.y + (to_light_y * light_radius) / TRIG_MAX_RATIO,
  };
  int falloff = light_radius + half_diag + LIGHT_FALLOFF_EXTRA;

  // Diurnal light — cosine sweep around the 24-hour cycle, peak at noon.
  int total_min = s_hour * 60 + s_minute;
  int day_offset_min = (total_min - 12 * 60 + 24 * 60) % (24 * 60);
  int32_t day_phase  = (day_offset_min * TRIG_MAX_ANGLE) / (24 * 60);
  int32_t day_cos    = cos_lookup(day_phase);
  int day_avg = (DAY_BRIGHT_x256 + NIGHT_DIM_x256) / 2;
  int day_amp = (DAY_BRIGHT_x256 - NIGHT_DIM_x256) / 2;
  int day_factor_x256 = day_avg + (day_amp * day_cos) / TRIG_MAX_RATIO;

  // The display HH:MM string. Built first so the layout solver can fit it.
  int display_hour = s_hour;
  if (!clock_is_24h_style()) {
    display_hour %= 12;
    if (display_hour == 0) display_hour = 12;
  }
  char time_buf[6];
  snprintf(time_buf, sizeof(time_buf), "%02d:%02d", display_hour, s_minute);

  // Auto-scale the rotated time mask so it always fits inside the wedge.
  int available_forward = ray_to_edge(bounds, center, sin_h, -cos_h);
  int time_scale = TIME_SCALE_FAR;
  int fitted_reveal_radius = REVEAL_RADIUS_FALLBACK;
  for (int scale = TIME_SCALE_NEAR; scale >= TIME_SCALE_FAR; scale--) {
    int time_half_w = text_width(time_buf, scale) / 2 + TIME_WIDTH_PAD;
    int time_half_h = 9 * scale + TIME_WEDGE_MARGIN;
    int max_reveal_radius = available_forward - time_half_w;
    int min_reveal_radius =
        (time_half_h * 1000) / SHADOW_HALF_TAN_x1000 + TIME_MIN_RADIUS_BUMP;

    if (max_reveal_radius < min_reveal_radius) continue;

    time_scale = scale;
    fitted_reveal_radius = clamp_int(reveal_radius, min_reveal_radius,
                                     max_reveal_radius);
    break;
  }
  reveal_radius = fitted_reveal_radius;

  // ── Build the rotated time mask, then rewrite every pixel ─────────────
  // The initial framebuffer state doesn't matter; the per-pixel loop below
  // writes every pixel inside the layer bounds.
  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GBitmapFormat fmt = gbitmap_get_format(fb);

  int mask_bytes = (bounds.size.w * bounds.size.h + 7) / 8;
  uint8_t *time_mask = malloc(mask_bytes);
  if (!time_mask) {
    graphics_release_frame_buffer(ctx, fb);
    return;
  }
  memset(time_mask, 0, mask_bytes);

  draw_under_time_mask(time_mask, bounds.size.w, bounds.size.h,
                       center, sin_h, -cos_h, time_buf, reveal_radius,
                       time_scale);

  for (int y = 0; y < bounds.size.h; y++) {
    GBitmapDataRowInfo row = gbitmap_get_data_row_info(fb, y);
    if (!row.data) continue;
    for (int x = row.min_x; x <= row.max_x; x++) {

      bool is_time = mask_get(time_mask, bounds.size.w, bounds.size.h, x, y);

      int gx = x - center.x;
      int gy = y - center.y;
      int dxs = x - sun.x;
      int dys = y - sun.y;
      int dist = isqrt_approx(dxs * dxs + dys * dys);
      int intensity = 255 - (dist * 255) / falloff;
      if (intensity < 0)   intensity = 0;
      if (intensity > 255) intensity = 255;

      // Is this pixel inside the shadow wedge?
      int proj_along = (gx * sin_h - gy * cos_h) / TRIG_MAX_RATIO;
      int proj_perp  = (gx * cos_h + gy * sin_h) / TRIG_MAX_RATIO;
      int abs_perp   = proj_perp < 0 ? -proj_perp : proj_perp;

      int max_perp  = 0;
      bool in_wedge = false;
      if (proj_along > WEDGE_NEAR_GAP - 1) {
        max_perp = (proj_along * SHADOW_HALF_TAN_x1000) / 1000 + 1;
        if (abs_perp <= max_perp) in_wedge = true;
      }

      if (in_wedge) {
        int edge_dist = max_perp - abs_perp;  // 0 at boundary, max_perp at axis
        int base = UNDER_BASE_INTENSITY +
                   ((x * 7 + y * 3) & UNDER_BASE_NOISE);

        if (is_time) {
          intensity = under_time_intensity(time_mask, bounds.size.w,
                                           bounds.size.h, x, y,
                                           under_light_step_x,
                                           under_light_step_y,
                                           x, y);
        } else if (edge_dist <= WEDGE_EDGE_LIP) {
          // Bright lip catching light along the upper-face's cut edge.
          intensity = WEDGE_EDGE_GLOW;
        } else {
          int hit = wedge_drop_shadow_steps(time_mask, bounds.size.w,
                                            bounds.size.h, x, y,
                                            under_light_step_x,
                                            under_light_step_y);
          if (hit > 0) {
            if (hit <= DROP_SHADOW_CORE) {
              intensity = 0;
            } else {
              int fade_steps    = DROP_SHADOW_LENGTH - DROP_SHADOW_CORE;
              int fade_progress = hit - DROP_SHADOW_CORE;
              intensity = (base * fade_progress) / fade_steps;
            }
          } else {
            intensity = base;
          }
        }
      } else {
        // Top-face dim floor — the lit surface never collapses to pure black,
        // so the wedge interior reads as a deeper plane below it. The diurnal
        // factor dims the gradient amplitude before the floor clamps it, so at
        // night most of the surface settles at the floor and the gradient is
        // narrow and moody; at noon it spans the full range.
        intensity = (intensity * day_factor_x256) / 256;
        if (intensity < TOP_FACE_FLOOR) intensity = TOP_FACE_FLOOR;
      }

      bool white = intensity > s_bayer[y & 3][x & 3];
      fb_set(row.data, x, fmt, white);
    }
  }
  free(time_mask);
  graphics_release_frame_buffer(ctx, fb);

  // The old raised gnomon is intentionally not drawn. The reveal wedge itself
  // now carries the sundial/shadow role, and a fixed vertical cap looked like
  // an artifact once the virtual camera started moving.
}

static void tick_handler(struct tm *time, TimeUnits changed) {
  s_hour   = time->tm_hour;
  s_minute = time->tm_min;
  if (s_canvas_layer) layer_mark_dirty(s_canvas_layer);
}

static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update);
  layer_add_child(root, s_canvas_layer);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas_layer);
}

static void init(void) {
  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  s_hour   = t->tm_hour;
  s_minute = t->tm_min;

  window_stack_push(s_window, true);
  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
