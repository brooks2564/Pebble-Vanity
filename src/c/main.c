#include <pebble.h>

// ============================================================
// VANITY — A watchface that checks its own popularity
// ============================================================

#define PERSIST_KEY_HEARTS    1
#define PERSIST_KEY_COLOR     2
#define PERSIST_KEY_RANK      3

// --- Globals ---
static Window *s_window;
static Layer *s_window_layer;
static Layer *s_canvas_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_hearts_layer;
static TextLayer *s_label_layer;

static char s_time_buf[8];
static char s_date_buf[16];
static char s_hearts_buf[16];

static int s_hearts_count = -1;
static int s_prev_hearts = -1;
static int s_rank = -1;           // store rank, -1 = unknown
static int s_frame = 0;
static bool s_bt_connected = true;

// Pulse animation
static bool s_pulse_active = false;
static int s_pulse_frame = 0;
static AppTimer *s_pulse_timer = NULL;

// Confetti animation
static bool s_confetti_active = false;
static int s_confetti_frame = 0;
static AppTimer *s_confetti_timer = NULL;

// Loading animation
static bool s_loading_active = true;
static int s_loading_frame = 0;
static AppTimer *s_loading_timer = NULL;

// Milestone crown
static bool s_show_crown = false;
static int s_crown_frame = 0;
static AppTimer *s_crown_timer = NULL;

// Floating "+N" badge
static bool s_badge_active = false;
static int s_badge_frame = 0;
static int s_badge_delta = 0;
static AppTimer *s_badge_timer = NULL;

// Heart color
static int s_heart_color_idx = 0;

#ifdef PBL_COLOR
typedef struct {
  GColor main;
  GColor dark;
  GColor highlight;
} HeartPalette;

static const HeartPalette PALETTES[] = {
  { {.argb=GColorRedARGB8},         {.argb=GColorDarkCandyAppleRedARGB8}, {.argb=GColorMelonARGB8} },
  { {.argb=GColorBlueMoonARGB8},    {.argb=GColorOxfordBlueARGB8},        {.argb=GColorCelesteARGB8} },
  { {.argb=GColorGreenARGB8},       {.argb=GColorDarkGreenARGB8},         {.argb=GColorMintGreenARGB8} },
  { {.argb=GColorPurpleARGB8},      {.argb=GColorImperialPurpleARGB8},    {.argb=GColorLavenderIndigoARGB8} },
  { {.argb=GColorOrangeARGB8},      {.argb=GColorWindsorTanARGB8},        {.argb=GColorYellowARGB8} },
  { {.argb=GColorShockingPinkARGB8},{.argb=GColorFashionMagentaARGB8},    {.argb=GColorRichBrilliantLavenderARGB8} },
};
#define NUM_PALETTES 6
#endif

// EKG waveform
static const int8_t EKG_WAVE[] = {
  0, 0, 0, 0, 0, 0, 0, 0,
  0, -2, -1, 0, 1, 0, 0, 0,
  2, -8, 18, -20, 6, -2, 0,
  0, 0, 0, 0,
  -2, -4, -5, -4, -2, 0,
  0, 0, 0, 0, 0, 0, 0, 0,
  0, 0, 0, 0, 0, 0
};
#define EKG_LEN ((int)(sizeof(EKG_WAVE) / sizeof(EKG_WAVE[0])))

// ============================================================
// Round screen helper: horizontal inset needed at a given Y
// On a circle of diameter D, at row y the visible width is
// 2*sqrt(r² - (y-r)²) where r = D/2.
// Returns the per-side inset (pixels to skip on each side).
// For rectangular screens, returns 0.
// ============================================================
#ifdef PBL_ROUND
static int round_inset_at_y(int y, int screen_h, int screen_w) {
  int r = screen_w / 2;
  int dy = y - r;
  if (dy < -r || dy > r) return r; // fully clipped
  // Use integer math: width = 2*sqrt(r*r - dy*dy)
  int inner = r * r - dy * dy;
  // isqrt via Newton's method
  int s = r;
  if (inner <= 0) return r;
  for (int i = 0; i < 8; i++) {
    s = (s + inner / s) / 2;
  }
  int half_width = s;
  int inset = r - half_width + 4; // +4 for margin
  if (inset < 0) inset = 0;
  return inset;
}
#endif

// Confetti
#define NUM_CONFETTI 20
typedef struct {
  int x, y, vx, vy;
#ifdef PBL_COLOR
  GColor color;
#endif
} Confetti;
static Confetti s_confetti[NUM_CONFETTI];

// ============================================================
// EKG scroll speed based on heart count
// ============================================================
static int get_ekg_speed(void) {
  // Returns pixels to scroll per frame
  // 0 hearts = 1 (flatline crawl), scales up to 8 at 1000+
  if (s_hearts_count <= 0) return 1;
  if (s_hearts_count < 5) return 2;
  if (s_hearts_count < 20) return 3;
  if (s_hearts_count < 50) return 4;
  if (s_hearts_count < 100) return 5;
  if (s_hearts_count < 500) return 6;
  if (s_hearts_count < 1000) return 7;
  return 8;
}

// ============================================================
// Heart shape drawing
// ============================================================
static void draw_heart_filled(GContext *ctx, int cx, int cy, int size) {
  int r = size / 3;
  int half = (r * 3) / 2;

  graphics_fill_circle(ctx, GPoint(cx - r + 1, cy - r / 3), r);
  graphics_fill_circle(ctx, GPoint(cx + r - 1, cy - r / 3), r);

  for (int dy = 0; dy <= size - r; dy++) {
    int w = half - (dy * half) / (size - r);
    if (w < 1) w = 1;
    graphics_draw_line(ctx, GPoint(cx - w, cy + dy), GPoint(cx + w, cy + dy));
  }

  for (int dy = -r / 3; dy <= r / 4; dy++) {
    int cw = half - 1;
    graphics_draw_line(ctx, GPoint(cx - cw, cy + dy),
                       GPoint(cx + cw, cy + dy));
  }
}

static void draw_heart_outline(GContext *ctx, int cx, int cy, int size) {
  int r = size / 3;
  int half = (r * 3) / 2;

  graphics_draw_circle(ctx, GPoint(cx - r + 1, cy - r / 3), r);
  graphics_draw_circle(ctx, GPoint(cx + r - 1, cy - r / 3), r);

  for (int i = 0; i < 2; i++) {
    int x1 = (i == 0) ? (cx - half) : (cx + half);
    graphics_draw_line(ctx, GPoint(x1, cy), GPoint(cx, cy + size - r));
  }
}

static void draw_heart_crack(GContext *ctx, int cx, int cy, int size) {
  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);

  int top = cy - size / 4;
  int bot = cy + size / 2;
  int seg_h = (bot - top) / 5;

  GPoint pts[] = {
    GPoint(cx, top),
    GPoint(cx + size/8, top + seg_h),
    GPoint(cx - size/10, top + seg_h * 2),
    GPoint(cx + size/6, top + seg_h * 3),
    GPoint(cx - size/12, top + seg_h * 4),
    GPoint(cx, bot)
  };

  for (int i = 0; i < 5; i++) {
    graphics_draw_line(ctx, pts[i], pts[i+1]);
  }
}

// ============================================================
// Crown
// ============================================================
static void draw_crown(GContext *ctx, int cx, int cy, int w) {
#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorChromeYellow);
  graphics_context_set_stroke_color(ctx, GColorWindsorTan);
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_context_set_stroke_color(ctx, GColorWhite);
#endif

  int h = w / 2;
  int half_w = w / 2;

  graphics_fill_rect(ctx, GRect(cx - half_w, cy + h - 3, w, 3), 0, GCornerNone);

  int points_y = cy;
  graphics_draw_line(ctx, GPoint(cx - half_w, cy + h - 3), GPoint(cx - half_w + w/6, points_y));
  graphics_draw_line(ctx, GPoint(cx - half_w + w/6, points_y), GPoint(cx - half_w + w/3, cy + h/2));
  graphics_draw_line(ctx, GPoint(cx - half_w + w/3, cy + h/2), GPoint(cx, points_y - 2));
  graphics_draw_line(ctx, GPoint(cx, points_y - 2), GPoint(cx + half_w - w/3, cy + h/2));
  graphics_draw_line(ctx, GPoint(cx + half_w - w/3, cy + h/2), GPoint(cx + half_w - w/6, points_y));
  graphics_draw_line(ctx, GPoint(cx + half_w - w/6, points_y), GPoint(cx + half_w, cy + h - 3));

  for (int dy = 2; dy < h - 3; dy++) {
    int frac = (dy * 256) / h;
    int lw = half_w - (frac * half_w / 512);
    if (lw < 2) lw = 2;
    graphics_draw_line(ctx, GPoint(cx - lw, cy + dy), GPoint(cx + lw, cy + dy));
  }

#ifdef PBL_COLOR
  graphics_context_set_fill_color(ctx, GColorRed);
  graphics_fill_circle(ctx, GPoint(cx, cy + h/2 - 1), 2);
  graphics_context_set_fill_color(ctx, GColorBlueMoon);
  graphics_fill_circle(ctx, GPoint(cx - w/4, cy + h/2), 1);
  graphics_fill_circle(ctx, GPoint(cx + w/4, cy + h/2), 1);
#endif
}

// ============================================================
// Battery bar
// ============================================================
static void draw_battery_bar(GContext *ctx, GRect bounds) {
  BatteryChargeState charge = battery_state_service_peek();
  int bar_h = 3;

#ifdef PBL_ROUND
  // On round screens, use an arc-style bar near the top
  // Inset heavily to stay within the visible circle
  int inset = round_inset_at_y(8, bounds.size.h, bounds.size.w);
  int bar_w = bounds.size.w - (inset * 2);
  int bar_x = inset;
  int bar_y = 8;
#else
  int bar_w = bounds.size.w - 8;
  int bar_x = 4;
  int bar_y = 1;
#endif

  graphics_context_set_fill_color(ctx, GColorDarkGray);
  graphics_fill_rect(ctx, GRect(bar_x, bar_y, bar_w, bar_h), 1, GCornersAll);

  int fill_w = (charge.charge_percent * bar_w) / 100;
  if (fill_w < 1 && charge.charge_percent > 0) fill_w = 1;

#ifdef PBL_COLOR
  if (charge.is_charging) {
    graphics_context_set_fill_color(ctx, GColorGreen);
  } else if (charge.charge_percent <= 20) {
    graphics_context_set_fill_color(ctx, GColorRed);
  } else {
    graphics_context_set_fill_color(ctx, GColorWhite);
  }
#else
  graphics_context_set_fill_color(ctx, GColorWhite);
#endif
  graphics_fill_rect(ctx, GRect(bar_x, bar_y, fill_w, bar_h), 1, GCornersAll);
}

// ============================================================
// EKG line (speed tied to heart count)
// ============================================================
static void draw_ekg_line(GContext *ctx, GRect bounds, int y_base, int frame) {
#ifdef PBL_ROUND
  int inset = round_inset_at_y(y_base, bounds.size.h, bounds.size.w);
  int x_start = inset;
  int x_end = bounds.size.w - inset;
#else
  int x_start = 4;
  int x_end = bounds.size.w - 4;
#endif
  int speed = get_ekg_speed();
  int scroll = (frame * speed) % EKG_LEN;

#ifdef PBL_COLOR
  graphics_context_set_stroke_color(ctx, PALETTES[s_heart_color_idx].main);
#else
  graphics_context_set_stroke_color(ctx, GColorWhite);
#endif
  graphics_context_set_stroke_width(ctx, 2);

  for (int x = x_start; x < x_end - 1; x++) {
    int idx1 = (x - x_start + scroll) % EKG_LEN;
    int idx2 = (x - x_start + scroll + 1) % EKG_LEN;
    graphics_draw_line(ctx, GPoint(x, y_base + EKG_WAVE[idx1]),
                       GPoint(x + 1, y_base + EKG_WAVE[idx2]));
  }
}

// ============================================================
// Confetti
// ============================================================
static void init_confetti(GRect bounds) {
  int cx = bounds.size.w / 2;
  int cy = bounds.size.h / 2;
  for (int i = 0; i < NUM_CONFETTI; i++) {
    s_confetti[i].x = cx * 10;
    s_confetti[i].y = cy * 10;
    s_confetti[i].vx = (rand() % 60) - 30;
    s_confetti[i].vy = -((rand() % 40) + 15);
#ifdef PBL_COLOR
    GColor colors[] = {
      GColorYellow, GColorCyan, GColorMagenta, GColorGreen,
      GColorRed, GColorOrange, GColorBlueMoon, GColorMintGreen
    };
    s_confetti[i].color = colors[rand() % 8];
#endif
  }
}

static void draw_confetti(GContext *ctx, GRect bounds) {
  if (!s_confetti_active) return;
  for (int i = 0; i < NUM_CONFETTI; i++) {
    int px = s_confetti[i].x / 10;
    int py = s_confetti[i].y / 10;
    if (px < 0 || px >= bounds.size.w || py < 0 || py >= bounds.size.h) continue;
#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, s_confetti[i].color);
#else
    graphics_context_set_fill_color(ctx, GColorWhite);
#endif
    if (i % 3 == 0) {
      graphics_fill_rect(ctx, GRect(px - 1, py - 1, 3, 3), 0, GCornerNone);
    } else {
      graphics_fill_circle(ctx, GPoint(px, py), 1);
    }
  }
}

static void confetti_timer_callback(void *data) {
  for (int i = 0; i < NUM_CONFETTI; i++) {
    s_confetti[i].x += s_confetti[i].vx;
    s_confetti[i].y += s_confetti[i].vy;
    s_confetti[i].vy += 3;
  }
  s_confetti_frame++;
  if (s_confetti_frame >= 25) {
    s_confetti_active = false;
    s_confetti_timer = NULL;
  } else {
    s_confetti_timer = app_timer_register(60, confetti_timer_callback, NULL);
  }
  layer_mark_dirty(s_canvas_layer);
}

// ============================================================
// Loading animation
// ============================================================
static void loading_timer_callback(void *data) {
  s_loading_frame++;
  if (s_loading_frame >= 20 || s_hearts_count >= 0) {
    s_loading_active = false;
    s_loading_timer = NULL;
  } else {
    s_loading_timer = app_timer_register(100, loading_timer_callback, NULL);
  }
  layer_mark_dirty(s_canvas_layer);
}

// ============================================================
// Crown (milestone)
// ============================================================
static void crown_timer_callback(void *data) {
  s_crown_frame++;
  if (s_crown_frame >= 40) {
    s_show_crown = false;
    s_crown_timer = NULL;
  } else {
    s_crown_timer = app_timer_register(100, crown_timer_callback, NULL);
  }
  layer_mark_dirty(s_canvas_layer);
}

static bool is_milestone(int count) {
  if (count <= 0) return false;
  return (count == 1 || count == 5 || count == 10 || count == 25 ||
          count == 50 || count == 100 || count == 250 || count == 500 ||
          count == 1000 || count == 5000 || count == 10000);
}

// ============================================================
// Floating "+N" badge
// ============================================================
static void badge_timer_callback(void *data) {
  s_badge_frame++;
  if (s_badge_frame >= 20) {
    s_badge_active = false;
    s_badge_timer = NULL;
  } else {
    s_badge_timer = app_timer_register(50, badge_timer_callback, NULL);
  }
  layer_mark_dirty(s_canvas_layer);
}

static void draw_badge(GContext *ctx, GRect bounds) {
  if (!s_badge_active || s_badge_delta <= 0) return;

  // Position badge to the right of center, but respect round edges
  int cx = bounds.size.w / 2 + PBL_IF_ROUND_ELSE(bounds.size.w / 6, bounds.size.w / 4);
  int base_y = bounds.size.h / 2;
  int float_y = base_y - (s_badge_frame * 3);  // rise 3px per frame

  // Fade: full opacity for first 12 frames, then fade
  // On Pebble we can't really fade, so just hide after frame 16
  if (s_badge_frame > 16) return;

  char badge_buf[12];
  snprintf(badge_buf, sizeof(badge_buf), "+%d", s_badge_delta);

  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);

#ifdef PBL_COLOR
  // Green for gains
  graphics_context_set_text_color(ctx, GColorGreen);
#else
  graphics_context_set_text_color(ctx, GColorWhite);
#endif

  GRect text_rect = GRect(cx - 30, float_y, 60, 22);
  graphics_draw_text(ctx, badge_buf, font, text_rect,
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);
}

// ============================================================
// Rank display
// ============================================================
static void draw_rank(GContext *ctx, GRect bounds, bool obstructed) {
  if (s_rank <= 0 || obstructed) return;

  char rank_buf[12];
  snprintf(rank_buf, sizeof(rank_buf), "#%d", s_rank);

  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14);

#ifdef PBL_ROUND
  // On round screens, position near bottom-center to avoid corner clip
  int ry = bounds.size.h - 38;
  int inset = round_inset_at_y(ry, bounds.size.h, bounds.size.w);
  // Place right-aligned but within the safe zone
  int rx = bounds.size.w - inset - 4;
  GRect text_rect = GRect(rx - 44, ry, 44, 16);
#else
  int rx = bounds.size.w - 6;
  int ry = bounds.size.h - 30;
  GRect text_rect = GRect(rx - 44, ry, 44, 16);
#endif

#ifdef PBL_COLOR
  graphics_context_set_text_color(ctx, GColorDarkGray);
#else
  graphics_context_set_text_color(ctx, GColorLightGray);
#endif

  graphics_draw_text(ctx, rank_buf, font, text_rect,
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentRight, NULL);
}

// ============================================================
// Canvas update proc
// ============================================================
static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GRect unobs = layer_get_unobstructed_bounds(s_window_layer);
  int avail_h = unobs.size.h;
  bool obstructed = (unobs.size.h < bounds.size.h);

  // Black background
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  // Battery bar
  draw_battery_bar(ctx, bounds);

  // Heart position + size
  int heart_cx = bounds.size.w / 2;
  int heart_size;

#if defined(PBL_ROUND)
  heart_size = 40;  // slightly smaller on round to avoid edge clipping
#elif PBL_DISPLAY_HEIGHT == 228
  heart_size = 52;
#else
  heart_size = 40;
#endif

  int heart_cy = avail_h / 2 + PBL_IF_ROUND_ELSE(8, 10);

  // Pulse scaling
  int draw_size = heart_size;
  if (s_pulse_active) {
    if (s_pulse_frame < 5) {
      draw_size = heart_size + s_pulse_frame * 2;
    } else {
      draw_size = heart_size + (10 - s_pulse_frame) * 2;
    }
  }

  int heart_top_y = heart_cy - draw_size / 3;

  if (!s_bt_connected) {
    // --- DISCONNECTED: Gray cracked heart ---
#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, GColorDarkGray);
    draw_heart_filled(ctx, heart_cx + 2, heart_top_y + 2, draw_size);
    graphics_context_set_fill_color(ctx, GColorLightGray);
    draw_heart_filled(ctx, heart_cx, heart_top_y, draw_size);
#else
    graphics_context_set_fill_color(ctx, GColorWhite);
    draw_heart_filled(ctx, heart_cx, heart_top_y, draw_size);
#endif
    draw_heart_crack(ctx, heart_cx, heart_top_y, draw_size);

  } else if (s_loading_active && s_hearts_count < 0) {
    // --- LOADING: Heart outline filling ---
#ifdef PBL_COLOR
    graphics_context_set_stroke_color(ctx, PALETTES[s_heart_color_idx].main);
#else
    graphics_context_set_stroke_color(ctx, GColorWhite);
#endif
    graphics_context_set_stroke_width(ctx, 2);
    draw_heart_outline(ctx, heart_cx, heart_top_y, draw_size);

    int fill_h = (s_loading_frame * draw_size) / 20;
    if (fill_h > draw_size) fill_h = draw_size;
    int r = draw_size / 3;
    int half = (r * 3) / 2;

#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, PALETTES[s_heart_color_idx].main);
#else
    graphics_context_set_fill_color(ctx, GColorWhite);
#endif

    int bot = heart_top_y + draw_size - r;
    int fill_start = bot - fill_h;
    for (int y = bot; y >= fill_start && y >= heart_top_y - r/3; y--) {
      int dy = y - heart_top_y;
      int w;
      if (dy >= 0) {
        w = half - (dy * half) / (draw_size - r);
      } else {
        w = half - 2;
      }
      if (w < 1) w = 1;
      graphics_draw_line(ctx, GPoint(heart_cx - w, y), GPoint(heart_cx + w, y));
    }

  } else {
    // --- NORMAL: Colored heart ---
#ifdef PBL_COLOR
    graphics_context_set_fill_color(ctx, PALETTES[s_heart_color_idx].dark);
    draw_heart_filled(ctx, heart_cx + 2, heart_top_y + 2, draw_size);

    graphics_context_set_fill_color(ctx, PALETTES[s_heart_color_idx].main);
    draw_heart_filled(ctx, heart_cx, heart_top_y, draw_size);

    graphics_context_set_fill_color(ctx, PALETTES[s_heart_color_idx].highlight);
    int hl_r = draw_size / 8;
    if (hl_r < 2) hl_r = 2;
    graphics_fill_circle(ctx,
      GPoint(heart_cx - draw_size / 5, heart_top_y - draw_size / 8), hl_r);
#else
    graphics_context_set_fill_color(ctx, GColorWhite);
    draw_heart_filled(ctx, heart_cx, heart_top_y, draw_size);
#endif
  }

  // Crown above heart
  if (s_show_crown) {
    int crown_w = PBL_IF_ROUND_ELSE(24, 28);
    int crown_y = heart_top_y - draw_size / 3 - crown_w / 2 - 2;
    if (s_crown_frame < 6) {
      crown_y -= (6 - s_crown_frame);
    }
    draw_crown(ctx, heart_cx, crown_y, crown_w);
  }

  // Floating "+N" badge
  draw_badge(ctx, bounds);

  // Rank in bottom-right
  draw_rank(ctx, bounds, obstructed);

  // EKG at bottom
  if (!obstructed) {
    int ekg_y = avail_h - PBL_IF_ROUND_ELSE(36, 16);
    draw_ekg_line(ctx, bounds, ekg_y, s_frame);
  }

  // Confetti overlay
  draw_confetti(ctx, bounds);
}

// ============================================================
// Layout positioning (Quick View aware)
// ============================================================
static void update_layout(void) {
  GRect bounds = layer_get_bounds(s_window_layer);
  GRect unobs = layer_get_unobstructed_bounds(s_window_layer);
  int avail_h = unobs.size.h;
  int w = bounds.size.w;
  bool obstructed = (unobs.size.h < bounds.size.h);

  int time_y, date_y, hearts_y, label_y;

#if defined(PBL_ROUND)
  time_y = (avail_h > 160) ? 16 : 8;
  date_y = time_y + 48;
  hearts_y = avail_h / 2 + 2;
  label_y = obstructed ? (avail_h - 26) : (avail_h - 34);
#elif PBL_DISPLAY_HEIGHT == 228
  time_y = (avail_h > 200) ? 14 : 6;
  date_y = time_y + 50;
  hearts_y = avail_h / 2 + 4;
  label_y = obstructed ? (avail_h - 22) : (avail_h - 34);
#else
  time_y = (avail_h > 150) ? 6 : 2;
  date_y = time_y + 44;
  hearts_y = avail_h / 2 - 2;
  label_y = obstructed ? (avail_h - 20) : (avail_h - 28);
#endif

  // Set text layer frames with round-screen insets
#ifdef PBL_ROUND
  {
    int time_inset = round_inset_at_y(time_y + 20, bounds.size.h, w);
    int date_inset = round_inset_at_y(date_y + 10, bounds.size.h, w);
    int hearts_inset = round_inset_at_y(hearts_y + 18, bounds.size.h, w);
    int label_inset = round_inset_at_y(label_y + 8, bounds.size.h, w);

    layer_set_frame(text_layer_get_layer(s_time_layer),
                    GRect(time_inset, time_y, w - time_inset * 2, 50));
    layer_set_frame(text_layer_get_layer(s_date_layer),
                    GRect(date_inset, date_y, w - date_inset * 2, 22));
    layer_set_frame(text_layer_get_layer(s_hearts_layer),
                    GRect(hearts_inset, hearts_y, w - hearts_inset * 2, 44));
    layer_set_frame(text_layer_get_layer(s_label_layer),
                    GRect(label_inset, label_y, w - label_inset * 2, 18));
  }
#else
  layer_set_frame(text_layer_get_layer(s_time_layer),
                  GRect(0, time_y, w, 50));
  layer_set_frame(text_layer_get_layer(s_date_layer),
                  GRect(0, date_y, w, 22));
  layer_set_frame(text_layer_get_layer(s_hearts_layer),
                  GRect(0, hearts_y, w, 44));
  layer_set_frame(text_layer_get_layer(s_label_layer),
                  GRect(0, label_y, w, 18));
#endif

  layer_set_hidden(text_layer_get_layer(s_label_layer), obstructed);
}

// ============================================================
// Quick View handlers
// ============================================================
static void prv_unobstructed_will_change(GRect final_area, void *context) {
  (void)final_area; (void)context;
}

static void prv_unobstructed_change(AnimationProgress progress, void *context) {
  (void)progress; (void)context;
  update_layout();
  layer_mark_dirty(s_canvas_layer);
}

static void prv_unobstructed_did_change(void *context) {
  (void)context;
  update_layout();
  layer_mark_dirty(s_canvas_layer);
}

// ============================================================
// Pulse animation
// ============================================================
static void pulse_timer_callback(void *data) {
  s_pulse_frame++;
  if (s_pulse_frame >= 10) {
    s_pulse_active = false;
    s_pulse_timer = NULL;
  } else {
    s_pulse_timer = app_timer_register(50, pulse_timer_callback, NULL);
  }
  layer_mark_dirty(s_canvas_layer);
}

static void start_pulse(void) {
  if (!s_pulse_active) {
    s_pulse_active = true;
    s_pulse_frame = 0;
    s_pulse_timer = app_timer_register(50, pulse_timer_callback, NULL);
    layer_mark_dirty(s_canvas_layer);
  }
}

static void double_pulse_timer(void *data) {
  start_pulse();
}

static void start_double_pulse(void) {
  start_pulse();
  app_timer_register(600, double_pulse_timer, NULL);
}

// ============================================================
// Tap handler — pulse + refresh
// ============================================================
static void tap_handler(AccelAxisType axis, int32_t direction) {
  start_pulse();
  DictionaryIterator *iter;
  AppMessageResult result = app_message_outbox_begin(&iter);
  if (result == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_HEARTS, 1);
    app_message_outbox_send();
  }
}

// ============================================================
// Time
// ============================================================
static void update_time(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  if (clock_is_24h_style()) {
    strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", t);
  } else {
    strftime(s_time_buf, sizeof(s_time_buf), "%I:%M", t);
    if (s_time_buf[0] == '0') {
      memmove(s_time_buf, s_time_buf + 1, strlen(s_time_buf));
    }
  }
  text_layer_set_text(s_time_layer, s_time_buf);

  strftime(s_date_buf, sizeof(s_date_buf), "%a %b %d", t);
  for (int i = 0; s_date_buf[i]; i++) {
    if (s_date_buf[i] >= 'a' && s_date_buf[i] <= 'z') {
      s_date_buf[i] -= 32;
    }
  }
  text_layer_set_text(s_date_layer, s_date_buf);
}

// ============================================================
// Hearts display
// ============================================================
static void update_hearts_display(void) {
  if (s_hearts_count >= 0) {
    snprintf(s_hearts_buf, sizeof(s_hearts_buf), "%d", s_hearts_count);
  } else {
    s_hearts_buf[0] = '\0';
  }
  text_layer_set_text(s_hearts_layer, s_hearts_buf);
}

// ============================================================
// Tick handler
// ============================================================
static void tick_handler(struct tm *tick_time, TimeUnits units) {
  update_time();
  s_frame++;
  if (s_frame > 10000) s_frame = 0;
  layer_mark_dirty(s_canvas_layer);

  if (tick_time->tm_min % 30 == 0) {
    DictionaryIterator *iter;
    AppMessageResult result = app_message_outbox_begin(&iter);
    if (result == APP_MSG_OK) {
      dict_write_uint8(iter, MESSAGE_KEY_REQUEST_HEARTS, 1);
      app_message_outbox_send();
    }
  }
}

// ============================================================
// AppMessage
// ============================================================
static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  // Heart count
  Tuple *hearts_tuple = dict_find(iterator, MESSAGE_KEY_HEARTS);
  if (hearts_tuple) {
    int new_count = hearts_tuple->value->int32;

    // Detect increase → confetti + floating badge
    if (s_hearts_count >= 0 && new_count > s_hearts_count) {
      int delta = new_count - s_hearts_count;

      // Confetti
      GRect bounds = layer_get_bounds(s_window_layer);
      init_confetti(bounds);
      s_confetti_active = true;
      s_confetti_frame = 0;
      if (s_confetti_timer) app_timer_cancel(s_confetti_timer);
      s_confetti_timer = app_timer_register(60, confetti_timer_callback, NULL);
      start_double_pulse();

      // Floating "+N" badge
      s_badge_active = true;
      s_badge_frame = 0;
      s_badge_delta = delta;
      if (s_badge_timer) app_timer_cancel(s_badge_timer);
      s_badge_timer = app_timer_register(50, badge_timer_callback, NULL);
    }

    // Milestone
    if (new_count != s_hearts_count && is_milestone(new_count)) {
      s_show_crown = true;
      s_crown_frame = 0;
      if (s_crown_timer) app_timer_cancel(s_crown_timer);
      s_crown_timer = app_timer_register(100, crown_timer_callback, NULL);
      vibes_long_pulse();
    }

    s_prev_hearts = s_hearts_count;
    s_hearts_count = new_count;
    s_loading_active = false;
    persist_write_int(PERSIST_KEY_HEARTS, s_hearts_count);
    update_hearts_display();
    layer_mark_dirty(s_canvas_layer);

    // Pulse on same-count update
    if (s_prev_hearts == s_hearts_count || s_prev_hearts < 0) {
      if (!s_pulse_active && s_hearts_count > 0) {
        start_pulse();
      }
    }
  }

  // Rank
  Tuple *rank_tuple = dict_find(iterator, MESSAGE_KEY_RANK);
  if (rank_tuple) {
    s_rank = rank_tuple->value->int32;
    persist_write_int(PERSIST_KEY_RANK, s_rank);
    layer_mark_dirty(s_canvas_layer);
  }

  // Heart color
  Tuple *color_tuple = dict_find(iterator, MESSAGE_KEY_HEART_COLOR);
  if (color_tuple) {
    s_heart_color_idx = color_tuple->value->int32;
#ifdef PBL_COLOR
    if (s_heart_color_idx < 0 || s_heart_color_idx >= NUM_PALETTES) {
      s_heart_color_idx = 0;
    }
#endif
    persist_write_int(PERSIST_KEY_COLOR, s_heart_color_idx);
    layer_mark_dirty(s_canvas_layer);
  }
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Message dropped: %d", reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator,
                                    AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: %d", reason);
}

// ============================================================
// Bluetooth
// ============================================================
static void bluetooth_callback(bool connected) {
  s_bt_connected = connected;
  if (!connected) {
    vibes_double_pulse();
  }
  layer_mark_dirty(s_canvas_layer);
}

// ============================================================
// Window
// ============================================================
static void window_load(Window *window) {
  s_window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(s_window_layer);

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(s_window_layer, s_canvas_layer);

  GFont time_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
  GFont date_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont hearts_font = fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS);
  GFont label_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  s_time_layer = text_layer_create(GRect(0, 0, bounds.size.w, 50));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, GColorWhite);
  text_layer_set_font(s_time_layer, time_font);
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);
  layer_add_child(s_window_layer, text_layer_get_layer(s_time_layer));

  s_date_layer = text_layer_create(GRect(0, 0, bounds.size.w, 22));
  text_layer_set_background_color(s_date_layer, GColorClear);
#ifdef PBL_COLOR
  text_layer_set_text_color(s_date_layer, GColorLightGray);
#else
  text_layer_set_text_color(s_date_layer, GColorWhite);
#endif
  text_layer_set_font(s_date_layer, date_font);
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentCenter);
  layer_add_child(s_window_layer, text_layer_get_layer(s_date_layer));

  s_hearts_layer = text_layer_create(GRect(0, 0, bounds.size.w, 44));
  text_layer_set_background_color(s_hearts_layer, GColorClear);
#ifdef PBL_COLOR
  text_layer_set_text_color(s_hearts_layer, GColorWhite);
#else
  text_layer_set_text_color(s_hearts_layer, GColorBlack);
#endif
  text_layer_set_font(s_hearts_layer, hearts_font);
  text_layer_set_text_alignment(s_hearts_layer, GTextAlignmentCenter);
  layer_add_child(s_window_layer, text_layer_get_layer(s_hearts_layer));

  s_label_layer = text_layer_create(GRect(0, 0, bounds.size.w, 18));
  text_layer_set_background_color(s_label_layer, GColorClear);
#ifdef PBL_COLOR
  text_layer_set_text_color(s_label_layer, GColorDarkGray);
#else
  text_layer_set_text_color(s_label_layer, GColorLightGray);
#endif
  text_layer_set_font(s_label_layer, label_font);
  text_layer_set_text_alignment(s_label_layer, GTextAlignmentCenter);
  text_layer_set_text(s_label_layer, "HEARTS");
  layer_add_child(s_window_layer, text_layer_get_layer(s_label_layer));

  // Load persisted state
  if (persist_exists(PERSIST_KEY_HEARTS)) {
    s_hearts_count = persist_read_int(PERSIST_KEY_HEARTS);
    s_loading_active = false;
  }
  if (persist_exists(PERSIST_KEY_COLOR)) {
    s_heart_color_idx = persist_read_int(PERSIST_KEY_COLOR);
#ifdef PBL_COLOR
    if (s_heart_color_idx < 0 || s_heart_color_idx >= NUM_PALETTES) {
      s_heart_color_idx = 0;
    }
#endif
  }
  if (persist_exists(PERSIST_KEY_RANK)) {
    s_rank = persist_read_int(PERSIST_KEY_RANK);
  }

  s_bt_connected = connection_service_peek_pebble_app_connection();

  update_time();
  update_hearts_display();

  if (s_loading_active) {
    s_loading_timer = app_timer_register(100, loading_timer_callback, NULL);
  }

  prv_unobstructed_change(0, NULL);
  prv_unobstructed_did_change(NULL);

  UnobstructedAreaHandlers handlers = {
    .will_change = prv_unobstructed_will_change,
    .change = prv_unobstructed_change,
    .did_change = prv_unobstructed_did_change
  };
  unobstructed_area_service_subscribe(handlers, NULL);
}

static void window_unload(Window *window) {
  unobstructed_area_service_unsubscribe();
  text_layer_destroy(s_time_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_hearts_layer);
  text_layer_destroy(s_label_layer);
  layer_destroy(s_canvas_layer);
  if (s_pulse_timer) { app_timer_cancel(s_pulse_timer); s_pulse_timer = NULL; }
  if (s_confetti_timer) { app_timer_cancel(s_confetti_timer); s_confetti_timer = NULL; }
  if (s_loading_timer) { app_timer_cancel(s_loading_timer); s_loading_timer = NULL; }
  if (s_crown_timer) { app_timer_cancel(s_crown_timer); s_crown_timer = NULL; }
  if (s_badge_timer) { app_timer_cancel(s_badge_timer); s_badge_timer = NULL; }
}

// ============================================================
// Init / Deinit
// ============================================================
static void init(void) {
  srand(time(NULL));

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload,
  });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_open(512, 64);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  accel_tap_service_subscribe(tap_handler);
  connection_service_subscribe((ConnectionHandlers) {
    .pebble_app_connection_handler = bluetooth_callback
  });
}

static void deinit(void) {
  connection_service_unsubscribe();
  accel_tap_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
