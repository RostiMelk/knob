#include "ui_voice.h"
#include "fonts/fonts.h"
#include "ui.h"

#include "app_config.h"
#include "esp_log.h"
#include "lvgl.h"

#include <cstdio>
#include <cstring>

static constexpr const char *TAG = "ui_voice";

// ─── Palette ────────────────────────────────────────────────────────────────

#define COL_V_BG lv_color_hex(0x000000)
#define COL_V_BLUE lv_color_hex(0x0A84FF)
#define COL_V_INDIGO lv_color_hex(0x5E5CE6)
#define COL_V_PURPLE lv_color_hex(0xBF5AF2)
#define COL_V_GRAY lv_color_hex(0x8E8E93)
#define COL_V_TEXT lv_color_hex(0xFFFFFF)
#define COL_V_DIM lv_color_hex(0x48484A)

// ─── Timing ─────────────────────────────────────────────────────────────────

static constexpr int ANIM_ENTER_MS = 300;
static constexpr int ANIM_EXIT_MS = 250;
static constexpr int ANIM_STATE_MS = 200;
static constexpr int HINT_VISIBLE_MS = 4000;
static constexpr int HINT_FADE_MS = 600;

// ─── State ──────────────────────────────────────────────────────────────────

static VoiceState s_state = VoiceState::Inactive;

static lv_obj_t *s_overlay;
static lv_obj_t *s_orb;
static lv_obj_t *s_lbl_status;
static lv_obj_t *s_lbl_transcript;
static lv_obj_t *s_lbl_hint;
static lv_timer_t *s_idle_timer;

#ifdef SIMULATOR
static lv_timer_t *s_sim_timer;
static int s_sim_phase;
#endif

// ─── Animation Callbacks ────────────────────────────────────────────────────

static void anim_opa_cb(void *obj, int32_t v) {
  lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), v, LV_PART_MAIN);
}

static void anim_shadow_spread_cb(void *obj, int32_t v) {
  lv_obj_set_style_shadow_spread(static_cast<lv_obj_t *>(obj), v, LV_PART_MAIN);
}

static void anim_shadow_opa_cb(void *obj, int32_t v) {
  lv_obj_set_style_shadow_opa(static_cast<lv_obj_t *>(obj), v, LV_PART_MAIN);
}

static void anim_orb_size_cb(void *obj, int32_t v) {
  auto *o = static_cast<lv_obj_t *>(obj);
  lv_obj_set_size(o, v, v);
  lv_obj_align(o, LV_ALIGN_CENTER, 0, -25);
}

// ─── Pulse ──────────────────────────────────────────────────────────────────

static void stop_pulse() {
  lv_anim_delete(s_orb, anim_shadow_spread_cb);
  lv_anim_delete(s_orb, anim_shadow_opa_cb);
}

static void start_pulse(int spread_lo, int spread_hi, int opa_lo, int opa_hi,
                        int period_ms) {
  stop_pulse();

  lv_anim_t a;

  lv_anim_init(&a);
  lv_anim_set_var(&a, s_orb);
  lv_anim_set_exec_cb(&a, anim_shadow_spread_cb);
  lv_anim_set_values(&a, spread_lo, spread_hi);
  lv_anim_set_duration(&a, period_ms);
  lv_anim_set_playback_duration(&a, period_ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);

  lv_anim_init(&a);
  lv_anim_set_var(&a, s_orb);
  lv_anim_set_exec_cb(&a, anim_shadow_opa_cb);
  lv_anim_set_values(&a, opa_lo, opa_hi);
  lv_anim_set_duration(&a, period_ms);
  lv_anim_set_playback_duration(&a, period_ms);
  lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

// ─── Idle Timeout ───────────────────────────────────────────────────────────

static void on_idle_timeout(lv_timer_t *) {
  lv_timer_pause(s_idle_timer);
  ESP_LOGI(TAG, "Idle timeout — auto-exiting voice mode");
  ui_voice_deactivate();
}

// ─── State Application ─────────────────────────────────────────────────────

static void apply_state(VoiceState state) {
  s_state = state;

  stop_pulse();
  lv_anim_delete(s_orb, anim_orb_size_cb);

  int target_size = 24;
  lv_color_t color = COL_V_GRAY;
  int shadow_w = 40;
  const char *status = "";

  switch (state) {
  case VoiceState::Connecting:
    target_size = 24;
    color = COL_V_GRAY;
    shadow_w = 40;
    status = "Connecting...";
    start_pulse(2, 8, 60, 150, 800);
    lv_timer_pause(s_idle_timer);
    break;

  case VoiceState::Listening:
    target_size = 80;
    color = COL_V_BLUE;
    shadow_w = 60;
    status = "Listening...";
    start_pulse(5, 20, 100, 200, 1200);
    lv_timer_reset(s_idle_timer);
    lv_timer_resume(s_idle_timer);
    break;

  case VoiceState::Thinking:
    target_size = 60;
    color = COL_V_INDIGO;
    shadow_w = 50;
    status = "";
    start_pulse(8, 18, 120, 220, 600);
    lv_timer_pause(s_idle_timer);
    break;

  case VoiceState::Speaking:
    target_size = 100;
    color = COL_V_PURPLE;
    shadow_w = 70;
    status = "";
    start_pulse(10, 30, 130, 230, 400);
    lv_timer_pause(s_idle_timer);
    break;

  default:
    return;
  }

  lv_obj_set_style_bg_color(s_orb, color, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(s_orb, color, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(s_orb, shadow_w, LV_PART_MAIN);

  int cur = lv_obj_get_width(s_orb);
  if (cur != target_size) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_orb);
    lv_anim_set_exec_cb(&a, anim_orb_size_cb);
    lv_anim_set_values(&a, cur, target_size);
    lv_anim_set_duration(&a, ANIM_STATE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }

  lv_label_set_text(s_lbl_status, status);
}

// ─── Simulator Demo ─────────────────────────────────────────────────────────

#ifdef SIMULATOR
static void on_sim_tick(lv_timer_t *) {
  s_sim_phase++;

  switch (s_sim_phase) {
  case 1:
    voice_ui_set_state(VoiceState::Listening);
    lv_timer_set_period(s_sim_timer, 3000);
    break;

  case 2:
    voice_ui_set_transcript("What's the weather in Oslo?", true);
    voice_ui_set_state(VoiceState::Thinking);
    lv_timer_set_period(s_sim_timer, 1500);
    break;

  case 3:
    voice_ui_set_state(VoiceState::Speaking);
    voice_ui_set_transcript(
        "It's 18 degrees and partly cloudy in Oslo right now.", false);
    lv_timer_set_period(s_sim_timer, 3000);
    break;

  case 4:
    voice_ui_set_state(VoiceState::Listening);
    voice_ui_set_transcript("", false);
    lv_timer_pause(s_sim_timer);
    break;

  default:
    lv_timer_pause(s_sim_timer);
    break;
  }
}
#endif

// ─── Public API ─────────────────────────────────────────────────────────────

void voice_ui_build(lv_obj_t *parent) {
  s_overlay = lv_obj_create(parent);
  lv_obj_set_size(s_overlay, LCD_H_RES, LCD_V_RES);
  lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(s_overlay, COL_V_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_overlay, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_overlay, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_overlay, 0, LV_PART_MAIN);
  lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_style_opa(s_overlay, LV_OPA_TRANSP, LV_PART_MAIN);

  // ── Status label (top) ──
  s_lbl_status = lv_label_create(s_overlay);
  lv_obj_set_style_text_color(s_lbl_status, COL_V_GRAY, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_status, &geist_regular_16, LV_PART_MAIN);
  lv_obj_set_style_text_align(s_lbl_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_label_set_text(s_lbl_status, "");
  lv_obj_align(s_lbl_status, LV_ALIGN_TOP_MID, 0, 75);

  // ── Orb (center) ──
  s_orb = lv_obj_create(s_overlay);
  lv_obj_set_size(s_orb, 24, 24);
  lv_obj_set_style_radius(s_orb, LV_RADIUS_CIRCLE, LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_orb, COL_V_BLUE, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_orb, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_orb, 0, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(s_orb, COL_V_BLUE, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(s_orb, 60, LV_PART_MAIN);
  lv_obj_set_style_shadow_spread(s_orb, 5, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(s_orb, 100, LV_PART_MAIN);
  lv_obj_remove_flag(s_orb, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(s_orb, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(s_orb, LV_ALIGN_CENTER, 0, -25);

  // ── Transcript (below orb) ──
  s_lbl_transcript = lv_label_create(s_overlay);
  lv_obj_set_style_text_color(s_lbl_transcript, COL_V_TEXT, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_transcript, &geist_regular_16, LV_PART_MAIN);
  lv_obj_set_style_text_align(s_lbl_transcript, LV_TEXT_ALIGN_CENTER,
                              LV_PART_MAIN);
  lv_obj_set_width(s_lbl_transcript, LCD_H_RES - 60);
  lv_label_set_long_mode(s_lbl_transcript, LV_LABEL_LONG_WRAP);
  lv_label_set_text(s_lbl_transcript, "");
  lv_obj_align(s_lbl_transcript, LV_ALIGN_CENTER, 0, 85);
  lv_obj_set_style_opa(s_lbl_transcript, LV_OPA_TRANSP, LV_PART_MAIN);

  // ── Hint (bottom) ──
  s_lbl_hint = lv_label_create(s_overlay);
  lv_obj_set_style_text_color(s_lbl_hint, COL_V_DIM, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_hint, &geist_regular_16, LV_PART_MAIN);
  lv_label_set_text(s_lbl_hint, "Exits automatically");
  lv_obj_align(s_lbl_hint, LV_ALIGN_BOTTOM_MID, 0, -55);

  // ── Timers ──
  s_idle_timer =
      lv_timer_create(on_idle_timeout, VOICE_IDLE_TIMEOUT_MS, nullptr);
  lv_timer_pause(s_idle_timer);

#ifdef SIMULATOR
  s_sim_timer = lv_timer_create(on_sim_tick, 800, nullptr);
  lv_timer_pause(s_sim_timer);
#endif
}

void voice_ui_enter() {
  if (s_state != VoiceState::Inactive)
    return;

  lv_obj_set_size(s_orb, 24, 24);
  lv_obj_align(s_orb, LV_ALIGN_CENTER, 0, -25);
  lv_obj_set_style_bg_color(s_orb, COL_V_GRAY, LV_PART_MAIN);
  lv_obj_set_style_shadow_color(s_orb, COL_V_GRAY, LV_PART_MAIN);
  lv_obj_set_style_shadow_width(s_orb, 40, LV_PART_MAIN);
  lv_obj_set_style_shadow_spread(s_orb, 5, LV_PART_MAIN);
  lv_obj_set_style_shadow_opa(s_orb, 100, LV_PART_MAIN);
  lv_label_set_text(s_lbl_transcript, "");
  lv_obj_set_style_opa(s_lbl_transcript, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_opa(s_lbl_hint, LV_OPA_COVER, LV_PART_MAIN);

  lv_obj_move_to_index(s_overlay, -1);
  lv_obj_remove_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);

  lv_anim_t a;

  lv_anim_init(&a);
  lv_anim_set_var(&a, s_overlay);
  lv_anim_set_exec_cb(&a, anim_opa_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, ANIM_ENTER_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);

  lv_anim_delete(s_lbl_hint, anim_opa_cb);
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_lbl_hint);
  lv_anim_set_exec_cb(&a, anim_opa_cb);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_duration(&a, HINT_FADE_MS);
  lv_anim_set_delay(&a, HINT_VISIBLE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);

  apply_state(VoiceState::Connecting);

  ESP_LOGI(TAG, "Voice mode entered");

#ifdef SIMULATOR
  s_sim_phase = 0;
  lv_timer_set_period(s_sim_timer, 800);
  lv_timer_reset(s_sim_timer);
  lv_timer_resume(s_sim_timer);
#endif
}

static void on_exit_done(lv_anim_t *a) {
  lv_obj_add_flag(static_cast<lv_obj_t *>(a->var), LV_OBJ_FLAG_HIDDEN);
}

void voice_ui_exit() {
  if (s_state == VoiceState::Inactive)
    return;

  stop_pulse();
  lv_anim_delete(s_orb, anim_orb_size_cb);
  lv_anim_delete(s_lbl_hint, anim_opa_cb);
  lv_anim_delete(s_lbl_transcript, anim_opa_cb);
  lv_timer_pause(s_idle_timer);

#ifdef SIMULATOR
  lv_timer_pause(s_sim_timer);
#endif

  s_state = VoiceState::Inactive;

  lv_anim_delete(s_overlay, anim_opa_cb);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_overlay);
  lv_anim_set_exec_cb(&a, anim_opa_cb);
  lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
  lv_anim_set_duration(&a, ANIM_EXIT_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_completed_cb(&a, on_exit_done);
  lv_anim_start(&a);

  ESP_LOGI(TAG, "Voice mode exited");
}

void voice_ui_set_state(VoiceState state) {
  if (state == VoiceState::Inactive) {
    voice_ui_exit();
    return;
  }
  apply_state(state);
}

void voice_ui_set_transcript(const char *text, bool is_user) {
  if (!text || !text[0]) {
    lv_anim_delete(s_lbl_transcript, anim_opa_cb);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_lbl_transcript);
    lv_anim_set_exec_cb(&a, anim_opa_cb);
    int32_t cur = lv_obj_get_style_opa(s_lbl_transcript, LV_PART_MAIN);
    lv_anim_set_values(&a, cur, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 150);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
    return;
  }

  lv_obj_set_style_text_color(s_lbl_transcript,
                              is_user ? COL_V_GRAY : COL_V_TEXT, LV_PART_MAIN);
  lv_label_set_text(s_lbl_transcript, text);

  lv_anim_delete(s_lbl_transcript, anim_opa_cb);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_lbl_transcript);
  lv_anim_set_exec_cb(&a, anim_opa_cb);
  lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
  lv_anim_set_duration(&a, ANIM_STATE_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_start(&a);
}

void voice_ui_tick() {
  // Reserved for future mic-amplitude reactive orb sizing
}
