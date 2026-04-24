#include "ui.h"
#include "app_config.h"
#include "art_decoder.h"
#include "display.h"
#include "encoder.h"
#include "fonts.h"
#include "haptic.h"
#include "settings.h"
#include "sonos.h"
#include "ui/images/images.h"
#include "ui/ui_timer.h"
#include "ui_pages.h"

#include "esp_heap_caps.h"

#include "esp_log.h"
#include "lvgl.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

static constexpr const char *TAG = "ui";

// ─── Interaction Model
// ────────────────────────────────────────────────────────────────────
//
//  VOLUME mode (default):
//    encoder turn  → volume arc brightens + number appears
//    screen tap    → enter BROWSE mode
//
//  BROWSE mode:
//    encoder turn  → cycle through stations (wraps)
//    screen tap    → select station, start streaming, → VOLUME
//    7s inactivity → cancel, revert, → VOLUME
//

enum class Mode { Volume, Browse };

static constexpr int BROWSE_TIMEOUT_MS = 15000;
static constexpr int VOL_DISPLAY_MS = 1500;
static constexpr int VOL_LOCAL_GRACE_MS = 2000;
static constexpr int ENCODER_POLL_MS = 20;
static constexpr int ANIM_FADE_MS = 200;
static constexpr int ANIM_QUICK_MS = 100;
static constexpr int ANIM_ARC_FADE_MS = 400;
static constexpr int ANIM_BG_FADE_MS = 250;
static constexpr int ANIM_BG_BROWSE_MS = 250;

static constexpr uint8_t BACKLIGHT_NORMAL = 80;
static constexpr uint8_t BACKLIGHT_DIM = 8;
static constexpr int BACKLIGHT_FADE_STEP_MS = 30;
static constexpr int BACKLIGHT_INACTIVITY_MS = 15000;
static constexpr int ART_BUF_SIZE = 256 * 1024;
static constexpr int EXTERNAL_IDLE_TIMEOUT_MS = 300000; // 5 minutes

// ─── Palette
// ────────────────────────────────────────────────────────────────────────────

#define COL_BG lv_color_hex(0x000000)
#define COL_TEXT lv_color_hex(0xFFFFFF)
#define COL_TEXT_SEC lv_color_hex(0x8E8E93)
#define COL_ACCENT lv_color_hex(0x0A84FF)
#define COL_ARC_BG lv_color_hex(0x1C1C1E)
#define COL_ARC_ACTIVE lv_color_hex(0xFFFFFF)
#define COL_ARC_DIM lv_color_hex(0x555555)
#define COL_GREEN lv_color_hex(0x30D158)
#define COL_ORANGE lv_color_hex(0xFF9F0A)
#define COL_RED lv_color_hex(0xFF453A)
#define COL_BROWSE_BG lv_color_hex(0x0A0A0A)

// ─── State
// ──────────────────────────────────────────────────────────────────────────────

static Mode s_mode = Mode::Volume;
static int s_volume;
static int s_station_index;
static int s_browse_index;
static PlayState s_play_state = PlayState::Stopped;
static bool s_was_playing;
static bool s_idle_active = false;
static bool s_weather_valid = false;
static bool s_external_playing = false;
static bool s_browse_has_external = false;
static MediaInfo s_media = {};
static volatile bool s_art_fetching = false;
static uint32_t s_track_changed_ms;

// ─── Widgets
// ────────────────────────────────────────────────────────────────────────────

static lv_obj_t *s_screen;
static lv_obj_t *s_home;

// Status bar (top)
static lv_obj_t *s_wifi_dot;

// Background (two solid color layers for tear-free crossfade)
static lv_obj_t *s_bg_back; // Bottom layer: current station color, fully opaque
static lv_obj_t *s_bg_front; // Top layer: fades in with new station color
static lv_obj_t *s_bg_dim;   // Black overlay for edge darkening

// Artwork area (center)
static lv_obj_t *s_img_logo;

// Info area (below artwork)
static lv_obj_t *s_lbl_station;
static lv_obj_t *s_lbl_subtitle;

// Browse mode extras
static lv_obj_t *s_lbl_position;

// Clock (idle state)
static lv_obj_t *s_lbl_clock;
static lv_timer_t *s_clock_timer;

// Weather (idle state)
static lv_obj_t *s_lbl_weather_date;
static lv_obj_t *s_lbl_weather_icon;
static lv_obj_t *s_lbl_weather_temp;
static char s_weather_date[16];
static char s_weather_icon[8];
static char s_weather_temp[16];
static uint32_t s_weather_color;

// Forecast strip (idle state)
static constexpr int FORECAST_COLS = 4;
static lv_obj_t *s_forecast_container;
static lv_obj_t *s_fc_hour[FORECAST_COLS];
static lv_obj_t *s_fc_icon[FORECAST_COLS];
static lv_obj_t *s_fc_temp[FORECAST_COLS];
static int s_forecast_count;

// Speaker (bottom)
static lv_obj_t *s_lbl_speaker;

// Artwork container (rounded card with shadow)
static lv_obj_t *s_logo_container;

// Prev/Next buttons (external media only)
static lv_obj_t *s_btn_prev;
static lv_obj_t *s_btn_next;

// Volume arc (inside home page)
static lv_obj_t *s_vol_arc;
static lv_timer_t *s_vol_hide_timer;
static int32_t s_arc_display_val; // Current animated arc position
static uint32_t s_local_vol_ms;   // Timestamp of last local volume change

// Browse timeout
static lv_timer_t *s_browse_timer;

// External media idle timeout (revert to radio after inactivity)
static lv_timer_t *s_external_idle_timer;

// Touch press detection (manual long-press via timer)
static lv_timer_t *s_press_timer;
static bool s_press_was_long;
static lv_point_t s_press_point;

// Backlight dimming
static lv_timer_t *s_bl_timer;
static lv_timer_t *s_bl_inactivity_timer;
static uint8_t s_bl_current = BACKLIGHT_NORMAL;
static uint8_t s_bl_target = BACKLIGHT_NORMAL;

// Album art (external media)
static uint8_t *s_art_jpeg;      // Raw JPEG download buffer (PSRAM)
static uint8_t *s_art_pixels;    // Decoded RGB565 pixels (PSRAM)
static lv_image_dsc_t s_art_dsc; // LVGL image descriptor for decoded art
static lv_obj_t *s_art_loading;
static char s_art_last_url[256]; // Dedup — skip re-download if URL unchanged

// Speaker picker
static lv_obj_t *s_scr_picker;
static lv_obj_t *s_picker_spinner;
static lv_obj_t *s_picker_list;
static DiscoveryResult s_discovered;
static int s_speaker_highlight;
static bool s_on_picker;
static char s_current_speaker[64];

// ─── Station Images (compiled into firmware as C arrays)
// ────────────────────────────────────────────────────────

static const lv_image_dsc_t *const s_logos[STATION_COUNT] = {
    &nrk_p1, &nrk_p2, &nrk_p3, &nrk_mp3,    &nrk_jazz,    &nrk_nyheter,
    &p4,     &p5,     &p9,     &radio_rock, &radio_norge, &nrj,
};

static void set_logo(int index) {
  if (index < 0 || index >= STATION_COUNT)
    return;
  lv_image_set_src(s_img_logo, s_logos[index]);
}

// ─── Background Crossfade (two-layer solid color opacity fade — tear-free)
// ─────────────────────────────────────────────────────
//
// Two full-screen solid color layers stacked. Transition: front layer gets the
// new station color and fades from transparent → opaque. Filling a solid rect
// is near-zero CPU cost — no image blending. Per-frame opacity delta is tiny,
// making any mid-flush DMA split invisible even with the 36-row buffer.

static void anim_bg_crossfade_cb(void *obj, int32_t v) {
  lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(obj), v, LV_PART_MAIN);
}

static void anim_bg_crossfade_done(lv_anim_t *a) {
  // Front is now fully opaque — copy its color to back and reset front.
  lv_color_t c = lv_obj_get_style_bg_color(s_bg_front, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_bg_back, c, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_bg_front, LV_OPA_TRANSP, LV_PART_MAIN);
  if (s_logo_container)
    lv_obj_set_style_bg_color(s_logo_container, c, LV_PART_MAIN);
}

static void set_bg(int index, bool animate) {
  if (index < 0 || index >= STATION_COUNT)
    return;

  lv_color_t target = lv_color_hex(STATIONS[index].color);

  // If a crossfade is in progress, finish it immediately
  lv_anim_delete(s_bg_front, anim_bg_crossfade_cb);
  int32_t front_opa = lv_obj_get_style_bg_opa(s_bg_front, LV_PART_MAIN);
  if (front_opa > LV_OPA_TRANSP) {
    // Collapse mid-fade: promote front to back
    lv_color_t fc = lv_obj_get_style_bg_color(s_bg_front, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_bg_back, fc, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bg_front, LV_OPA_TRANSP, LV_PART_MAIN);
  }

  // Already showing this color?
  lv_color_t current = lv_obj_get_style_bg_color(s_bg_back, LV_PART_MAIN);
  if (current.red == target.red && current.green == target.green &&
      current.blue == target.blue)
    return;

  if (!animate) {
    lv_obj_set_style_bg_color(s_bg_back, target, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_bg_front, LV_OPA_TRANSP, LV_PART_MAIN);
    if (s_logo_container)
      lv_obj_set_style_bg_color(s_logo_container, target, LV_PART_MAIN);
    return;
  }

  // Set front to new color, crossfade its opacity
  lv_obj_set_style_bg_color(s_bg_front, target, LV_PART_MAIN);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_bg_front);
  lv_anim_set_exec_cb(&a, anim_bg_crossfade_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, ANIM_BG_FADE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_completed_cb(&a, anim_bg_crossfade_done);
  lv_anim_start(&a);
}

// ─── Forward Declarations
// ─────────────────────────────────────────────────────────────

static void enter_browse();
static void exit_browse();
static void confirm_browse();
static void update_subtitle();
static void update_clock();
static bool home_should_idle();
static void show_idle_ui(bool idle);
static void do_tap();
static void do_long_press();
static void on_page_changed(int index, const char *id);
static void on_encoder_poll(lv_timer_t *);
static void on_prev_tap(lv_event_t *);
static void on_next_tap(lv_event_t *);
static void select_speaker(int index);

// ─── Animation Helpers
// ────────────────────────────────────────────────────────────────

static void anim_opa_cb(void *obj, int32_t v) {
  lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), v, LV_PART_MAIN);
}

static void anim_img_opa_cb(void *obj, int32_t v) {
  lv_obj_set_style_image_opa(static_cast<lv_obj_t *>(obj), v, LV_PART_MAIN);
}

static void anim_arc_ind_opa_cb(void *obj, int32_t v) {
  lv_obj_set_style_arc_opa(static_cast<lv_obj_t *>(obj), v, LV_PART_INDICATOR);
}

static void anim_vol_arc_cb(void *obj, int32_t v) {
  s_arc_display_val = v;
  lv_arc_set_value(static_cast<lv_obj_t *>(obj), v);
}

static void anim_bg_opa_cb(void *obj, int32_t v) {
  lv_obj_set_style_bg_opa(static_cast<lv_obj_t *>(obj), v, LV_PART_MAIN);
}

static void anim_hide_done(lv_anim_t *a) {
  lv_obj_add_flag(static_cast<lv_obj_t *>(a->var), LV_OBJ_FLAG_HIDDEN);
}

static void anim_fade(lv_obj_t *obj, lv_anim_exec_xcb_t exec_cb, int32_t start,
                      int32_t end, int duration,
                      void (*done_cb)(lv_anim_t *) = nullptr) {
  // Skip animation — apply final value immediately
  if (duration <= 0) {
    exec_cb(obj, end);
    if (done_cb) {
      lv_anim_t dummy = {};
      lv_anim_set_var(&dummy, obj);
      done_cb(&dummy);
    }
    return;
  }

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, obj);
  lv_anim_set_exec_cb(&a, exec_cb);
  lv_anim_set_values(&a, start, end);
  lv_anim_set_duration(&a, duration);
  lv_anim_set_path_cb(&a, duration <= 150 ? lv_anim_path_linear
                                          : lv_anim_path_ease_in_out);
  if (done_cb)
    lv_anim_set_completed_cb(&a, done_cb);
  lv_anim_start(&a);
}

// ─── Volume Arc
// ─────────────────────────────────────────────────────────────────────

// ─── Backlight Fade
// ─────────────────────────────────────────────────────────────────────

static void on_backlight_step(lv_timer_t *) {
  if (s_bl_current == s_bl_target) {
    lv_timer_pause(s_bl_timer);
    return;
  }
  if (s_bl_current < s_bl_target)
    s_bl_current = std::min<uint8_t>(s_bl_current + 3, s_bl_target);
  else
    s_bl_current = (s_bl_current > s_bl_target + 3)
                       ? static_cast<uint8_t>(s_bl_current - 3)
                       : s_bl_target;
  display_set_backlight(s_bl_current);
}

static void backlight_fade_to(uint8_t target) {
  s_bl_target = target;
  if (s_bl_current == target)
    return;
  lv_timer_reset(s_bl_timer);
  lv_timer_resume(s_bl_timer);
}

static void on_bl_inactivity(lv_timer_t *) {
  lv_timer_pause(s_bl_inactivity_timer);
  if (!s_idle_active)
    backlight_fade_to(BACKLIGHT_DIM);
}

static void backlight_poke() {
  backlight_fade_to(BACKLIGHT_NORMAL);
  if (s_bl_inactivity_timer) {
    lv_timer_reset(s_bl_inactivity_timer);
    lv_timer_resume(s_bl_inactivity_timer);
  }
}

static void on_vol_hide(lv_timer_t *) {
  anim_fade(s_vol_arc, anim_arc_ind_opa_cb, LV_OPA_COVER, LV_OPA_30,
            ANIM_ARC_FADE_MS);
  lv_timer_pause(s_vol_hide_timer);
  if (s_idle_active)
    backlight_fade_to(BACKLIGHT_DIM);
}

// ─── Browse rotation callback (deferred image load)
// ────────────────────────────────

static void on_browse_rotate_fade_done(lv_anim_t *) {
  if (s_browse_index < 0 && s_art_pixels) {
    lv_image_set_src(s_img_logo, &s_art_dsc);
    lv_image_set_inner_align(s_img_logo, LV_IMAGE_ALIGN_STRETCH);
  } else if (s_browse_index >= 0) {
    set_logo(s_browse_index);
  }
  anim_fade(s_logo_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_70,
            ANIM_QUICK_MS);
}

// ─── Clock
// ────────────────────────────────────────────────────────────────────────────

static void update_clock() {
  time_t now = time(nullptr);
  struct tm *t = localtime(&now);
  char buf[6];
  snprintf(buf, sizeof(buf), "%02d:%02d", t->tm_hour, t->tm_min);
  lv_label_set_text(s_lbl_clock, buf);
}

static void on_clock_tick(lv_timer_t *) { update_clock(); }

static bool home_should_idle() {
  return s_play_state == PlayState::Stopped && s_mode == Mode::Volume;
}

static void show_idle_ui(bool idle) {
  if (!pages_is_home())
    return;
  if (idle == s_idle_active)
    return;
  s_idle_active = idle;

  if (idle) {
    backlight_fade_to(BACKLIGHT_DIM);
    lv_timer_pause(s_bl_inactivity_timer);
  } else {
    backlight_poke();
  }

  lv_anim_delete(s_lbl_clock, anim_opa_cb);
  lv_anim_delete(s_lbl_weather_date, anim_opa_cb);
  lv_anim_delete(s_lbl_weather_icon, anim_opa_cb);
  lv_anim_delete(s_lbl_weather_temp, anim_opa_cb);
  lv_anim_delete(s_forecast_container, anim_opa_cb);
  lv_anim_delete(s_logo_container, anim_opa_cb);
  lv_anim_delete(s_lbl_station, anim_opa_cb);
  lv_anim_delete(s_lbl_speaker, anim_opa_cb);
  lv_anim_delete(s_lbl_subtitle, anim_opa_cb);
  lv_anim_delete(s_bg_front, anim_bg_crossfade_cb);
  lv_anim_delete(s_bg_dim, anim_bg_opa_cb);

  if (idle) {
    update_clock();

    lv_obj_remove_flag(s_lbl_clock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_lbl_clock, LV_OPA_TRANSP, LV_PART_MAIN);
    anim_fade(s_lbl_clock, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_90, ANIM_FADE_MS);

    int32_t logo_opa = lv_obj_get_style_opa(s_logo_container, LV_PART_MAIN);
    anim_fade(s_logo_container, anim_opa_cb, logo_opa, LV_OPA_TRANSP,
              ANIM_FADE_MS, anim_hide_done);
    anim_fade(s_lbl_station, anim_opa_cb, LV_OPA_COVER, LV_OPA_TRANSP,
              ANIM_FADE_MS, anim_hide_done);
    anim_fade(s_lbl_speaker, anim_opa_cb, LV_OPA_60, LV_OPA_TRANSP,
              ANIM_FADE_MS, anim_hide_done);

    anim_fade(s_bg_dim, anim_bg_opa_cb, LV_OPA_TRANSP, LV_OPA_70, ANIM_FADE_MS);

    if (s_weather_valid) {
      lv_label_set_text(s_lbl_weather_date, s_weather_date);
      lv_label_set_text(s_lbl_weather_icon, s_weather_icon);
      lv_obj_set_style_text_color(s_lbl_weather_icon,
                                  lv_color_hex(s_weather_color), LV_PART_MAIN);
      lv_label_set_text(s_lbl_weather_temp, s_weather_temp);

      // Center date + icon + temp as a group
      lv_obj_update_layout(s_lbl_weather_date);
      lv_obj_update_layout(s_lbl_weather_temp);
      int32_t dw = lv_obj_get_width(s_lbl_weather_date);
      int32_t tw = lv_obj_get_width(s_lbl_weather_temp);
      int32_t total = dw + 8 + 20 + 4 + tw; // date + gap + icon + gap + temp
      int32_t x = -(total / 2);
      lv_obj_align(s_lbl_weather_date, LV_ALIGN_CENTER, x + dw / 2, 10);
      x += dw + 8;
      lv_obj_align(s_lbl_weather_icon, LV_ALIGN_CENTER, x + 10, 10);
      x += 20 + 4;
      lv_obj_align(s_lbl_weather_temp, LV_ALIGN_CENTER, x + tw / 2, 10);

      lv_obj_remove_flag(s_lbl_weather_date, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s_lbl_weather_icon, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s_lbl_weather_temp, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_opa(s_lbl_weather_date, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_opa(s_lbl_weather_icon, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_opa(s_lbl_weather_temp, LV_OPA_TRANSP, LV_PART_MAIN);
      anim_fade(s_lbl_weather_date, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                ANIM_FADE_MS);
      anim_fade(s_lbl_weather_icon, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                ANIM_FADE_MS);
      anim_fade(s_lbl_weather_temp, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                ANIM_FADE_MS);

      // Show forecast strip
      if (s_forecast_count > 0) {
        lv_obj_remove_flag(s_forecast_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_opa(s_forecast_container, LV_OPA_TRANSP, LV_PART_MAIN);
        anim_fade(s_forecast_container, anim_opa_cb, LV_OPA_TRANSP,
                  LV_OPA_COVER, ANIM_FADE_MS);
      }

      lv_obj_set_style_opa(s_lbl_subtitle, LV_OPA_TRANSP, LV_PART_MAIN);
    } else {
      lv_obj_set_style_opa(s_lbl_subtitle, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_align(s_lbl_subtitle, LV_ALIGN_CENTER, 0, 30);
      anim_fade(s_lbl_subtitle, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                ANIM_FADE_MS);
    }

    lv_timer_resume(s_clock_timer);
  } else {
    anim_fade(s_lbl_clock, anim_opa_cb,
              lv_obj_get_style_opa(s_lbl_clock, LV_PART_MAIN), LV_OPA_TRANSP,
              ANIM_QUICK_MS, anim_hide_done);

    if (!lv_obj_has_flag(s_lbl_weather_icon, LV_OBJ_FLAG_HIDDEN)) {
      anim_fade(s_lbl_weather_date, anim_opa_cb,
                lv_obj_get_style_opa(s_lbl_weather_date, LV_PART_MAIN),
                LV_OPA_TRANSP, ANIM_QUICK_MS, anim_hide_done);
      anim_fade(s_lbl_weather_icon, anim_opa_cb,
                lv_obj_get_style_opa(s_lbl_weather_icon, LV_PART_MAIN),
                LV_OPA_TRANSP, ANIM_QUICK_MS, anim_hide_done);
      anim_fade(s_lbl_weather_temp, anim_opa_cb,
                lv_obj_get_style_opa(s_lbl_weather_temp, LV_PART_MAIN),
                LV_OPA_TRANSP, ANIM_QUICK_MS, anim_hide_done);
      anim_fade(s_forecast_container, anim_opa_cb,
                lv_obj_get_style_opa(s_forecast_container, LV_PART_MAIN),
                LV_OPA_TRANSP, ANIM_QUICK_MS, anim_hide_done);
    }

    if (!s_external_playing) {
      lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_opa(s_logo_container, LV_OPA_TRANSP, LV_PART_MAIN);
      anim_fade(s_logo_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                ANIM_FADE_MS);
    }
    lv_obj_remove_flag(s_lbl_station, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_lbl_speaker, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_opa(s_lbl_station, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(s_lbl_speaker, LV_OPA_TRANSP, LV_PART_MAIN);

    anim_fade(s_lbl_station, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
              ANIM_FADE_MS);
    anim_fade(s_lbl_speaker, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_60,
              ANIM_FADE_MS);

    anim_fade(s_bg_dim, anim_bg_opa_cb,
              lv_obj_get_style_bg_opa(s_bg_dim, LV_PART_MAIN), LV_OPA_TRANSP,
              ANIM_FADE_MS);

    lv_obj_set_style_opa(s_lbl_subtitle, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_align(s_lbl_subtitle, LV_ALIGN_CENTER, 0, 94);
    anim_fade(s_lbl_subtitle, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
              ANIM_FADE_MS);

    lv_timer_pause(s_clock_timer);
  }
}

static void show_volume(int level) {
  backlight_poke();
  lv_anim_delete(s_vol_arc, anim_arc_ind_opa_cb);
  lv_obj_set_style_arc_opa(s_vol_arc, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_ACTIVE, LV_PART_INDICATOR);

  lv_anim_delete(s_vol_arc, anim_vol_arc_cb);
  s_arc_display_val = level;
  lv_arc_set_value(s_vol_arc, level);

  lv_timer_reset(s_vol_hide_timer);
  lv_timer_resume(s_vol_hide_timer);
}

// ─── Mode Switching
// ─────────────────────────────────────────────────────────────────

static int browse_total() {
  return STATION_COUNT + (s_browse_has_external ? 1 : 0);
}

static int browse_display_pos() {
  return s_browse_index + 1 + (s_browse_has_external ? 1 : 0);
}

static void browse_update_position() {
  char pos[24];
  snprintf(pos, sizeof(pos), "%d / %d", browse_display_pos(), browse_total());
  lv_label_set_text(s_lbl_position, pos);
}

static void enter_browse() {
  s_mode = Mode::Browse;
  if (s_btn_prev && s_btn_next) {
    lv_obj_add_flag(s_btn_prev, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
  }
  s_was_playing = (s_play_state == PlayState::Playing);
  s_browse_has_external = s_external_playing;
  s_browse_index = s_browse_has_external ? -1 : s_station_index;

  lv_obj_set_style_text_color(s_lbl_station, COL_TEXT, LV_PART_MAIN);

  if (s_was_playing || s_browse_has_external) {
    lv_anim_delete(s_logo_container, anim_opa_cb);
    anim_fade(s_logo_container, anim_opa_cb,
              lv_obj_get_style_opa(s_logo_container, LV_PART_MAIN), LV_OPA_70,
              ANIM_FADE_MS);
  } else {
    show_idle_ui(false);
    lv_anim_delete(s_logo_container, anim_opa_cb);
    anim_fade(s_logo_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_70,
              ANIM_FADE_MS);
  }

  if (s_browse_index < 0) {
    lv_label_set_text(s_lbl_subtitle,
                      s_media.source[0] ? s_media.source : "Now playing");
  } else {
    lv_label_set_text(s_lbl_subtitle, "Tap to play");
  }
  lv_obj_set_style_text_color(s_lbl_subtitle, lv_color_hex(0xBBBBBB),
                              LV_PART_MAIN);
  lv_anim_delete(s_lbl_subtitle, anim_opa_cb);
  lv_obj_set_style_opa(s_lbl_subtitle, LV_OPA_TRANSP, LV_PART_MAIN);
  anim_fade(s_lbl_subtitle, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
            ANIM_QUICK_MS);

  browse_update_position();
  lv_obj_remove_flag(s_lbl_position, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(s_lbl_position, LV_OPA_TRANSP, LV_PART_MAIN);
  anim_fade(s_lbl_position, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
            ANIM_QUICK_MS);

  lv_anim_delete(s_vol_arc, anim_vol_arc_cb);
  s_arc_display_val = s_volume;

  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_DIM, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_BG, LV_PART_MAIN);
  lv_timer_pause(s_vol_hide_timer);

  lv_timer_reset(s_browse_timer);
  lv_timer_resume(s_browse_timer);
}

// Called after fade-out completes during browse exit — swaps logo while hidden
static void on_exit_browse_fade_done(lv_anim_t *a) {
  if (s_external_playing && s_art_pixels) {
    lv_image_set_src(s_img_logo, &s_art_dsc);
    lv_image_set_inner_align(s_img_logo, LV_IMAGE_ALIGN_STRETCH);
  } else {
    set_logo(s_station_index);
  }
  anim_fade(s_logo_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
            ANIM_FADE_MS);
}

static void exit_browse() {
  s_mode = Mode::Volume;
  if (s_external_playing && s_btn_prev && s_btn_next) {
    lv_obj_remove_flag(s_btn_prev, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
  }

  lv_obj_set_style_text_color(s_lbl_station, COL_TEXT, LV_PART_MAIN);

  if (s_external_playing) {
    lv_label_set_text(s_lbl_station,
                      s_media.title[0] ? s_media.title : "Unknown");
  } else {
    lv_label_set_text(s_lbl_station, STATIONS[s_station_index].name);
  }
  update_subtitle();

  set_bg(s_station_index, true);

  bool needs_image_swap = (s_browse_index != s_station_index);

  if (needs_image_swap) {
    // Fade out logo, swap image while hidden, fade back in
    lv_anim_delete(s_logo_container, anim_opa_cb);
    anim_fade(s_logo_container, anim_opa_cb,
              lv_obj_get_style_opa(s_logo_container, LV_PART_MAIN),
              LV_OPA_TRANSP, ANIM_QUICK_MS, on_exit_browse_fade_done);
  } else {
    // Same station — just restore directly
    set_logo(s_station_index);
  }

  if (s_was_playing) {
    if (!needs_image_swap) {
      lv_anim_delete(s_logo_container, anim_opa_cb);
      anim_fade(s_logo_container, anim_opa_cb, LV_OPA_70, LV_OPA_COVER,
                ANIM_FADE_MS);
    }

    lv_anim_delete(s_lbl_subtitle, anim_opa_cb);
    lv_obj_set_style_opa(s_lbl_subtitle, LV_OPA_TRANSP, LV_PART_MAIN);
    anim_fade(s_lbl_subtitle, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
              ANIM_QUICK_MS);
  } else {
    show_idle_ui(true);
  }

  lv_obj_set_style_text_color(s_lbl_subtitle, lv_color_hex(0x9A9A9A),
                              LV_PART_MAIN);

  lv_anim_delete(s_lbl_position, anim_opa_cb);
  anim_fade(s_lbl_position, anim_opa_cb,
            lv_obj_get_style_opa(s_lbl_position, LV_PART_MAIN), LV_OPA_TRANSP,
            ANIM_QUICK_MS, anim_hide_done);

  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_ACTIVE, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_BG, LV_PART_MAIN);

  lv_arc_set_value(s_vol_arc, s_volume);
  s_arc_display_val = s_volume;

  lv_timer_pause(s_browse_timer);
}

static void confirm_browse() {
  if (s_browse_index < 0) {
    // External media entry — just exit browse, keep current stream
    s_was_playing = true;
    exit_browse();
    return;
  }
  // Same station already playing — just go back to volume mode
  if (s_browse_index == s_station_index && !s_external_playing &&
      s_play_state == PlayState::Playing) {
    s_was_playing = true;
    exit_browse();
    return;
  }
  s_external_playing = false;
  s_media = {};
  s_station_index = s_browse_index;
  set_logo(s_station_index);
  set_bg(s_station_index, true);
  int32_t idx = s_station_index;
  esp_event_post(APP_EVENT, APP_EVENT_STATION_CHANGED, &idx, sizeof(idx), 0);
  esp_event_post(APP_EVENT, APP_EVENT_PLAY_REQUESTED, nullptr, 0, 0);
  s_play_state = PlayState::Playing;
  s_was_playing = true;
  exit_browse();
}

static void on_browse_timeout(lv_timer_t *) { exit_browse(); }

static void on_external_idle_timeout(lv_timer_t *) {
  lv_timer_pause(s_external_idle_timer);
  if (!s_external_playing)
    return;
  if (s_play_state == PlayState::Playing ||
      s_play_state == PlayState::Transitioning)
    return;

  ESP_LOGI(TAG, "External media idle timeout — reverting to radio");
  s_external_playing = false;
  s_media = {};
  s_art_last_url[0] = '\0';
  if (s_btn_prev && s_btn_next) {
    lv_obj_add_flag(s_btn_prev, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
  }
  lv_label_set_text(s_lbl_station, STATIONS[s_station_index].name);
  set_logo(s_station_index);
  set_bg(s_station_index, true);
  lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_HIDDEN);
  lv_anim_delete(s_logo_container, anim_opa_cb);
  anim_fade(s_logo_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
            ANIM_FADE_MS);
  char saved[64] = {};
  settings_get_speaker_name(saved, sizeof(saved));
  if (saved[0])
    lv_label_set_text(s_lbl_speaker, saved);
  update_subtitle();
  show_idle_ui(home_should_idle());
}

// ─── Subtitle (play state)
// ────────────────────────────────────────────────────────────

static void update_subtitle() {
  if (s_external_playing && s_media.artist[0]) {
    lv_obj_set_width(s_lbl_subtitle, LCD_H_RES - 100);
    lv_label_set_long_mode(s_lbl_subtitle, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(s_lbl_subtitle, LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_label_set_text(s_lbl_subtitle, s_media.artist);
    return;
  }

  lv_label_set_long_mode(s_lbl_subtitle, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(s_lbl_subtitle, LV_SIZE_CONTENT);

  const char *text;
  switch (s_play_state) {
  case PlayState::Playing:
    text = s_external_playing && s_media.source[0] ? s_media.source : "Playing";
    break;
  case PlayState::Paused:
    text = "Paused";
    break;
  case PlayState::Stopped:
    text = "Tap to browse stations";
    break;
  case PlayState::Transitioning:
    text = "Loading...";
    break;
  default:
    text = "";
    break;
  }
  lv_label_set_text(s_lbl_subtitle, text);
}

// ─── Touch
// ────────────────────────────────────────────────────────────────────────────

static void do_tap() {
  if (s_on_picker) {
    if (s_discovered.count > 0 && s_speaker_highlight >= 0 &&
        s_speaker_highlight < s_discovered.count)
      select_speaker(s_speaker_highlight);
    return;
  }

  switch (s_mode) {
  case Mode::Volume:
    enter_browse();
    break;
  case Mode::Browse:
    confirm_browse();
    break;
  }
}

static void do_long_press() {
  if (s_on_picker)
    return;

  if (s_mode == Mode::Browse) {
    exit_browse();
  }

  if (s_play_state == PlayState::Playing ||
      s_play_state == PlayState::Transitioning) {
    s_play_state = PlayState::Stopped;
    esp_event_post(APP_EVENT, APP_EVENT_STOP_REQUESTED, nullptr, 0, 0);
    update_subtitle();
    show_idle_ui(true);
  }
}

static void on_press_timer(lv_timer_t *) {
  s_press_was_long = true;
  lv_timer_pause(s_press_timer);
  do_long_press();
}

static void on_screen_pressed(lv_event_t *) {
  s_press_was_long = false;
  lv_indev_t *indev = lv_indev_active();
  if (indev)
    lv_indev_get_point(indev, &s_press_point);
  lv_timer_reset(s_press_timer);
  lv_timer_resume(s_press_timer);
}

static void on_screen_released(lv_event_t *) {
  lv_timer_pause(s_press_timer);
  if (s_press_was_long)
    return;

  // Detect swipe-up: finger moved toward top of screen → open speaker menu
  lv_point_t rel = {};
  lv_indev_t *indev = lv_indev_active();
  if (indev)
    lv_indev_get_point(indev, &rel);
  int32_t dy = rel.y - s_press_point.y;
  int32_t adx = std::abs(rel.x - s_press_point.x);
  int32_t ady = std::abs(dy);
  constexpr int32_t SWIPE_MIN_DIST = 40;
  if (dy < -SWIPE_MIN_DIST && ady > adx && !s_on_picker) {
    esp_event_post(APP_EVENT, APP_EVENT_SPEAKER_RESCAN, nullptr, 0, 0);
    return;
  }

  do_tap();
}

// ─── Main Screen
// ────────────────────────────────────────────────────────────────────

// ─── Home Page (page 0 in pager)
// ────────────────────────────────────────────────────────

static void home_page_build(lv_obj_t *parent) {
  s_home = parent;

  // ── Background layers (two solid colors for tear-free crossfade) ──
  auto make_bg_layer = [&](lv_color_t color, lv_opa_t opa) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, LCD_H_RES, LCD_V_RES);
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, opa, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, 0, LV_PART_MAIN);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
  };
  s_bg_back = make_bg_layer(lv_color_hex(STATIONS[0].color), LV_OPA_COVER);
  s_bg_front = make_bg_layer(lv_color_black(), LV_OPA_TRANSP);

  // ── Black dimming overlay (edge darkening for depth) ──
  s_bg_dim = lv_obj_create(parent);
  lv_obj_set_size(s_bg_dim, LCD_H_RES, LCD_V_RES);
  lv_obj_align(s_bg_dim, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(s_bg_dim, lv_color_black(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_bg_dim, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_bg_dim, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_bg_dim, 0, LV_PART_MAIN);
  lv_obj_remove_flag(s_bg_dim, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_bg_dim, LV_OBJ_FLAG_SCROLLABLE);

  // ── Volume arc — hugs the edge of the round display ──
  s_vol_arc = lv_arc_create(parent);
  lv_obj_set_size(s_vol_arc, LCD_H_RES - 4, LCD_V_RES - 4);
  lv_obj_center(s_vol_arc);
  lv_arc_set_rotation(s_vol_arc, 135);
  lv_arc_set_bg_angles(s_vol_arc, 0, 270);
  lv_arc_set_range(s_vol_arc, VOLUME_MIN, VOLUME_MAX);
  lv_arc_set_value(s_vol_arc, s_volume);
  s_arc_display_val = s_volume;
  lv_obj_remove_flag(s_vol_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(s_vol_arc, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_BG, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_vol_arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_vol_arc, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_ACTIVE, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_vol_arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_vol_arc, LV_OPA_TRANSP, LV_PART_KNOB);

  // ── Station logo — rounded card with clip ──
  s_logo_container = lv_obj_create(parent);
  lv_obj_set_size(s_logo_container, 120, 120);
  lv_obj_set_style_bg_color(s_logo_container, lv_color_hex(STATIONS[0].color),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_logo_container, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_logo_container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_logo_container, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_logo_container, 28, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(s_logo_container, true, LV_PART_MAIN);
  lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(s_logo_container, LV_ALIGN_CENTER, 0, -30);

  // ── Prev/Next buttons — flanking album art, only shown in external mode ──
  auto make_media_btn = [&](bool is_next) -> lv_obj_t * {
    lv_obj_t *btn = lv_obj_create(parent);
    lv_obj_set_size(btn, 44, 44);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
    // Align to either side of the logo container (120px wide, centered at x=0,
    // y=-30)
    lv_obj_align(btn, LV_ALIGN_CENTER, is_next ? 110 : -110, -30);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xAAAAAA), LV_PART_MAIN);
    lv_obj_set_style_text_font(lbl, &lucide_22, LV_PART_MAIN);
    lv_label_set_text(lbl, is_next ? ICON_SKIP_FORWARD : ICON_SKIP_BACK);
    lv_obj_center(lbl);
    return btn;
  };

  s_btn_prev = make_media_btn(false);
  s_btn_next = make_media_btn(true);
  lv_obj_add_event_cb(s_btn_prev, on_prev_tap, LV_EVENT_CLICKED, nullptr);
  lv_obj_add_event_cb(s_btn_next, on_next_tap, LV_EVENT_CLICKED, nullptr);

  s_img_logo = lv_image_create(s_logo_container);
  lv_obj_set_size(s_img_logo, 120, 120);
  lv_image_set_inner_align(s_img_logo, LV_IMAGE_ALIGN_CENTER);
  lv_obj_set_pos(s_img_logo, 0, 0);
  set_logo(0);

  // ── Art loading placeholder (pulsating, shown during download) ──
  s_art_loading = lv_obj_create(s_logo_container);
  lv_obj_set_size(s_art_loading, 120, 120);
  lv_obj_set_style_bg_color(s_art_loading, lv_color_hex(0x1C1C1E),
                            LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_art_loading, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_art_loading, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_art_loading, 0, LV_PART_MAIN);
  lv_obj_remove_flag(s_art_loading, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_pos(s_art_loading, 0, 0);
  lv_obj_add_flag(s_art_loading, LV_OBJ_FLAG_HIDDEN);

  // ── Clock — large centered time, visible when idle ──
  s_lbl_clock = lv_label_create(parent);
  lv_obj_set_style_text_color(s_lbl_clock, COL_TEXT, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_clock, &geist_medium_52, LV_PART_MAIN);
  lv_obj_set_style_text_opa(s_lbl_clock, LV_OPA_90, LV_PART_MAIN);

  lv_label_set_text(s_lbl_clock, "00:00");
  lv_obj_align(s_lbl_clock, LV_ALIGN_CENTER, 0, -34);

  // ── Weather — date + icon + temp, shown during idle ──
  s_lbl_weather_date = lv_label_create(parent);
  lv_obj_set_style_text_color(s_lbl_weather_date, COL_TEXT, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_weather_date, &geist_regular_20,
                             LV_PART_MAIN);
  lv_label_set_text(s_lbl_weather_date, "");
  lv_obj_align(s_lbl_weather_date, LV_ALIGN_CENTER, -50, 10);
  lv_obj_add_flag(s_lbl_weather_date, LV_OBJ_FLAG_HIDDEN);

  s_lbl_weather_icon = lv_label_create(parent);
  lv_obj_set_style_text_font(s_lbl_weather_icon, &lucide_weather_20,
                             LV_PART_MAIN);
  lv_obj_set_style_text_color(s_lbl_weather_icon, lv_color_hex(0xFFD60A),
                              LV_PART_MAIN);
  lv_label_set_text(s_lbl_weather_icon, "");
  lv_obj_align(s_lbl_weather_icon, LV_ALIGN_CENTER, -40, 10);
  lv_obj_add_flag(s_lbl_weather_icon, LV_OBJ_FLAG_HIDDEN);

  s_lbl_weather_temp = lv_label_create(parent);
  lv_obj_set_style_text_color(s_lbl_weather_temp, COL_TEXT, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_weather_temp, &geist_regular_20,
                             LV_PART_MAIN);
  lv_label_set_text(s_lbl_weather_temp, "");
  lv_obj_align(s_lbl_weather_temp, LV_ALIGN_CENTER, 8, 9);
  lv_obj_add_flag(s_lbl_weather_temp, LV_OBJ_FLAG_HIDDEN);

  // ── Forecast strip — 5 columns of hour/icon/temp, shown during idle ──
  s_forecast_container = lv_obj_create(parent);
  lv_obj_set_size(s_forecast_container, 260, 70);
  lv_obj_align(s_forecast_container, LV_ALIGN_CENTER, 0, 80);
  lv_obj_set_style_bg_opa(s_forecast_container, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_forecast_container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_forecast_container, 0, LV_PART_MAIN);
  lv_obj_remove_flag(s_forecast_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_forecast_container, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_forecast_container, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < FORECAST_COLS; i++) {
    int32_t cx = 32 + i * 65; // 4 cols across 260px: 32, 97, 162, 227

    s_fc_hour[i] = lv_label_create(s_forecast_container);
    lv_obj_set_style_text_color(s_fc_hour[i], lv_color_hex(0x8E8E93),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_fc_hour[i], &geist_regular_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_fc_hour[i], LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_label_set_text(s_fc_hour[i], "");
    lv_obj_set_pos(s_fc_hour[i], cx - 20, 0);
    lv_obj_set_width(s_fc_hour[i], 40);

    s_fc_icon[i] = lv_label_create(s_forecast_container);
    lv_obj_set_style_text_font(s_fc_icon[i], &lucide_22, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_fc_icon[i], COL_TEXT_SEC, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_fc_icon[i], LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_label_set_text(s_fc_icon[i], "");
    lv_obj_set_pos(s_fc_icon[i], cx - 20, 20);
    lv_obj_set_width(s_fc_icon[i], 40);

    s_fc_temp[i] = lv_label_create(s_forecast_container);
    lv_obj_set_style_text_color(s_fc_temp[i], lv_color_hex(0xBBBBBB),
                                LV_PART_MAIN);
    lv_obj_set_style_text_font(s_fc_temp[i], &geist_regular_16, LV_PART_MAIN);
    lv_obj_set_style_text_align(s_fc_temp[i], LV_TEXT_ALIGN_CENTER,
                                LV_PART_MAIN);
    lv_label_set_text(s_fc_temp[i], "");
    lv_obj_set_pos(s_fc_temp[i], cx - 20, 46);
    lv_obj_set_width(s_fc_temp[i], 40);
  }

  // ── Station name ──
  s_lbl_station = lv_label_create(parent);
  lv_obj_set_style_text_color(s_lbl_station, COL_TEXT, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_station, &geist_medium_28, LV_PART_MAIN);
  lv_obj_set_width(s_lbl_station, LCD_H_RES - 80);
  lv_label_set_long_mode(s_lbl_station, LV_LABEL_LONG_SCROLL_CIRCULAR);
  lv_obj_set_style_text_align(s_lbl_station, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);
  lv_label_set_text(s_lbl_station, STATIONS[0].name);
  lv_obj_align(s_lbl_station, LV_ALIGN_CENTER, 0, 68);

  // ── Subtitle — play state or "Tap to play" in browse mode ──
  s_lbl_subtitle = lv_label_create(parent);
  lv_obj_set_style_text_color(s_lbl_subtitle, lv_color_hex(0xAAAAAA),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_subtitle, &geist_regular_16, LV_PART_MAIN);
  lv_label_set_text(s_lbl_subtitle, "");
  lv_obj_align(s_lbl_subtitle, LV_ALIGN_CENTER, 0, 94);

  // ── Position indicator (browse mode only) ──
  s_lbl_position = lv_label_create(parent);
  lv_obj_set_style_text_color(s_lbl_position, lv_color_hex(0x888888),
                              LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_position, &geist_regular_16, LV_PART_MAIN);
  lv_label_set_text(s_lbl_position, "");
  lv_obj_align(s_lbl_position, LV_ALIGN_CENTER, 0, 116);
  lv_obj_add_flag(s_lbl_position, LV_OBJ_FLAG_HIDDEN);

  // ── Speaker name — bottom, very subtle ──
  s_lbl_speaker = lv_label_create(parent);
  lv_obj_set_style_text_color(s_lbl_speaker, COL_TEXT_SEC, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_speaker, &geist_regular_16, LV_PART_MAIN);
  lv_obj_set_style_text_opa(s_lbl_speaker, LV_OPA_60, LV_PART_MAIN);
  lv_label_set_text(s_lbl_speaker, "");
  lv_obj_align(s_lbl_speaker, LV_ALIGN_BOTTOM_MID, 0, -36);
}

static void home_page_destroy() { s_home = nullptr; }

static const PageDef s_home_page = {
    .id = "home",
    .build = home_page_build,
    .destroy = home_page_destroy,
    .tick = nullptr,
};

static void build_main_screen() {
  s_screen = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_screen, COL_BG, LV_PART_MAIN);
  lv_obj_remove_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_screen, LCD_H_RES, LCD_V_RES);
  lv_obj_add_flag(s_screen, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_screen, on_screen_pressed, LV_EVENT_PRESSED, nullptr);
  lv_obj_add_event_cb(s_screen, on_screen_released, LV_EVENT_RELEASED, nullptr);

  // Pager: creates the horizontal strip and builds home page into it
  pages_init(s_screen, &s_home_page, on_page_changed);

  // ── Overlays (above pager strip) ──

  // ── WiFi dot — only visible when disconnected (red) ──
  s_wifi_dot = lv_obj_create(s_screen);
  lv_obj_set_size(s_wifi_dot, 8, 8);
  lv_obj_set_style_radius(s_wifi_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_wifi_dot, COL_RED, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_wifi_dot, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_wifi_dot, 0, LV_PART_MAIN);
  lv_obj_remove_flag(s_wifi_dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(s_wifi_dot, LV_ALIGN_TOP_MID, 0, 36);
  lv_obj_add_flag(s_wifi_dot, LV_OBJ_FLAG_HIDDEN);
}

// ─── Speaker Picker
// ───────────────────────────────────────────────────────────────

static void anim_spinner_cb(void *obj, int32_t v) {
  lv_arc_set_rotation(static_cast<lv_obj_t *>(obj), static_cast<int16_t>(v));
}

// Guard to prevent concurrent background speaker tasks
static volatile bool s_speaker_task_running = false;

// Args for the background speaker-switch task (static — only one switch at a
// time)
static struct {
  char new_ip[40];
  char new_name[64];
  int new_port;
  bool was_playing;
} s_switch_args;

static void speaker_switch_task(void *) {
  // All blocking HTTP calls run here, OFF the LVGL task
  sonos_stop();
  sonos_set_speaker(s_switch_args.new_ip, s_switch_args.new_port);
  settings_set_speaker_name(s_switch_args.new_name);
  sonos_start();
  if (s_switch_args.was_playing)
    esp_event_post(APP_EVENT, APP_EVENT_PLAY_REQUESTED, nullptr, 0, 0);
  s_speaker_task_running = false;
  vTaskDelete(nullptr);
}

static void select_speaker(int index) {
  if (index < 0 || index >= s_discovered.count)
    return;
  if (s_speaker_task_running)
    return;
  auto &speaker = s_discovered.speakers[index];

  // Capture args for the background task
  strncpy(s_switch_args.new_ip, speaker.ip, sizeof(s_switch_args.new_ip) - 1);
  s_switch_args.new_ip[sizeof(s_switch_args.new_ip) - 1] = '\0';
  strncpy(s_switch_args.new_name, speaker.name,
          sizeof(s_switch_args.new_name) - 1);
  s_switch_args.new_name[sizeof(s_switch_args.new_name) - 1] = '\0';
  s_switch_args.new_port = speaker.port;
  s_switch_args.was_playing = (s_play_state == PlayState::Playing ||
                               s_play_state == PlayState::Transitioning);

  // UI updates (instant, on LVGL task)
  strncpy(s_current_speaker, speaker.name, sizeof(s_current_speaker) - 1);
  lv_label_set_text(s_lbl_speaker, speaker.name);
  s_on_picker = false;
  lv_screen_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 150, 0, false);
  ESP_LOGI(TAG, "Speaker selected: %s (%s)", speaker.name, speaker.ip);

  // Blocking work on a background task
  s_speaker_task_running = true;
  xTaskCreatePinnedToCore(speaker_switch_task, "spk_sw", 10240, nullptr,
                          NET_TASK_PRIO, nullptr, NET_TASK_CORE);
}

static void on_speaker_tap(lv_event_t *e) {
  auto index =
      static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  select_speaker(index);
}

static void highlight_picker_item(int highlight) {
  if (!s_picker_list)
    return;
  int count = lv_obj_get_child_count(s_picker_list);
  for (int i = 0; i < count; i++) {
    lv_obj_t *child = lv_obj_get_child(s_picker_list, i);
    bool active = (i == highlight);
    lv_obj_set_style_border_color(
        child, active ? COL_ACCENT : lv_color_hex(0x2C2C2E), LV_PART_MAIN);
    lv_obj_set_style_border_width(child, active ? 2 : 1, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        child, active ? lv_color_hex(0x1A1A2E) : lv_color_hex(0x1C1C1E),
        LV_PART_MAIN);
    if (active)
      lv_obj_scroll_to_view(child, LV_ANIM_ON);
  }
}

static lv_point_t s_picker_press_point;

static void on_picker_pressed(lv_event_t *) {
  lv_indev_t *indev = lv_indev_active();
  if (indev)
    lv_indev_get_point(indev, &s_picker_press_point);
}

static void on_picker_released(lv_event_t *) {
  if (!s_current_speaker[0])
    return;
  lv_point_t rel = {};
  lv_indev_t *indev = lv_indev_active();
  if (indev)
    lv_indev_get_point(indev, &rel);
  int32_t dy = rel.y - s_picker_press_point.y;
  // Swipe down (positive dy with corrected touch coords) → dismiss
  if (dy > 30) {
    s_on_picker = false;
    lv_screen_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 150, 0, false);
  }
}

static void on_picker_dismiss(lv_event_t *) {
  s_on_picker = false;
  lv_screen_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 150, 0, false);
}

static void on_prev_tap(lv_event_t *) {
  backlight_poke();
  haptic_buzz();
  sonos_previous();
}

static void on_next_tap(lv_event_t *) {
  backlight_poke();
  haptic_buzz();
  sonos_next();
}

static void build_picker_scanning() {
  if (s_scr_picker)
    lv_obj_delete(s_scr_picker);

  s_scr_picker = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scr_picker, COL_BG, LV_PART_MAIN);
  lv_obj_set_size(s_scr_picker, LCD_H_RES, LCD_V_RES);
  lv_obj_remove_flag(s_scr_picker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_scr_picker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_scr_picker, on_picker_pressed, LV_EVENT_PRESSED,
                      nullptr);
  lv_obj_add_event_cb(s_scr_picker, on_picker_released, LV_EVENT_RELEASED,
                      nullptr);

  lv_obj_t *title = lv_label_create(s_scr_picker);
  lv_obj_set_style_text_color(title, COL_TEXT_SEC, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &geist_regular_22, LV_PART_MAIN);
  lv_label_set_text(title, "Speakers");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 54);

  s_picker_spinner = lv_arc_create(s_scr_picker);
  lv_obj_set_size(s_picker_spinner, 48, 48);
  lv_arc_set_rotation(s_picker_spinner, 0);
  lv_arc_set_bg_angles(s_picker_spinner, 0, 360);
  lv_arc_set_range(s_picker_spinner, 0, 100);
  lv_arc_set_value(s_picker_spinner, 25);
  lv_obj_remove_flag(s_picker_spinner, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(s_picker_spinner, 3, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_picker_spinner, lv_color_hex(0x2C2C2E),
                             LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_picker_spinner, 3, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_picker_spinner, COL_ACCENT, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_picker_spinner, LV_OPA_TRANSP, LV_PART_KNOB);
  lv_obj_align(s_picker_spinner, LV_ALIGN_CENTER, 0, -16);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_picker_spinner);
  lv_anim_set_exec_cb(&a, anim_spinner_cb);
  lv_anim_set_values(&a, 0, 360);
  lv_anim_set_duration(&a, 1200);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_start(&a);

  lv_obj_t *scan_lbl = lv_label_create(s_scr_picker);
  lv_obj_set_style_text_color(scan_lbl, COL_TEXT_SEC, LV_PART_MAIN);
  lv_obj_set_style_text_font(scan_lbl, &geist_regular_16, LV_PART_MAIN);
  lv_label_set_text(scan_lbl, "Scanning for speakers\u2026");
  lv_obj_align(scan_lbl, LV_ALIGN_CENTER, 0, 24);
}

static void rebuild_speaker_list() {
  if (s_scr_picker)
    lv_obj_delete(s_scr_picker);

  s_scr_picker = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scr_picker, COL_BG, LV_PART_MAIN);
  lv_obj_set_size(s_scr_picker, LCD_H_RES, LCD_V_RES);
  lv_obj_remove_flag(s_scr_picker, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_scr_picker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_scr_picker, on_picker_pressed, LV_EVENT_PRESSED,
                      nullptr);
  lv_obj_add_event_cb(s_scr_picker, on_picker_released, LV_EVENT_RELEASED,
                      nullptr);

  lv_obj_t *title = lv_label_create(s_scr_picker);
  lv_obj_set_style_text_color(title, COL_TEXT_SEC, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &geist_regular_22, LV_PART_MAIN);
  lv_label_set_text(title, "Speakers");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 54);

  if (s_discovered.count > 0) {
    s_picker_list = lv_obj_create(s_scr_picker);
    lv_obj_set_size(s_picker_list, 260, 170);
    lv_obj_align(s_picker_list, LV_ALIGN_CENTER, 0, -2);
    lv_obj_set_style_bg_opa(s_picker_list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(s_picker_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_picker_list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_picker_list, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(s_picker_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_picker_list, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(s_picker_list, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < s_discovered.count; i++) {
      auto &spk = s_discovered.speakers[i];
      bool is_current =
          (s_current_speaker[0] && strcmp(spk.name, s_current_speaker) == 0);
      bool has_dot = is_current;

      lv_obj_t *card = lv_obj_create(s_picker_list);
      lv_obj_set_size(card, 240, 50);
      lv_obj_set_style_bg_color(card, lv_color_hex(0x1C1C1E), LV_PART_MAIN);
      lv_obj_set_style_bg_opa(card, LV_OPA_COVER, LV_PART_MAIN);
      lv_obj_set_style_border_color(card, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
      lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
      lv_obj_set_style_radius(card, 16, LV_PART_MAIN);
      lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_style_bg_color(
          card, lv_color_hex(0x0A3A6B),
          static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED));

      if (has_dot) {
        lv_obj_t *dot = lv_obj_create(card);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
        lv_obj_set_style_bg_color(dot, COL_GREEN, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 12, 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
      }

      lv_obj_t *lbl = lv_label_create(card);
      lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
      lv_obj_set_style_text_font(lbl, &geist_regular_18, LV_PART_MAIN);
      lv_label_set_text(lbl, spk.name);
      lv_obj_align(lbl, LV_ALIGN_LEFT_MID, has_dot ? 26 : 16, 0);

      lv_obj_add_event_cb(card, on_speaker_tap, LV_EVENT_SHORT_CLICKED,
                          reinterpret_cast<void *>(static_cast<intptr_t>(i)));
    }

    highlight_picker_item(0);
  } else {
    s_picker_list = nullptr;
    lv_obj_t *empty = lv_label_create(s_scr_picker);
    lv_obj_set_style_text_color(empty, COL_TEXT_SEC, LV_PART_MAIN);
    lv_obj_set_style_text_font(empty, &geist_regular_16, LV_PART_MAIN);
    lv_label_set_text(empty, "No speakers found.\nCheck your network.");
    lv_obj_set_style_text_align(empty, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(empty, LV_ALIGN_CENTER, 0, 0);
  }

  // Dismiss / Back button
  lv_obj_t *back_btn = lv_obj_create(s_scr_picker);
  lv_obj_set_size(back_btn, 110, 40);
  lv_obj_align(back_btn, LV_ALIGN_BOTTOM_MID, 0, -37);
  lv_obj_set_style_bg_color(back_btn, lv_color_hex(0x2C2C2E), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(back_btn, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(back_btn, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(back_btn, 20, LV_PART_MAIN);
  lv_obj_set_style_pad_all(back_btn, 0, LV_PART_MAIN);
  lv_obj_remove_flag(back_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(back_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(
      back_btn, lv_color_hex(0x222222),
      static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED));
  lv_obj_set_flex_flow(back_btn, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(back_btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(back_btn, 6, LV_PART_MAIN);

  lv_obj_t *back_icon = lv_label_create(back_btn);
  lv_obj_set_style_text_color(back_icon, COL_TEXT, LV_PART_MAIN);
  lv_obj_set_style_text_font(back_icon, &lucide_22, LV_PART_MAIN);
  lv_label_set_text(back_icon, s_current_speaker[0] ? ICON_ARROW_LEFT : ICON_X);

  lv_obj_t *back_lbl = lv_label_create(back_btn);
  lv_obj_set_style_text_color(back_lbl, COL_TEXT, LV_PART_MAIN);
  lv_obj_set_style_text_font(back_lbl, &geist_regular_16, LV_PART_MAIN);
  lv_label_set_text(back_lbl, s_current_speaker[0] ? "Back" : "Dismiss");

  lv_obj_add_event_cb(back_btn, on_picker_dismiss, LV_EVENT_SHORT_CLICKED,
                      nullptr);
}

// ─── Public API
// ─────────────────────────────────────────────────────────────────────

static void on_splash_done(lv_timer_t *t) {
  lv_timer_delete(t);
  if (display_lock(50)) {
    lv_screen_load_anim(s_screen, LV_SCR_LOAD_ANIM_FADE_IN, 400, 0, true);
    display_unlock();
  }
}

void ui_init() {
  lv_display_t *disp = nullptr;
  lv_indev_t *touch = nullptr;
  display_init(&disp, &touch);

  s_volume = settings_get_volume();

  if (display_lock(200)) {
    // Boot splash
    lv_obj_t *splash = lv_obj_create(nullptr);
    lv_obj_set_style_bg_color(splash, COL_BG, LV_PART_MAIN);
    lv_obj_remove_flag(splash, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(splash);
    lv_obj_set_style_text_font(lbl, &geist_medium_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, COL_TEXT_SEC, LV_PART_MAIN);
    lv_label_set_text(lbl, "knob <3");
    lv_obj_center(lbl);

    lv_screen_load(splash);
    lv_timer_create(on_splash_done, 1200, nullptr);

    build_main_screen();
    s_vol_hide_timer = lv_timer_create(on_vol_hide, VOL_DISPLAY_MS, nullptr);
    lv_timer_pause(s_vol_hide_timer);

    s_browse_timer =
        lv_timer_create(on_browse_timeout, BROWSE_TIMEOUT_MS, nullptr);
    lv_timer_pause(s_browse_timer);

    s_clock_timer = lv_timer_create(on_clock_tick, 30000, nullptr);

    s_bl_timer =
        lv_timer_create(on_backlight_step, BACKLIGHT_FADE_STEP_MS, nullptr);
    lv_timer_pause(s_bl_timer);

    s_bl_inactivity_timer =
        lv_timer_create(on_bl_inactivity, BACKLIGHT_INACTIVITY_MS, nullptr);
    lv_timer_pause(s_bl_inactivity_timer);

    s_press_timer = lv_timer_create(on_press_timer, 500, nullptr);
    lv_timer_pause(s_press_timer);

    s_external_idle_timer = lv_timer_create(on_external_idle_timeout,
                                            EXTERNAL_IDLE_TIMEOUT_MS, nullptr);
    lv_timer_pause(s_external_idle_timer);

    lv_timer_create(on_encoder_poll, ENCODER_POLL_MS, nullptr);

    show_idle_ui(true);

    set_logo(s_station_index);
    set_bg(s_station_index, /*animate=*/false);

    char saved_name[64] = {};
    settings_get_speaker_name(saved_name, sizeof(saved_name));
    if (saved_name[0]) {
      lv_label_set_text(s_lbl_speaker, saved_name);
      strncpy(s_current_speaker, saved_name, sizeof(s_current_speaker) - 1);
    }

    update_subtitle();
    // Main screen loaded by on_splash_done timer — not here
    display_unlock();
  }

  ESP_LOGI(TAG, "UI ready");
}

void ui_set_volume(int level) {
  if (display_lock(200)) {
    if (lv_tick_elaps(s_local_vol_ms) < VOL_LOCAL_GRACE_MS) {
      display_unlock();
      return;
    }
    if (level != s_volume) {
      if (pages_is_home() && s_mode == Mode::Volume) {
        show_volume(level);
      } else {
        lv_anim_delete(s_vol_arc, anim_vol_arc_cb);
        s_arc_display_val = level;
        lv_arc_set_value(s_vol_arc, level);
      }
      s_volume = level;
    }
    display_unlock();
  }
}

void ui_set_play_state(PlayState state) {
  if (state == s_play_state)
    return;
  if (display_lock(50)) {
    s_play_state = state;
    if (state == PlayState::Stopped && s_external_playing) {
      lv_timer_pause(s_external_idle_timer);
      s_external_playing = false;
      s_media = {};
      if (s_btn_prev && s_btn_next) {
        lv_obj_add_flag(s_btn_prev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
      }
      lv_label_set_text(s_lbl_station, STATIONS[s_station_index].name);
      lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_opa(s_logo_container, LV_OPA_COVER, LV_PART_MAIN);
    }
    // External idle timeout — revert to radio after 5 min of paused/stopped
    if (s_external_playing) {
      if (state == PlayState::Playing || state == PlayState::Transitioning) {
        lv_timer_pause(s_external_idle_timer);
      } else {
        lv_timer_reset(s_external_idle_timer);
        lv_timer_resume(s_external_idle_timer);
      }
    }
    if (s_mode == Mode::Volume) {
      update_subtitle();
      show_idle_ui(state == PlayState::Stopped);
    }
    display_unlock();
  }
}

void ui_set_station(int index) {
  if (index < 0 || index >= STATION_COUNT)
    return;
  if (index == s_station_index && !s_external_playing)
    return;
  if (display_lock(50)) {
    bool was_external = s_external_playing;
    s_station_index = index;
    s_external_playing = false;
    s_media = {};
    if (s_mode == Mode::Volume) {
      lv_label_set_text(s_lbl_station, STATIONS[index].name);
      if (was_external) {
        if (s_btn_prev && s_btn_next) {
          lv_obj_add_flag(s_btn_prev, LV_OBJ_FLAG_HIDDEN);
          lv_obj_add_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_HIDDEN);
        lv_anim_delete(s_logo_container, anim_opa_cb);
        lv_obj_set_style_opa(s_logo_container, LV_OPA_TRANSP, LV_PART_MAIN);
        anim_fade(s_logo_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                  ANIM_FADE_MS);
      }
      set_logo(index);
      set_bg(index, true);
      update_subtitle();
    }
    display_unlock();
  }
}

static void art_free_pixels() {
  if (s_art_pixels) {
    heap_caps_free(s_art_pixels);
    s_art_pixels = nullptr;
  }
  s_art_dsc = {};

  // Reset image widget back to station logo state after album art
  if (s_art_loading) {
    lv_anim_delete(s_art_loading, nullptr);
    lv_obj_add_flag(s_art_loading, LV_OBJ_FLAG_HIDDEN);
  }
  lv_obj_remove_flag(s_img_logo, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(s_img_logo, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_size(s_img_logo, 120, 120);
  lv_obj_set_pos(s_img_logo, 0, 0);
  lv_image_set_inner_align(s_img_logo, LV_IMAGE_ALIGN_CENTER);
}

void ui_set_media_info(const MediaInfo *info) {
  // Download + decode album art OUTSIDE the display lock (HTTP + decode block)
  // Guard against re-entry: if a fetch is already in progress, skip this one
  bool art_ready = false;
  if (info && info->has_media && info->art_url[0] &&
      strcmp(info->art_url, s_art_last_url) != 0 && !s_art_fetching) {
    s_art_fetching = true;
    ESP_LOGI(TAG, "Art URL changed: %.120s", info->art_url);
    strncpy(s_art_last_url, info->art_url, sizeof(s_art_last_url) - 1);
    s_art_last_url[sizeof(s_art_last_url) - 1] = '\0';

    // Show pulsating loading placeholder (Spotify pattern)
    if (display_lock(50)) {
      lv_obj_add_flag(s_img_logo, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s_art_loading, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_opa(s_logo_container, LV_OPA_COVER, LV_PART_MAIN);
      lv_anim_t pulse;
      lv_anim_init(&pulse);
      lv_anim_set_var(&pulse, s_art_loading);
      lv_anim_set_values(&pulse, LV_OPA_40, LV_OPA_80);
      lv_anim_set_duration(&pulse, 600);
      lv_anim_set_exec_cb(&pulse, anim_bg_opa_cb);
      lv_anim_set_playback_duration(&pulse, 600);
      lv_anim_set_repeat_count(&pulse, LV_ANIM_REPEAT_INFINITE);
      lv_anim_start(&pulse);
      display_unlock();
    }

    if (!s_art_jpeg)
      s_art_jpeg = static_cast<uint8_t *>(
          heap_caps_malloc(ART_BUF_SIZE, MALLOC_CAP_SPIRAM));
    if (s_art_jpeg) {
      int len = sonos_fetch_art(info->art_url, s_art_jpeg, ART_BUF_SIZE);
      if (len > 0) {
        uint8_t *px = nullptr;
        int aw = 0, ah = 0;
        bool decoded = art_decode_jpeg(s_art_jpeg, len, &px, &aw, &ah, 300);
        if (decoded && px) {
          if (display_lock(200)) {
            art_free_pixels();
            s_art_pixels = px;
            s_art_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
            s_art_dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
            s_art_dsc.header.w = static_cast<uint32_t>(aw);
            s_art_dsc.header.h = static_cast<uint32_t>(ah);
            s_art_dsc.header.stride = static_cast<uint32_t>(aw * 2);
            s_art_dsc.data_size = static_cast<uint32_t>(aw * ah * 2);
            s_art_dsc.data = s_art_pixels;
            art_ready = true;

            lv_image_set_src(s_img_logo, &s_art_dsc);
            lv_image_set_inner_align(s_img_logo, LV_IMAGE_ALIGN_STRETCH);

            // Stop pulse, hide placeholder, fade in art
            lv_anim_delete(s_art_loading, anim_bg_opa_cb);
            lv_obj_add_flag(s_art_loading, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(s_img_logo, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_opa(s_img_logo, LV_OPA_TRANSP, LV_PART_MAIN);
            anim_fade(s_img_logo, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                      300);
            display_unlock();
          } else {
            heap_caps_free(px);
          }
        }
      }
    }
    s_art_fetching = false;

    // If art failed to load, stop the pulse placeholder
    if (!art_ready && display_lock(50)) {
      lv_anim_delete(s_art_loading, anim_bg_opa_cb);
      lv_obj_add_flag(s_art_loading, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s_img_logo, LV_OBJ_FLAG_HIDDEN);
      display_unlock();
    }

    // Skip the normal display_lock section below — art was handled inline
    if (art_ready) {
      if (!display_lock(50))
        return;
      s_media = *info;
      s_external_playing = true;
      s_track_changed_ms = lv_tick_get();
      if (s_mode == Mode::Volume && !s_idle_active) {
        if (s_btn_prev && s_btn_next) {
          lv_obj_remove_flag(s_btn_prev, LV_OBJ_FLAG_HIDDEN);
          lv_obj_remove_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
        }
        lv_label_set_text(s_lbl_station,
                          s_media.title[0] ? s_media.title : "Unknown");
        if (s_media.source[0])
          lv_label_set_text(s_lbl_speaker, s_media.source);
        update_subtitle();
      }
      display_unlock();
      return;
    }
  }

  if (!display_lock(50))
    return;

  bool was_external = s_external_playing;

  if (info && info->has_media && s_play_state != PlayState::Stopped) {
    s_media = *info;
    s_external_playing = true;
    s_track_changed_ms = lv_tick_get();

    if (s_mode == Mode::Volume && !s_idle_active) {
      if (s_btn_prev && s_btn_next) {
        lv_obj_remove_flag(s_btn_prev, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
      }
      lv_label_set_text(s_lbl_station,
                        s_media.title[0] ? s_media.title : "Unknown");

      // Show album art if we decoded it, otherwise hide the logo
      if (s_art_pixels && (art_ready || !was_external)) {
        lv_image_set_src(s_img_logo, &s_art_dsc);
        lv_image_set_inner_align(s_img_logo, LV_IMAGE_ALIGN_STRETCH);
        lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_HIDDEN);
        lv_anim_delete(s_logo_container, anim_opa_cb);
        if (!was_external) {
          lv_obj_set_style_opa(s_logo_container, LV_OPA_TRANSP, LV_PART_MAIN);
          anim_fade(s_logo_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                    ANIM_FADE_MS);
        } else {
          lv_obj_set_style_opa(s_logo_container, LV_OPA_COVER, LV_PART_MAIN);
        }
      } else if (!s_art_pixels && !was_external) {
        lv_anim_delete(s_logo_container, anim_opa_cb);
        anim_fade(s_logo_container, anim_opa_cb,
                  lv_obj_get_style_opa(s_logo_container, LV_PART_MAIN),
                  LV_OPA_TRANSP, ANIM_FADE_MS, anim_hide_done);
      }

      if (s_media.source[0])
        lv_label_set_text(s_lbl_speaker, s_media.source);

      update_subtitle();
    }
  } else if (was_external) {
    s_external_playing = false;
    s_media = {};
    s_art_last_url[0] = '\0';
    if (s_btn_prev && s_btn_next) {
      lv_obj_add_flag(s_btn_prev, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_btn_next, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_mode == Mode::Volume && !s_idle_active) {
      art_free_pixels();
      lv_label_set_text(s_lbl_station, STATIONS[s_station_index].name);
      set_logo(s_station_index);
      lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_HIDDEN);
      lv_anim_delete(s_logo_container, anim_opa_cb);
      anim_fade(s_logo_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                ANIM_FADE_MS);

      char saved[64] = {};
      settings_get_speaker_name(saved, sizeof(saved));
      if (saved[0])
        lv_label_set_text(s_lbl_speaker, saved);

      update_subtitle();
    }
  }

  display_unlock();
}

void ui_set_wifi_status(bool connected) {
  if (display_lock(50)) {
    if (connected) {
      lv_obj_add_flag(s_wifi_dot, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_remove_flag(s_wifi_dot, LV_OBJ_FLAG_HIDDEN);
    }
    display_unlock();
  }
}

void ui_set_speaker_name(const char *name) {
  if (display_lock(50)) {
    lv_label_set_text(s_lbl_speaker, name);
    strncpy(s_current_speaker, name, sizeof(s_current_speaker) - 1);
    display_unlock();
  }
}

void ui_set_weather(const WeatherData *data) {
  if (!display_lock(50))
    return;

  s_weather_valid = data && data->valid && data->condition[0];
  if (s_weather_valid) {
    int t = static_cast<int>(data->temperature +
                             (data->temperature >= 0 ? 0.5f : -0.5f));
    snprintf(s_weather_temp, sizeof(s_weather_temp), "%d\xC2\xB0", t);

    // Format "Mon 20" from local time
    static const char *const DAYS[] = {"Sun", "Mon", "Tue", "Wed",
                                       "Thu", "Fri", "Sat"};
    time_t now = time(nullptr);
    struct tm *t_info = localtime(&now);
    if (t_info)
      snprintf(s_weather_date, sizeof(s_weather_date), "%s %d",
               DAYS[t_info->tm_wday], t_info->tm_mday);

    if (data->icon) {
      strncpy(s_weather_icon, data->icon, sizeof(s_weather_icon) - 1);
      s_weather_icon[sizeof(s_weather_icon) - 1] = '\0';
    }
    s_weather_color = data->color;

    // Update forecast strip labels
    s_forecast_count = data->forecast_count;
    for (int i = 0; i < FORECAST_COLS; i++) {
      if (i < data->forecast_count) {
        auto &fc = data->forecast[i];
        char hbuf[8];
        snprintf(hbuf, sizeof(hbuf), "%02d", fc.hour);
        lv_label_set_text(s_fc_hour[i], hbuf);
        lv_label_set_text(s_fc_icon[i], fc.icon ? fc.icon : "");
        lv_obj_set_style_text_color(s_fc_icon[i], lv_color_hex(fc.color),
                                    LV_PART_MAIN);
        char tbuf[8];
        int ft = static_cast<int>(fc.temperature +
                                  (fc.temperature >= 0 ? 0.5f : -0.5f));
        snprintf(tbuf, sizeof(tbuf), "%d\xC2\xB0", ft);
        lv_label_set_text(s_fc_temp[i], tbuf);
        lv_obj_remove_flag(s_fc_hour[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_fc_icon[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_fc_temp[i], LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(s_fc_hour[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_fc_icon[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_fc_temp[i], LV_OBJ_FLAG_HIDDEN);
      }
    }
  }

  if (s_idle_active && s_weather_valid) {
    lv_label_set_text(s_lbl_weather_date, s_weather_date);
    lv_label_set_text(s_lbl_weather_icon, s_weather_icon);
    lv_obj_set_style_text_color(s_lbl_weather_icon,
                                lv_color_hex(s_weather_color), LV_PART_MAIN);
    lv_label_set_text(s_lbl_weather_temp, s_weather_temp);

    // Center date + icon + temp as a group
    lv_obj_update_layout(s_lbl_weather_date);
    lv_obj_update_layout(s_lbl_weather_temp);
    int32_t dw = lv_obj_get_width(s_lbl_weather_date);
    int32_t tw = lv_obj_get_width(s_lbl_weather_temp);
    int32_t total = dw + 8 + 20 + 4 + tw;
    int32_t x = -(total / 2);
    lv_obj_align(s_lbl_weather_date, LV_ALIGN_CENTER, x + dw / 2, 10);
    x += dw + 8;
    lv_obj_align(s_lbl_weather_icon, LV_ALIGN_CENTER, x + 10, 10);
    x += 20 + 4;
    lv_obj_align(s_lbl_weather_temp, LV_ALIGN_CENTER, x + tw / 2, 10);

    if (lv_obj_has_flag(s_lbl_weather_icon, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_remove_flag(s_lbl_weather_date, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s_lbl_weather_icon, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s_lbl_weather_temp, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_opa(s_lbl_weather_date, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_opa(s_lbl_weather_icon, LV_OPA_TRANSP, LV_PART_MAIN);
      lv_obj_set_style_opa(s_lbl_weather_temp, LV_OPA_TRANSP, LV_PART_MAIN);
      anim_fade(s_lbl_weather_date, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                ANIM_FADE_MS);
      anim_fade(s_lbl_weather_icon, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                ANIM_FADE_MS);
      anim_fade(s_lbl_weather_temp, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                ANIM_FADE_MS);
      anim_fade(s_lbl_subtitle, anim_opa_cb,
                lv_obj_get_style_opa(s_lbl_subtitle, LV_PART_MAIN),
                LV_OPA_TRANSP, ANIM_QUICK_MS, anim_hide_done);
    }

    // Show forecast strip if available
    if (s_forecast_count > 0 &&
        lv_obj_has_flag(s_forecast_container, LV_OBJ_FLAG_HIDDEN)) {
      lv_obj_remove_flag(s_forecast_container, LV_OBJ_FLAG_HIDDEN);
      lv_obj_set_style_opa(s_forecast_container, LV_OPA_TRANSP, LV_PART_MAIN);
      anim_fade(s_forecast_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
                ANIM_FADE_MS);
    }
  }

  display_unlock();
}

static void on_page_changed(int index, const char *id) {
  (void)id;
  if (index == 0) {
    lv_anim_delete(s_vol_arc, anim_vol_arc_cb);
    lv_arc_set_value(s_vol_arc, s_volume);
    s_arc_display_val = s_volume;

    show_idle_ui(home_should_idle());
    update_subtitle();
  }
}

// Core encoder logic — called from LVGL task context (no lock needed)
static void handle_encoder(int32_t steps) {
  backlight_poke();

  if (s_on_picker) {
    if (s_discovered.count > 0) {
      s_speaker_highlight =
          std::clamp(s_speaker_highlight + static_cast<int>(steps), 0,
                     s_discovered.count - 1);
      highlight_picker_item(s_speaker_highlight);
    }
    return;
  }

  if (!pages_is_home()) {
    pages_navigate(steps > 0 ? 1 : -1);
    pages_poke();
    return;
  }

  pages_poke();

  switch (s_mode) {
  case Mode::Volume: {
    int raw = s_volume + static_cast<int>(steps) * VOLUME_STEP;
    s_volume = std::clamp(raw, VOLUME_MIN, VOLUME_MAX);
    if (raw < VOLUME_MIN || raw > VOLUME_MAX)
      haptic_buzz();
    show_volume(s_volume);
    s_local_vol_ms = lv_tick_get();
    int32_t vol = s_volume;
    esp_event_post(APP_EVENT, APP_EVENT_VOLUME_CHANGED, &vol, sizeof(vol), 0);
    sonos_set_volume(s_volume);
    break;
  }
  case Mode::Browse: {
    int total = browse_total();
    int base = s_browse_has_external ? -1 : 0;
    int shifted = s_browse_index - base + static_cast<int>(steps);
    shifted = ((shifted % total) + total) % total;
    s_browse_index = shifted + base;

    if (s_browse_index < 0) {
      lv_label_set_text(s_lbl_station,
                        s_media.title[0] ? s_media.title : "Unknown");
      lv_label_set_text(s_lbl_subtitle,
                        s_media.source[0] ? s_media.source : "Now playing");
    } else {
      lv_label_set_text(s_lbl_station, STATIONS[s_browse_index].name);
      lv_label_set_text(s_lbl_subtitle, "Tap to play");
    }

    lv_anim_delete(s_logo_container, anim_opa_cb);
    anim_fade(s_logo_container, anim_opa_cb,
              lv_obj_get_style_opa(s_logo_container, LV_PART_MAIN),
              LV_OPA_TRANSP, ANIM_QUICK_MS / 2, on_browse_rotate_fade_done);

    lv_anim_delete(s_lbl_station, anim_opa_cb);
    lv_obj_set_style_opa(s_lbl_station, LV_OPA_TRANSP, LV_PART_MAIN);
    anim_fade(s_lbl_station, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
              ANIM_QUICK_MS);

    browse_update_position();

    lv_timer_reset(s_browse_timer);
    break;
  }
  }
}

// LVGL timer — drains accumulated encoder steps in one shot
static void on_encoder_poll(lv_timer_t *) {
  int32_t steps = encoder_take_steps();
  if (steps != 0)
    handle_encoder(steps);
}

void ui_on_encoder_rotate(int32_t steps) {
  if (!display_lock(50))
    return;
  handle_encoder(steps);
  display_unlock();
}

void ui_on_touch_tap() {
  if (!display_lock(50))
    return;
  backlight_poke();
  pages_poke();
  if (!pages_is_home())
    pages_go_home();
  else
    do_tap();
  display_unlock();
}

void ui_on_touch_long_press() {
  if (!display_lock(50))
    return;
  do_long_press();
  display_unlock();
}

void ui_show_scanning() {
  if (display_lock(50)) {
    build_picker_scanning();
    s_on_picker = true;
    lv_screen_load_anim(s_scr_picker, LV_SCR_LOAD_ANIM_OVER_TOP, 150, 0, false);
    display_unlock();
  }
}

void ui_show_speaker_picker(const DiscoveryResult *speakers) {
  if (display_lock(200)) {
    if (!speakers) {
      if (s_on_picker) {
        s_on_picker = false;
        lv_anim_delete(s_picker_spinner, anim_spinner_cb);
        lv_screen_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_TOP, 150, 0, false);
      }
      display_unlock();
      return;
    }

    memcpy(&s_discovered, speakers, sizeof(s_discovered));
    std::sort(s_discovered.speakers, s_discovered.speakers + s_discovered.count,
              [](const SonosSpeaker &a, const SonosSpeaker &b) {
                return strcmp(a.name, b.name) < 0;
              });
    s_speaker_highlight = 0;

    bool was_showing = s_on_picker;
    rebuild_speaker_list();
    s_on_picker = true;

    if (was_showing) {
      lv_screen_load(s_scr_picker);
    } else {
      lv_screen_load_anim(s_scr_picker, LV_SCR_LOAD_ANIM_OVER_TOP, 150, 0,
                          false);
    }
    display_unlock();
  }
}
