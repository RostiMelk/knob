#include "ui_timer.h"
#include "fonts/fonts.h"
#include "ui_pages.h"

#include "app_config.h"
#include "timer/timer.h"

#include "esp_log.h"

#include <cstdio>
#include <cstring>

static constexpr const char *TAG = "ui_timer";
static constexpr const char *PAGE_ID = "timer";
static constexpr int TIMER_PAGE_PRIORITY = 10;

#define COL_T_BG lv_color_hex(0x000000)
#define COL_T_TEXT lv_color_hex(0xFFFFFF)
#define COL_T_LABEL lv_color_hex(0x8E8E93)
#define COL_T_ARC lv_color_hex(0xFF9F0A)
#define COL_T_ARC_BG lv_color_hex(0x1C1C1E)
#define COL_T_DONE lv_color_hex(0x30D158)

// ─── State ──────────────────────────────────────────────────────────────────

static lv_obj_t *s_arc;
static lv_obj_t *s_lbl_time;
static lv_obj_t *s_lbl_label;
static lv_obj_t *s_lbl_done;

static int s_total_seconds;
static bool s_registered;
static bool s_fired;
static int s_last_rem;

// ─── Formatting ─────────────────────────────────────────────────────────────

static void fmt_mmss(char *buf, size_t len, int seconds) {
  if (seconds <= 0) {
    snprintf(buf, len, "0:00");
    return;
  }
  int m = seconds / 60;
  int s = seconds % 60;
  if (m >= 10)
    snprintf(buf, len, "%02d:%02d", m, s);
  else
    snprintf(buf, len, "%d:%02d", m, s);
}

// ─── Arc Animation ──────────────────────────────────────────────────────────

static void anim_arc_value_cb(void *obj, int32_t v) {
  lv_arc_set_value(static_cast<lv_obj_t *>(obj), v);
}

static void arc_animate_to(int remaining_sec) {
  if (!s_arc || s_total_seconds <= 0)
    return;

  int32_t target = (remaining_sec * 1000) / s_total_seconds;
  int32_t cur = lv_arc_get_value(s_arc);

  lv_anim_delete(s_arc, anim_arc_value_cb);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_arc);
  lv_anim_set_exec_cb(&a, anim_arc_value_cb);
  lv_anim_set_values(&a, cur, target);
  lv_anim_set_duration(&a, 1000);
  lv_anim_set_path_cb(&a, lv_anim_path_linear);
  lv_anim_start(&a);
}

// ─── Page Build ─────────────────────────────────────────────────────────────

static void page_build(lv_obj_t *parent) {
  lv_obj_set_style_bg_color(parent, COL_T_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, LV_PART_MAIN);

  // Edge arc — same size/position as volume arc on home page
  s_arc = lv_arc_create(parent);
  lv_obj_set_size(s_arc, LCD_H_RES - 4, LCD_V_RES - 4);
  lv_obj_center(s_arc);
  lv_arc_set_rotation(s_arc, 270);
  lv_arc_set_bg_angles(s_arc, 0, 360);
  lv_arc_set_range(s_arc, 0, 1000);
  lv_arc_set_value(s_arc, 1000);
  lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(s_arc, 6, LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_arc, COL_T_ARC_BG, LV_PART_MAIN);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_arc, 6, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_arc, COL_T_ARC, LV_PART_INDICATOR);
  lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);

  // Countdown text
  s_lbl_time = lv_label_create(parent);
  lv_obj_set_style_text_color(s_lbl_time, COL_T_TEXT, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_time, &geist_medium_52, LV_PART_MAIN);
  lv_obj_set_style_text_align(s_lbl_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_width(s_lbl_time, 200);
  lv_label_set_text(s_lbl_time, "0:00");
  lv_obj_align(s_lbl_time, LV_ALIGN_CENTER, 0, -10);

  // Label text (below countdown)
  s_lbl_label = lv_label_create(parent);
  lv_obj_set_style_text_color(s_lbl_label, COL_T_LABEL, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_label, &geist_regular_18, LV_PART_MAIN);
  lv_obj_set_style_text_align(s_lbl_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
  lv_obj_set_width(s_lbl_label, LCD_H_RES - 80);
  lv_label_set_long_mode(s_lbl_label, LV_LABEL_LONG_DOT);
  lv_label_set_text(s_lbl_label, "");
  lv_obj_align(s_lbl_label, LV_ALIGN_CENTER, 0, 30);

  // "Done!" text (hidden until timer fires)
  s_lbl_done = lv_label_create(parent);
  lv_obj_set_style_text_color(s_lbl_done, COL_T_DONE, LV_PART_MAIN);
  lv_obj_set_style_text_font(s_lbl_done, &geist_medium_28, LV_PART_MAIN);
  lv_label_set_text(s_lbl_done, "Done!");
  lv_obj_align(s_lbl_done, LV_ALIGN_CENTER, 0, -10);
  lv_obj_add_flag(s_lbl_done, LV_OBJ_FLAG_HIDDEN);

  // Apply initial state
  s_fired = false;

  char label_buf[64];
  timer_get_label(label_buf, sizeof(label_buf));
  lv_label_set_text(s_lbl_label, label_buf);

  int rem = timer_remaining_sec();
  s_last_rem = rem;

  char time_buf[16];
  fmt_mmss(time_buf, sizeof(time_buf), rem);
  lv_label_set_text(s_lbl_time, time_buf);

  if (s_total_seconds > 0) {
    int progress = (rem * 1000) / s_total_seconds;
    lv_arc_set_value(s_arc, progress);
  }
}

static void page_destroy() {
  if (s_arc)
    lv_anim_delete(s_arc, anim_arc_value_cb);
  s_arc = nullptr;
  s_lbl_time = nullptr;
  s_lbl_label = nullptr;
  s_lbl_done = nullptr;
}

// ─── Page Tick ──────────────────────────────────────────────────────────────

static void page_tick() {
  if (!s_lbl_time)
    return;

  int rem = timer_remaining_sec();

  if (rem <= 0 && !s_fired) {
    s_fired = true;
    lv_label_set_text(s_lbl_time, "0:00");

    lv_anim_delete(s_arc, anim_arc_value_cb);
    lv_arc_set_value(s_arc, 0);
    lv_obj_set_style_arc_color(s_arc, COL_T_DONE, LV_PART_INDICATOR);

    lv_obj_remove_flag(s_lbl_done, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_lbl_time, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  if (rem <= 0)
    return;

  if (rem != s_last_rem) {
    s_last_rem = rem;

    char buf[16];
    fmt_mmss(buf, sizeof(buf), rem);
    lv_label_set_text(s_lbl_time, buf);

    arc_animate_to(rem);
  }
}

// ─── Page Definition ────────────────────────────────────────────────────────

static const PageDef s_page_def = {
    .id = PAGE_ID,
    .build = page_build,
    .destroy = page_destroy,
    .tick = page_tick,
};

// ─── Public API ─────────────────────────────────────────────────────────────

void ui_timer_init() {
  s_registered = false;
  s_fired = false;
  s_total_seconds = 0;
  s_last_rem = 0;
  s_arc = nullptr;
  s_lbl_time = nullptr;
  s_lbl_label = nullptr;
  s_lbl_done = nullptr;
}

void ui_timer_show(int seconds, const char *label) {
  s_total_seconds = seconds;
  s_fired = false;
  s_last_rem = seconds;

  if (!s_registered) {
    pages_add(&s_page_def, TIMER_PAGE_PRIORITY);
    s_registered = true;
    ESP_LOGI(TAG, "Timer page added: %ds '%s'", seconds, label ? label : "");
  } else {
    if (s_lbl_time) {
      char buf[16];
      fmt_mmss(buf, sizeof(buf), seconds);
      lv_label_set_text(s_lbl_time, buf);
      lv_obj_remove_flag(s_lbl_time, LV_OBJ_FLAG_HIDDEN);
    }
    if (s_lbl_label && label)
      lv_label_set_text(s_lbl_label, label);
    if (s_lbl_done)
      lv_obj_add_flag(s_lbl_done, LV_OBJ_FLAG_HIDDEN);
    if (s_arc) {
      lv_anim_delete(s_arc, anim_arc_value_cb);
      lv_arc_set_value(s_arc, 1000);
      lv_obj_set_style_arc_color(s_arc, COL_T_ARC, LV_PART_INDICATOR);
    }
  }

  pages_go_to(PAGE_ID);
}

void ui_timer_dismiss() {
  if (!s_registered)
    return;

  pages_remove(PAGE_ID);
  s_registered = false;
  s_total_seconds = 0;
  s_fired = false;
  s_last_rem = 0;

  ESP_LOGI(TAG, "Timer page removed");
}

bool ui_timer_is_visible() {
  return s_registered && pages_current_index() == pages_find(PAGE_ID);
}

const char *ui_timer_page_id() { return PAGE_ID; }
