#include <pebble.h>
#include <stdlib.h>
#include <string.h>

// Apex Shadow — a sundial-styled watchface.
//
// An off-screen sun sits opposite the current hour. It throws a Bayer-dithered
// directional gradient across the face. A gnomon at the center blocks the light,
// cutting a hard black wedge that points to the hour.
//
// The shadow wedge works like a cutaway into a lower layer. A digital time mask
// sits under the dial and only peeks through the revealed wedge.

static Window *s_window;
static Layer  *s_canvas_layer;
static int     s_hour   = 12;
static int     s_minute = 0;

#define SHADOW_HALF_TAN_x1000  650   // wide reveal wedge, ~33° half-angle
#define GNOMON_HALF_H            9
#define LIGHT_OFFSCREEN_MARGIN  38   // keep the light source center off-screen
#define LIGHT_FALLOFF_EXTRA     70
#define CAMERA_PULL_MIN         14
#define CAMERA_PULL_MAX         34
#define CAMERA_REVEAL_MARGIN    18
#define TIME_SCALE_NEAR          4
#define TIME_SCALE_FAR           2
#define TIME_WEDGE_MARGIN        5
#define UNDER_ZOOM_MIN_x1000  1160
#define UNDER_ZOOM_MAX_x1000  1450

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

static inline void mask_or(uint8_t *dst, const uint8_t *src, int bytes) {
  for (int i = 0; i < bytes; i++) dst[i] |= src[i];
}

static int under_time_intensity(const uint8_t *mask, int w, int h, int x, int y,
                                int under_x, int under_y) {
  bool lit_edge = !mask_get(mask, w, h, x - 1, y - 1) ||
                  !mask_get(mask, w, h, x, y - 1) ||
                  !mask_get(mask, w, h, x - 1, y);
  bool dark_edge = !mask_get(mask, w, h, x + 1, y + 1) ||
                   !mask_get(mask, w, h, x, y + 1) ||
                   !mask_get(mask, w, h, x + 1, y);

  if (lit_edge) return 255;
  if (dark_edge) return 78 + ((under_x * 3 + under_y) & 15);
  return 150 + ((under_x + under_y * 3) & 31);
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
  if (c == ' ') return 2 * scale;
  if (c >= 'A' && c <= 'Z') return 3 * scale;
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

static int text_height(const char *txt, int scale) {
  int height = 0;
  for (int i = 0; txt[i]; i++) {
    int char_h = (txt[i] >= 'A' && txt[i] <= 'Z') ? 5 * scale : 9 * scale;
    if (char_h > height) height = char_h;
  }
  return height;
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

static const uint8_t *letter_rows(char c) {
  static const uint8_t A[5] = {2, 5, 7, 5, 5};
  static const uint8_t B[5] = {6, 5, 6, 5, 6};
  static const uint8_t C[5] = {7, 4, 4, 4, 7};
  static const uint8_t D[5] = {6, 5, 5, 5, 6};
  static const uint8_t E[5] = {7, 4, 6, 4, 7};
  static const uint8_t F[5] = {7, 4, 6, 4, 4};
  static const uint8_t G[5] = {7, 4, 5, 5, 7};
  static const uint8_t J[5] = {1, 1, 1, 5, 7};
  static const uint8_t L[5] = {4, 4, 4, 4, 7};
  static const uint8_t M[5] = {5, 7, 7, 5, 5};
  static const uint8_t N[5] = {5, 7, 7, 7, 5};
  static const uint8_t O[5] = {7, 5, 5, 5, 7};
  static const uint8_t P[5] = {7, 5, 7, 4, 4};
  static const uint8_t R[5] = {6, 5, 6, 5, 5};
  static const uint8_t S[5] = {7, 4, 7, 1, 7};
  static const uint8_t T[5] = {7, 2, 2, 2, 2};
  static const uint8_t U[5] = {5, 5, 5, 5, 7};
  static const uint8_t V[5] = {5, 5, 5, 5, 2};
  static const uint8_t Y[5] = {5, 5, 2, 2, 2};

  switch (c) {
    case 'A': return A;
    case 'B': return B;
    case 'C': return C;
    case 'D': return D;
    case 'E': return E;
    case 'F': return F;
    case 'G': return G;
    case 'J': return J;
    case 'L': return L;
    case 'M': return M;
    case 'N': return N;
    case 'O': return O;
    case 'P': return P;
    case 'R': return R;
    case 'S': return S;
    case 'T': return T;
    case 'U': return U;
    case 'V': return V;
    case 'Y': return Y;
  }
  return NULL;
}

static void mask_draw_letter(uint8_t *mask, int w, int h, GPoint origin,
                             int32_t x_axis_x, int32_t x_axis_y,
                             int32_t y_axis_x, int32_t y_axis_y,
                             char c, int scale, int x_offset) {
  const uint8_t *rows = letter_rows(c);
  if (!rows) return;
  for (int row = 0; row < 5; row++) {
    for (int col = 0; col < 3; col++) {
      if (rows[row] & (1 << (2 - col))) {
        mask_draw_axis_rect(mask, w, h, origin, x_axis_x, x_axis_y,
                            y_axis_x, y_axis_y, x_offset + col * scale,
                            row * scale, scale, scale);
      }
    }
  }
}

static void mask_draw_segment_text(uint8_t *mask, int w, int h, GPoint center,
                                   const char *txt, int scale,
                                   int32_t x_axis_x, int32_t x_axis_y,
                                   int32_t y_axis_x, int32_t y_axis_y) {
  int tw = text_width(txt, scale);
  int th = 0;
  for (int i = 0; txt[i]; i++) {
    int char_h = (txt[i] >= 'A' && txt[i] <= 'Z') ? 5 * scale : 9 * scale;
    if (char_h > th) th = char_h;
  }
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
    } else if (txt[i] >= 'A' && txt[i] <= 'Z') {
      mask_draw_letter(mask, w, h, origin, x_axis_x, x_axis_y,
                       y_axis_x, y_axis_y, txt[i], scale, x);
    }
    x += glyph_width(txt[i], scale);
  }
}

static void draw_under_time_mask(uint8_t *time_mask, uint8_t *object_mask,
                                 int w, int h, GPoint center,
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
  int bytes = (w * h + 7) / 8;
  mask_or(object_mask, time_mask, bytes);
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
  reveal_screen_radius = clamp_int(reveal_screen_radius, 38,
                                   forward_space - CAMERA_REVEAL_MARGIN);
  int reveal_radius = reveal_screen_radius + camera_pull;
  int camera_zoom = UNDER_ZOOM_MIN_x1000 +
      ((camera_pull - CAMERA_PULL_MIN) *
       (UNDER_ZOOM_MAX_x1000 - UNDER_ZOOM_MIN_x1000)) /
      (CAMERA_PULL_MAX - CAMERA_PULL_MIN);

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

  int light_radius = half_diag + LIGHT_OFFSCREEN_MARGIN;
  GPoint sun = {
    .x = center.x + (to_light_x * light_radius) / TRIG_MAX_RATIO,
    .y = center.y + (to_light_y * light_radius) / TRIG_MAX_RATIO,
  };
  int falloff = light_radius + half_diag + LIGHT_FALLOFF_EXTRA;

  // ── Step 1: reset the framebuffer; object masks are drawn separately ───
  graphics_context_set_antialiased(ctx, false);
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  int display_hour = s_hour;
  if (!clock_is_24h_style()) {
    display_hour %= 12;
    if (display_hour == 0) display_hour = 12;
  }
  char time_buf[6];
  snprintf(time_buf, sizeof(time_buf), "%02d:%02d", display_hour, s_minute);
  int available_forward = ray_to_edge(bounds, center, sin_h, -cos_h);
  int time_scale = TIME_SCALE_FAR;
  int fitted_reveal_radius = 28;
  for (int scale = TIME_SCALE_NEAR; scale >= TIME_SCALE_FAR; scale--) {
    int time_half_w = text_width(time_buf, scale) / 2 + 8;
    int time_half_h = text_height(time_buf, scale) + TIME_WEDGE_MARGIN;
    int max_reveal_radius = available_forward - time_half_w;
    int min_reveal_radius = (time_half_h * 1000) / SHADOW_HALF_TAN_x1000 + 3;

    if (max_reveal_radius < min_reveal_radius) continue;

    time_scale = scale;
    fitted_reveal_radius = clamp_int(reveal_radius, min_reveal_radius,
                                     max_reveal_radius);
    break;
  }
  reveal_radius = fitted_reveal_radius;

  // ── Step 2: build masks, then rewrite every pixel ─────────────────────

  GBitmap *fb = graphics_capture_frame_buffer(ctx);
  if (!fb) return;
  GBitmapFormat fmt = gbitmap_get_format(fb);

  int mask_bytes = (bounds.size.w * bounds.size.h + 7) / 8;
  uint8_t *time_mask = malloc(mask_bytes);
  uint8_t *object_mask = malloc(mask_bytes);
  if (!time_mask || !object_mask) {
    if (time_mask) free(time_mask);
    if (object_mask) free(object_mask);
    graphics_release_frame_buffer(ctx, fb);
    return;
  }
  memset(time_mask, 0, mask_bytes);
  memset(object_mask, 0, mask_bytes);

  draw_under_time_mask(time_mask, object_mask, bounds.size.w, bounds.size.h,
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

      // Are we inside the gnomon's shadow wedge?
      int proj_along = (gx * sin_h - gy * cos_h) / TRIG_MAX_RATIO;
      int proj_perp  = (gx * cos_h + gy * sin_h) / TRIG_MAX_RATIO;
      int abs_perp   = proj_perp < 0 ? -proj_perp : proj_perp;

      bool in_wedge = false;
      if (proj_along > GNOMON_HALF_H - 1) {
        int max_perp = (proj_along * SHADOW_HALF_TAN_x1000) / 1000 + 1;
        if (abs_perp <= max_perp) in_wedge = true;
      }

      if (in_wedge) {
        int under_x = center.x + (gx * camera_zoom) / 1000;
        int under_y = center.y + (gy * camera_zoom) / 1000;
        intensity = 0;

        if (is_time) {
          intensity = under_time_intensity(object_mask, bounds.size.w,
                                           bounds.size.h, x, y,
                                           under_x, under_y);
        }
      }

      bool white = intensity > s_bayer[y & 3][x & 3];
      fb_set(row.data, x, fmt, white);
    }
  }
  free(time_mask);
  free(object_mask);
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
