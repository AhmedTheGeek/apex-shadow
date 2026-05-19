#include <pebble.h>
#include <stdlib.h>
#include <string.h>

// Apex Shadow — a sundial-styled watchface.
//
// Two physical layers separated by exactly one cut, with a visible actor.
//
//   Gnomon: a small black triangle on the lit dial. Its tip touches the wedge
//   apex; its base widens on the side facing the sun. Rotates with the hour.
//   The wedge IS its shadow continuing outward — gnomon and wedge join at a
//   single point and read as one continuous object.
//
//   Top face: a dithered radial gradient lit from an off-screen point source
//   that orbits the dial opposite the current hour. The bright half is sharpened
//   (linear → mildly squared) so the sun's direction has a visible bloom near
//   the screen edge instead of a smooth, anonymous wash. A minimum floor keeps
//   the dim end visibly lit so the wedge reads as a deeper plane below it.
//
//   Cut (shadow wedge): a 66° wedge originating at the gnomon's tip, pointing
//   toward the current hour. The wedge is carved, not painted: a bright lip
//   catches light at the cut's top corner, a darker wall sits just inside the
//   lip, then the under-face opens beyond. Three pixel bands sell the cut.
//
//   Under face (inside the cut): a sparse near-black stipple is the surface,
//   one Bayer band below the top-face floor. The digital HH:MM is a custom
//   5×9 cinematic display face, NOT a seven-segment readout — real curves on
//   0/6/8/9, a true double-loop 8, a real diagonal slash on 7. Glyphs are
//   rotated to the wedge axis and shaded with directional rim/body/back.
//   Non-glyph pixels ray-trace toward the gnomon; pixels hit by a glyph's
//   shadow get a 4-pixel pure-black core plus a 6-pixel linear fade back to
//   the under-face base.
//
// Pixels are written directly to the framebuffer. Glyphs are drawn as bitmaps,
// not via Pebble system fonts (system fonts cannot rotate with the wedge and
// their antialiasing would betray the 1-bit medium).

static Window *s_window;
static Layer  *s_canvas_layer;
static int     s_hour   = 12;
static int     s_minute = 0;

#define SHADOW_HALF_TAN_x1000  650   // wide reveal wedge, ~33° half-angle
#define LIGHT_OFFSCREEN_MARGIN  38   // keep the light source center off-screen
#define LIGHT_FALLOFF_EXTRA     70
#define GNOMON_BACK             12   // gnomon extends this far back from center
#define GNOMON_BASE_HALF         4   // half-width at the base (lit side)
// Locked geometry — the digit size and on-screen position are constant at
// every hour. The auto-scale solver and dynamic camera pull are gone; their
// jitter (scale 4 at diagonals, scale 2 at cardinal sides) was the source of
// the "digits look different at different times" problem.
//
// Why scale 3: scale 4 cannot fit inside the wedge at cardinal angles on a
// 144x168 screen, even at maximum camera pull. Scale 3 fits everywhere.
//
// Why this camera_pull / reveal_radius: with scale 3, the wedge needs to be
// at least 52px from the dial center for the rotated glyph block to clear
// the wedge edges (9*3+5 height / tan(33°) ≈ 49, plus 3px safety). We place
// the digit center 20px ahead of screen center along the shadow axis (≈ the
// midpoint of the visible wedge at cardinal angles), which fixes the
// camera_pull at 52 − 20 = 32. Same at every hour.
#define LOCKED_TIME_SCALE        3
#define LOCKED_DIGIT_OFFSET     20    // digit center distance from screen_center along shadow axis
#define LOCKED_REVEAL           52    // distance from dial center to digit center along shadow axis
#define LOCKED_CAMERA_PULL      32    // = LOCKED_REVEAL - LOCKED_DIGIT_OFFSET
#define TIME_WEDGE_MARGIN        5    // vertical clearance from glyph top/bottom to wedge edge

// Four-band tonal architecture. Each band lands in a distinct Bayer-threshold
// density so the eye reads each as a separate depth:
//
//   Top-face lit zone (outside wedge)
//   ├── bright bloom: amplified above linear, near the sun edge
//   ├── lit core:    80–255 → 5–16 white dots per 4×4   (lit surface)
//   └── dim floor:   56–80  → 3–5 dots                  (still surface)
//   Gnomon
//   └── silhouette:  0      → 0 dots                    (the actor)
//   Wedge cut (carved, three pixel bands)
//   ├── edge lip:    230    → 14–15 dots                (light catching the cut top)
//   ├── cut wall:    4      → 0–1 dots                  (the wall just inside the lip)
//   ├── under-face:  16–31  → 1–2 dots                  (the plane below)
//   ├── shadow fade: 0→base → 0→1 dots                  (returning to plane)
//   └── shadow core: 0      → 0 dots                    (the void at the digit's heel)
#define TOP_FACE_FLOOR          56    // dim end of the gradient never dithers below this
#define TOP_FACE_BLOOM_KNEE     100   // intensities above this get amplified
#define TOP_FACE_BLOOM_NUM      3     // ratio for the amplification (3/2)
#define TOP_FACE_BLOOM_DEN      2
// Diurnal light. The off-screen sun's intensity varies through the 24-hour
// cycle: brightest at noon (full), dimmest at midnight (half). Only the top
// face dims; the wedge, lip, and shadows are unaffected, so the cut metaphor
// holds at every hour while the mood quietly shifts from noir to harsh-noon.
#define DAY_BRIGHT_x256        256    // peak top-face intensity multiplier at noon
#define NIGHT_DIM_x256         128    // peak top-face intensity multiplier at midnight
#define WEDGE_LIP_THICKNESS      1    // thickness of the bright cut-edge lip
#define WEDGE_LIP_GLOW         230    // intensity of the bright lip
#define WEDGE_WALL_THICKNESS     2    // thickness of the dark wall behind the lip
#define WEDGE_WALL_INTENSITY     4    // wall is darker than the under-face base
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

// Apex Display — a 5×9 cinematic glyph set. Geometric, slightly condensed,
// hand-drawn at native resolution. Five-bit rows; bit 4 is the leftmost column.
// Not segment-built: real curves on 0/6/8/9, a true double-loop 8 (the
// "tell" that distinguishes this from a seven-segment readout), a real
// diagonal on 7, a slim 1 with a flag, a triple-hook 3.
static const uint8_t s_digit_rows[10][9] = {
  // '0' — open ring
  { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E },
  // '1' — slim with a flag
  { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E },
  // '2'
  { 0x0E, 0x11, 0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F },
  // '3' — three hooks on a right spine
  { 0x0F, 0x01, 0x01, 0x01, 0x07, 0x01, 0x01, 0x01, 0x0F },
  // '4' — decisive double stem
  { 0x11, 0x11, 0x11, 0x11, 0x1F, 0x01, 0x01, 0x01, 0x01 },
  // '5'
  { 0x1F, 0x10, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E },
  // '6' — top hook, full bottom loop
  { 0x0E, 0x10, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x11, 0x0E },
  // '7' — single sharp diagonal slash
  { 0x1F, 0x01, 0x02, 0x02, 0x04, 0x04, 0x08, 0x08, 0x10 },
  // '8' — true double loop (NOT lit-segments)
  { 0x0E, 0x11, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x11, 0x0E },
  // '9' — mirror of 6, hooked tail
  { 0x0E, 0x11, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x11, 0x0E },
};

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
  if (c < '0' || c > '9') return;
  const uint8_t *rows = s_digit_rows[c - '0'];
  for (int row = 0; row < 9; row++) {
    uint8_t bits = rows[row];
    for (int col = 0; col < 5; col++) {
      if (bits & (1 << (4 - col))) {
        mask_draw_axis_rect(mask, w, h, origin, x_axis_x, x_axis_y,
                            y_axis_x, y_axis_y,
                            x_offset + col * scale, row * scale,
                            scale, scale);
      }
    }
  }
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

  // Locked geometry — same at every hour. See the header defines for why.
  int time_scale    = LOCKED_TIME_SCALE;
  int reveal_radius = LOCKED_REVEAL;
  int camera_pull   = LOCKED_CAMERA_PULL;

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

  // The display HH:MM string.
  int display_hour = s_hour;
  if (!clock_is_24h_style()) {
    display_hour %= 12;
    if (display_hour == 0) display_hour = 12;
  }
  char time_buf[6];
  snprintf(time_buf, sizeof(time_buf), "%02d:%02d", display_hour, s_minute);

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

      // Wedge / gnomon classification. The two share the same shadow axis:
      // gnomon lives at proj_along < 0 (lit side), wedge at proj_along >= 0.
      // Both taper to a single pixel at proj_along = 0, so they read as one
      // continuous shape: solid triangle joining its own cast shadow.
      int proj_along = (gx * sin_h - gy * cos_h) / TRIG_MAX_RATIO;
      int proj_perp  = (gx * cos_h + gy * sin_h) / TRIG_MAX_RATIO;
      int abs_perp   = proj_perp < 0 ? -proj_perp : proj_perp;

      int max_perp  = 0;
      bool in_wedge = false;
      if (proj_along >= 0) {
        max_perp = (proj_along * SHADOW_HALF_TAN_x1000) / 1000 + 1;
        if (abs_perp <= max_perp) in_wedge = true;
      }

      bool in_gnomon = false;
      if (!in_wedge && proj_along < 0 && proj_along > -GNOMON_BACK - 1) {
        int from_tip = -proj_along;  // 1 at tip, GNOMON_BACK at base
        int half_w = ((from_tip - 1) * GNOMON_BASE_HALF) / (GNOMON_BACK - 1);
        if (abs_perp <= half_w) in_gnomon = true;
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
        } else if (edge_dist < WEDGE_LIP_THICKNESS) {
          // Bright lip catching light along the cut's top corner.
          intensity = WEDGE_LIP_GLOW;
        } else if (edge_dist < WEDGE_LIP_THICKNESS + WEDGE_WALL_THICKNESS) {
          // Dark wall just inside the lip — the cut's inner face.
          intensity = WEDGE_WALL_INTENSITY;
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
      } else if (in_gnomon) {
        // Solid silhouette — the visible actor casting the wedge as its
        // shadow. Slightly darker than the under-face base so the gnomon
        // reads as a real object on the lit dial, not a continuation of
        // the cut.
        intensity = 0;
      } else {
        // Top face. Diurnal dim, then a mild knee-bloom on the bright half
        // so the off-screen sun has a locatable bloom near the screen edge
        // instead of a smooth anonymous wash. Floor keeps the dim end lit
        // so the wedge below reads as a deeper plane.
        intensity = (intensity * day_factor_x256) / 256;
        if (intensity > TOP_FACE_BLOOM_KNEE) {
          intensity = TOP_FACE_BLOOM_KNEE +
              ((intensity - TOP_FACE_BLOOM_KNEE) * TOP_FACE_BLOOM_NUM) /
              TOP_FACE_BLOOM_DEN;
          if (intensity > 255) intensity = 255;
        }
        if (intensity < TOP_FACE_FLOOR) intensity = TOP_FACE_FLOOR;
      }

      bool white = intensity > s_bayer[y & 3][x & 3];
      fb_set(row.data, x, fmt, white);
    }
  }
  free(time_mask);
  graphics_release_frame_buffer(ctx, fb);
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
