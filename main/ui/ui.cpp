#include "ui.h"
#include "app_config.h"
#include "fonts/fonts.h"
#include "input/haptic.h"
#include "sonos/sonos.h"
#include "storage/settings.h"
#include "ui/display.h"
#include "ui/images/images.h"
#include "ui_pages.h"
#include "ui_timer.h"
#include "ui_voice.h"

#include "esp_log.h"
#include "lvgl.h"

#include <algorithm>
#include <cstdio>
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

enum class Mode { Volume, Browse, Voice };

static constexpr int BROWSE_TIMEOUT_MS = 7000;
static constexpr int VOL_DISPLAY_MS = 1500;
static constexpr int ANIM_FADE_MS = 200;
static constexpr int ANIM_QUICK_MS = 100;
static constexpr int ANIM_ARC_FADE_MS = 400;
static constexpr int ANIM_BG_FADE_MS = 250;
static constexpr int ANIM_BG_BROWSE_MS = 250;

static constexpr uint8_t BACKLIGHT_NORMAL = 80;
static constexpr uint8_t BACKLIGHT_DIM = 8;
static constexpr int BACKLIGHT_FADE_STEP_MS = 30;

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

// Speaker (bottom)
static lv_obj_t *s_lbl_speaker;

// Artwork container (rounded card with shadow)
static lv_obj_t *s_logo_container;

// Volume arc (inside home page)
static lv_obj_t *s_vol_arc;
static lv_timer_t *s_vol_hide_timer;

// Browse timeout
static lv_timer_t *s_browse_timer;

// Touch press detection (manual long-press via timer)
static lv_timer_t *s_press_timer;
static bool s_press_was_long;

// Backlight dimming
static lv_timer_t *s_bl_timer;
static uint8_t s_bl_current = BACKLIGHT_NORMAL;
static uint8_t s_bl_target = BACKLIGHT_NORMAL;

// Double-tap detection for voice mode
static uint32_t s_last_tap_ms;
static lv_timer_t *s_tap_delay_timer;
static int s_pre_voice_volume;

// Speaker picker
static lv_obj_t *s_scr_speaker_picker;
static lv_obj_t *s_scanning_overlay;
static lv_obj_t *s_lbl_scanning;
static DiscoveryResult s_discovered;
static int s_speaker_highlight;
static bool s_on_picker;

// ─── Station Images (compiled into firmware as C arrays)
// ────────────────────────────────────────────────────

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
static void activate_voice();
static void deactivate_voice();
static void on_page_changed(int index, const char *id);

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
  // Only swap logo during browse — bg stays on the playing station's image.
  // Bg only transitions on confirm/exit (tap to play).
  set_logo(s_browse_index);
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

  backlight_fade_to(idle ? BACKLIGHT_DIM : BACKLIGHT_NORMAL);

  lv_anim_delete(s_lbl_clock, anim_opa_cb);
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

    lv_obj_set_style_opa(s_lbl_subtitle, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_align(s_lbl_subtitle, LV_ALIGN_CENTER, 0, 30);
    anim_fade(s_lbl_subtitle, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
              ANIM_FADE_MS);

    lv_timer_resume(s_clock_timer);
  } else {
    anim_fade(s_lbl_clock, anim_opa_cb,
              lv_obj_get_style_opa(s_lbl_clock, LV_PART_MAIN), LV_OPA_TRANSP,
              ANIM_QUICK_MS, anim_hide_done);

    lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_lbl_station, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_lbl_speaker, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_style_opa(s_logo_container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(s_lbl_station, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_opa(s_lbl_speaker, LV_OPA_TRANSP, LV_PART_MAIN);

    anim_fade(s_logo_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
              ANIM_FADE_MS);
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
  backlight_fade_to(BACKLIGHT_NORMAL);
  lv_anim_delete(s_vol_arc, anim_arc_ind_opa_cb);
  lv_obj_set_style_arc_opa(s_vol_arc, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_arc_set_value(s_vol_arc, level);
  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_ACTIVE, LV_PART_INDICATOR);
  lv_timer_reset(s_vol_hide_timer);
  lv_timer_resume(s_vol_hide_timer);
}

// ─── Mode Switching
// ─────────────────────────────────────────────────────────────────

static void enter_browse() {
  s_mode = Mode::Browse;
  s_was_playing = (s_play_state == PlayState::Playing);
  s_browse_index = s_station_index;

  lv_obj_set_style_text_color(s_lbl_station, COL_TEXT, LV_PART_MAIN);

  if (s_was_playing) {
    lv_anim_delete(s_logo_container, anim_opa_cb);
    anim_fade(s_logo_container, anim_opa_cb, LV_OPA_COVER, LV_OPA_70,
              ANIM_FADE_MS);
  } else {
    show_idle_ui(false);
    lv_anim_delete(s_logo_container, anim_opa_cb);
    anim_fade(s_logo_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_70,
              ANIM_FADE_MS);
  }

  lv_label_set_text(s_lbl_subtitle, "Tap to play");
  lv_obj_set_style_text_color(s_lbl_subtitle, lv_color_hex(0xBBBBBB),
                              LV_PART_MAIN);
  lv_anim_delete(s_lbl_subtitle, anim_opa_cb);
  lv_obj_set_style_opa(s_lbl_subtitle, LV_OPA_TRANSP, LV_PART_MAIN);
  anim_fade(s_lbl_subtitle, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
            ANIM_QUICK_MS);

  char pos[24];
  snprintf(pos, sizeof(pos), "%d / %d", s_browse_index + 1, STATION_COUNT);
  lv_label_set_text(s_lbl_position, pos);
  lv_obj_remove_flag(s_lbl_position, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(s_lbl_position, LV_OPA_TRANSP, LV_PART_MAIN);
  anim_fade(s_lbl_position, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
            ANIM_QUICK_MS);

  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_DIM, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_BG, LV_PART_MAIN);
  lv_timer_pause(s_vol_hide_timer);

  lv_timer_reset(s_browse_timer);
  lv_timer_resume(s_browse_timer);
}

// Called after fade-out completes during browse exit — swaps logo while hidden
static void on_exit_browse_fade_done(lv_anim_t *a) {
  set_logo(s_station_index);
  anim_fade(s_logo_container, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
            ANIM_FADE_MS);
}

static void exit_browse() {
  s_mode = Mode::Volume;

  lv_obj_set_style_text_color(s_lbl_station, COL_TEXT, LV_PART_MAIN);

  lv_label_set_text(s_lbl_station, STATIONS[s_station_index].name);
  update_subtitle();

  // Crossfade bg back to the playing station (only if it changed)
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

  lv_timer_pause(s_browse_timer);
}

static void confirm_browse() {
  s_station_index = s_browse_index;
  set_logo(s_station_index);
  // Crossfade bg to the newly confirmed station
  set_bg(s_station_index, true);
  int32_t idx = s_station_index;
  esp_event_post(APP_EVENT, APP_EVENT_STATION_CHANGED, &idx, sizeof(idx), 0);
  esp_event_post(APP_EVENT, APP_EVENT_PLAY_REQUESTED, nullptr, 0, 0);
  s_play_state = PlayState::Playing;
  s_was_playing = true;
  exit_browse();
}

static void on_browse_timeout(lv_timer_t *) { exit_browse(); }

// ─── Subtitle (play state)
// ────────────────────────────────────────────────────────────

static void update_subtitle() {
  const char *text;
  switch (s_play_state) {
  case PlayState::Playing:
    text = "Playing";
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

static void on_tap_delay(lv_timer_t *) {
  lv_timer_pause(s_tap_delay_timer);
  s_last_tap_ms = 0;
  do_tap();
}

static void activate_voice() {
  if (s_mode == Mode::Voice)
    return;
  if (s_mode == Mode::Browse)
    exit_browse();

  s_pre_voice_volume = s_volume;
  s_mode = Mode::Voice;
  voice_ui_enter();
  sonos_set_volume(VOICE_DUCKED_VOLUME);
  esp_event_post(APP_EVENT, APP_EVENT_VOICE_ACTIVATE, nullptr, 0, 0);
}

static void deactivate_voice() {
  if (s_mode != Mode::Voice)
    return;

  voice_ui_exit();
  s_mode = Mode::Volume;
  sonos_set_volume(s_pre_voice_volume);
  update_subtitle();
  esp_event_post(APP_EVENT, APP_EVENT_VOICE_DEACTIVATE, nullptr, 0, 0);
}

static void do_tap() {
  if (s_on_picker)
    return;

  switch (s_mode) {
  case Mode::Volume:
    enter_browse();
    break;
  case Mode::Browse:
    confirm_browse();
    break;
  case Mode::Voice:
    break;
  }
}

static void do_long_press() {
  if (s_on_picker)
    return;

  if (s_mode == Mode::Voice) {
    deactivate_voice();
    return;
  }

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
  lv_timer_reset(s_press_timer);
  lv_timer_resume(s_press_timer);
}

static void on_screen_released(lv_event_t *) {
  lv_timer_pause(s_press_timer);
  if (s_press_was_long)
    return;

  if (s_mode == Mode::Voice) {
    deactivate_voice();
    return;
  }

  uint32_t now = lv_tick_get();
  if (s_last_tap_ms && (now - s_last_tap_ms) < DOUBLE_TAP_WINDOW_MS) {
    s_last_tap_ms = 0;
    lv_timer_pause(s_tap_delay_timer);
    activate_voice();
  } else {
    s_last_tap_ms = now;
    lv_timer_reset(s_tap_delay_timer);
    lv_timer_resume(s_tap_delay_timer);
  }
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
  lv_obj_remove_flag(s_vol_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(s_vol_arc, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_BG, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_vol_arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_vol_arc, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_vol_arc, COL_ARC_ACTIVE, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_vol_arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_vol_arc, LV_OPA_TRANSP, LV_PART_KNOB);

  // ── Station logo — squircle card ──
  s_logo_container = lv_obj_create(parent);
  lv_obj_set_size(s_logo_container, 120, 120);
  lv_obj_set_style_radius(s_logo_container, 27, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_logo_container, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_logo_container, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_logo_container, 0, LV_PART_MAIN);
  lv_obj_set_style_clip_corner(s_logo_container, true, LV_PART_MAIN);
  lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_logo_container, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_align(s_logo_container, LV_ALIGN_CENTER, 0, -30);

  s_img_logo = lv_image_create(s_logo_container);
  lv_obj_set_size(s_img_logo, 120, 120);
  lv_image_set_inner_align(s_img_logo, LV_IMAGE_ALIGN_CENTER);
  lv_obj_set_pos(s_img_logo, 0, 0);
  set_logo(0);

  // ── Clock — large centered time, visible when idle ──
  s_lbl_clock = lv_label_create(parent);
  lv_obj_set_style_text_color(s_lbl_clock, COL_TEXT, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_clock, &geist_medium_52, LV_PART_MAIN);
  lv_obj_set_style_text_opa(s_lbl_clock, LV_OPA_90, LV_PART_MAIN);
  lv_label_set_text(s_lbl_clock, "00:00");
  lv_obj_align(s_lbl_clock, LV_ALIGN_CENTER, 0, -20);

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

  // ── WiFi dot — tiny status indicator, top center ──
  s_wifi_dot = lv_obj_create(s_screen);
  lv_obj_set_size(s_wifi_dot, 8, 8);
  lv_obj_set_style_radius(s_wifi_dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_wifi_dot, COL_TEXT_SEC, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_wifi_dot, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_wifi_dot, 0, LV_PART_MAIN);
  lv_obj_remove_flag(s_wifi_dot, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(s_wifi_dot, LV_ALIGN_TOP_MID, 0, 36);
}

// ─── Speaker Picker (first boot only)
// ───────────────────────────────────────────────

static void on_speaker_tap(lv_event_t *e) {
  auto index =
      static_cast<int>(reinterpret_cast<intptr_t>(lv_event_get_user_data(e)));
  if (index < 0 || index >= s_discovered.count)
    return;

  auto &speaker = s_discovered.speakers[index];
  sonos_set_speaker(speaker.ip, speaker.port);
  settings_set_speaker_name(speaker.name);
  sonos_start(); // Start network task (idempotent — safe if already running)
  lv_label_set_text(s_lbl_speaker, speaker.name);

  s_on_picker = false;
  lv_screen_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
  ESP_LOGI(TAG, "Speaker selected: %s (%s)", speaker.name, speaker.ip);
}

static void highlight_picker_item(int highlight) {
  int count = lv_obj_get_child_count(s_scr_speaker_picker);
  for (int i = 1; i < count; i++) {
    lv_obj_t *child = lv_obj_get_child(s_scr_speaker_picker, i);
    if (i - 1 == highlight) {
      lv_obj_set_style_border_color(child, COL_ACCENT, LV_PART_MAIN);
      lv_obj_set_style_border_width(child, 2, LV_PART_MAIN);
      lv_obj_scroll_to_view(child, LV_ANIM_ON);
    } else {
      lv_obj_set_style_border_color(child, COL_ARC_BG, LV_PART_MAIN);
      lv_obj_set_style_border_width(child, 1, LV_PART_MAIN);
    }
  }
}

static void on_skip_tap(lv_event_t *) {
  s_on_picker = false;
  lv_label_set_text(s_lbl_speaker, "No speaker");
  lv_screen_load_anim(s_screen, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 200, 0, false);
  ESP_LOGI(TAG, "Speaker picker skipped");
}

static void rebuild_speaker_list() {
  if (s_scr_speaker_picker)
    lv_obj_delete(s_scr_speaker_picker);

  s_scr_speaker_picker = lv_obj_create(nullptr);
  lv_obj_set_style_bg_color(s_scr_speaker_picker, COL_BG, LV_PART_MAIN);
  lv_obj_set_size(s_scr_speaker_picker, LCD_H_RES, LCD_V_RES);
  lv_obj_set_style_pad_top(s_scr_speaker_picker, 50, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(s_scr_speaker_picker, 50, LV_PART_MAIN);
  lv_obj_set_style_pad_left(s_scr_speaker_picker, 40, LV_PART_MAIN);
  lv_obj_set_style_pad_right(s_scr_speaker_picker, 40, LV_PART_MAIN);
  lv_obj_set_flex_flow(s_scr_speaker_picker, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_scr_speaker_picker, LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(s_scr_speaker_picker, 8, LV_PART_MAIN);

  lv_obj_t *title = lv_label_create(s_scr_speaker_picker);
  lv_obj_set_style_text_color(title, COL_TEXT, LV_PART_MAIN);
  lv_obj_set_style_text_font(title, &geist_regular_22, LV_PART_MAIN);
  lv_label_set_text(title, "Select Speaker");

  for (int i = 0; i < s_discovered.count; i++) {
    lv_obj_t *btn = lv_obj_create(s_scr_speaker_picker);
    lv_obj_set_size(btn, LCD_H_RES - 100, 52);
    lv_obj_set_style_bg_color(btn, COL_BG, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn, COL_ARC_BG, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 12, LV_PART_MAIN);
    lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(
        btn, COL_ACCENT,
        static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED));

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_color(lbl, COL_TEXT, LV_PART_MAIN);
    lv_label_set_text(lbl, s_discovered.speakers[i].name);
    lv_obj_center(lbl);

    lv_obj_add_event_cb(btn, on_speaker_tap, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(static_cast<intptr_t>(i)));
  }

  if (s_discovered.count == 0) {
    lv_obj_t *lbl = lv_label_create(s_scr_speaker_picker);
    lv_obj_set_style_text_color(lbl, COL_TEXT_SEC, LV_PART_MAIN);
    lv_label_set_text(lbl, "No speakers found.\nCheck your network.");
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  }

  // "Skip" button — always shown so user can dismiss picker
  lv_obj_t *skip_btn = lv_obj_create(s_scr_speaker_picker);
  lv_obj_set_size(skip_btn, LCD_H_RES - 100, 48);
  lv_obj_set_style_bg_color(skip_btn, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_border_color(skip_btn, COL_TEXT_SEC, LV_PART_MAIN);
  lv_obj_set_style_border_width(skip_btn, 1, LV_PART_MAIN);
  lv_obj_set_style_radius(skip_btn, 12, LV_PART_MAIN);
  lv_obj_remove_flag(skip_btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(skip_btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(
      skip_btn, COL_ACCENT,
      static_cast<lv_style_selector_t>(LV_PART_MAIN | LV_STATE_PRESSED));

  lv_obj_t *skip_lbl = lv_label_create(skip_btn);
  lv_obj_set_style_text_color(skip_lbl, COL_TEXT_SEC, LV_PART_MAIN);
  lv_label_set_text(skip_lbl, "Skip");
  lv_obj_center(skip_lbl);

  lv_obj_add_event_cb(skip_btn, on_skip_tap, LV_EVENT_CLICKED, nullptr);
}

// ─── Scanning Overlay
// ───────────────────────────────────────────────────────────────

static void build_scanning_overlay() {
  s_scanning_overlay = lv_obj_create(s_screen);
  lv_obj_set_size(s_scanning_overlay, LCD_H_RES, LCD_V_RES);
  lv_obj_set_style_bg_color(s_scanning_overlay, COL_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_scanning_overlay, LV_OPA_90, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_scanning_overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_scanning_overlay, 0, LV_PART_MAIN);
  lv_obj_remove_flag(s_scanning_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_scanning_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_scanning_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_align(s_scanning_overlay, LV_ALIGN_CENTER, 0, 0);

  s_lbl_scanning = lv_label_create(s_scanning_overlay);
  lv_obj_set_style_text_color(s_lbl_scanning, COL_TEXT_SEC, LV_PART_MAIN);
  lv_label_set_text(s_lbl_scanning, "Scanning…");
  lv_obj_center(s_lbl_scanning);
}

// ─── Public API
// ─────────────────────────────────────────────────────────────────────

void ui_init() {
  lv_display_t *disp = nullptr;
  lv_indev_t *touch = nullptr;
  display_init(&disp, &touch);

  s_volume = settings_get_volume();

  if (display_lock(200)) {
    build_main_screen();
    build_scanning_overlay();

    s_vol_hide_timer = lv_timer_create(on_vol_hide, VOL_DISPLAY_MS, nullptr);
    lv_timer_pause(s_vol_hide_timer);

    s_browse_timer =
        lv_timer_create(on_browse_timeout, BROWSE_TIMEOUT_MS, nullptr);
    lv_timer_pause(s_browse_timer);

    s_clock_timer = lv_timer_create(on_clock_tick, 30000, nullptr);

    s_bl_timer =
        lv_timer_create(on_backlight_step, BACKLIGHT_FADE_STEP_MS, nullptr);
    lv_timer_pause(s_bl_timer);

    s_press_timer = lv_timer_create(on_press_timer, 500, nullptr);
    lv_timer_pause(s_press_timer);

    s_tap_delay_timer =
        lv_timer_create(on_tap_delay, DOUBLE_TAP_WINDOW_MS, nullptr);
    lv_timer_pause(s_tap_delay_timer);

    voice_ui_build(s_screen);

    show_idle_ui(true);

    set_logo(s_station_index);
    set_bg(s_station_index, /*animate=*/false);

    char saved_name[64] = {};
    settings_get_speaker_name(saved_name, sizeof(saved_name));
    if (saved_name[0])
      lv_label_set_text(s_lbl_speaker, saved_name);

    update_subtitle();
    lv_screen_load(s_screen);
    display_unlock();
  }

  ESP_LOGI(TAG, "UI ready");
}

void ui_set_volume(int level) {
  if (display_lock(50)) {
    s_volume = level;
    if (pages_is_home() && s_vol_arc)
      lv_arc_set_value(s_vol_arc, level);
    display_unlock();
  }
}

void ui_set_play_state(PlayState state) {
  if (state == s_play_state)
    return;
  if (display_lock(50)) {
    s_play_state = state;
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
  if (index == s_station_index)
    return;
  if (display_lock(50)) {
    s_station_index = index;
    if (s_mode == Mode::Volume) {
      lv_label_set_text(s_lbl_station, STATIONS[index].name);
      set_logo(index);
      set_bg(index, true);
    }
    display_unlock();
  }
}

void ui_set_wifi_status(bool connected) {
  if (display_lock(50)) {
    if (connected) {
      lv_obj_set_style_bg_color(s_wifi_dot, COL_GREEN, LV_PART_MAIN);
    } else {
      lv_obj_set_style_bg_color(s_wifi_dot, COL_TEXT_SEC, LV_PART_MAIN);
    }
    display_unlock();
  }
}

void ui_set_speaker_name(const char *name) {
  if (display_lock(50)) {
    lv_label_set_text(s_lbl_speaker, name);
    display_unlock();
  }
}

// ─── Voice Mode
// ─────────────────────────────────────────────────────────────────────

void ui_voice_activate() {
  if (!display_lock(50))
    return;
  activate_voice();
  display_unlock();
}

void ui_voice_deactivate() {
  if (!display_lock(50))
    return;
  deactivate_voice();
  display_unlock();
}

void ui_voice_set_state(VoiceState state) {
  if (!display_lock(50))
    return;
  voice_ui_set_state(state);
  display_unlock();
}

void ui_voice_set_transcript(const char *text, bool is_user) {
  if (!display_lock(50))
    return;
  voice_ui_set_transcript(text, is_user);
  display_unlock();
}

bool ui_is_voice_active() { return s_mode == Mode::Voice; }

static void on_page_changed(int index, const char *id) {
  (void)id;
  if (index == 0) {
    show_idle_ui(home_should_idle());
    update_subtitle();
  }
}

void ui_on_encoder_rotate(int32_t steps) {
  if (!display_lock(50))
    return;

  if (s_on_picker) {
    if (s_discovered.count > 0) {
      s_speaker_highlight =
          std::clamp(s_speaker_highlight + static_cast<int>(steps), 0,
                     s_discovered.count - 1);
      highlight_picker_item(s_speaker_highlight);
    }
    display_unlock();
    return;
  }

  if (s_mode == Mode::Voice) {
    display_unlock();
    return;
  }

  // If we're on a live view page, encoder navigates between pages
  if (!pages_is_home()) {
    pages_navigate(steps > 0 ? 1 : -1);
    pages_poke();
    display_unlock();
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
    int32_t vol = s_volume;
    esp_event_post(APP_EVENT, APP_EVENT_VOLUME_CHANGED, &vol, sizeof(vol), 0);
    sonos_set_volume(s_volume);
    break;
  }
  case Mode::Browse: {
    int new_idx = s_browse_index + static_cast<int>(steps);
    s_browse_index =
        ((new_idx % STATION_COUNT) + STATION_COUNT) % STATION_COUNT;
    lv_label_set_text(s_lbl_station, STATIONS[s_browse_index].name);

    lv_anim_delete(s_logo_container, anim_opa_cb);
    anim_fade(s_logo_container, anim_opa_cb,
              lv_obj_get_style_opa(s_logo_container, LV_PART_MAIN),
              LV_OPA_TRANSP, ANIM_QUICK_MS / 2, on_browse_rotate_fade_done);

    lv_anim_delete(s_lbl_station, anim_opa_cb);
    lv_obj_set_style_opa(s_lbl_station, LV_OPA_TRANSP, LV_PART_MAIN);
    anim_fade(s_lbl_station, anim_opa_cb, LV_OPA_TRANSP, LV_OPA_COVER,
              ANIM_QUICK_MS);

    char pos[24];
    snprintf(pos, sizeof(pos), "%d / %d", s_browse_index + 1, STATION_COUNT);
    lv_label_set_text(s_lbl_position, pos);

    lv_timer_reset(s_browse_timer);
    break;
  }
  case Mode::Voice:
    break;
  }

  display_unlock();
}

void ui_on_touch_tap() {
  if (!display_lock(50))
    return;
  pages_poke();
  if (s_mode == Mode::Voice)
    deactivate_voice();
  else if (!pages_is_home())
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
    lv_obj_remove_flag(s_scanning_overlay, LV_OBJ_FLAG_HIDDEN);
    display_unlock();
  }
}

void ui_show_speaker_picker(const DiscoveryResult *speakers) {
  if (display_lock(200)) {
    lv_obj_add_flag(s_scanning_overlay, LV_OBJ_FLAG_HIDDEN);
    memcpy(&s_discovered, speakers, sizeof(s_discovered));
    s_speaker_highlight = 0;
    rebuild_speaker_list();
    s_on_picker = true;
    lv_screen_load_anim(s_scr_speaker_picker, LV_SCR_LOAD_ANIM_MOVE_LEFT, 200,
                        0, false);
    display_unlock();
  }
}
