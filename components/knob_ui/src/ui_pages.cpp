#include "ui_pages.h"
#include "hal_pins.h"

#include "esp_log.h"

#include <algorithm>
#include <cstring>

static constexpr const char *TAG = "ui_pages";

static constexpr int INACTIVITY_MS = 7000;
static constexpr int SLIDE_ANIM_MS = 250;
static constexpr int DOT_SIZE = 6;
static constexpr int DOT_GAP = 8;
static constexpr int DOT_BOTTOM_PAD = 24;

#define COL_DOT_ACTIVE lv_color_hex(0xFFFFFF)
#define COL_DOT_INACTIVE lv_color_hex(0x48484A)

// ─── Slot ───────────────────────────────────────────────────────────────────

struct PageSlot {
  PageDef def;
  int priority;
  lv_obj_t *container;
  bool live;
};

// ─── State ──────────────────────────────────────────────────────────────────

static lv_obj_t *s_parent;
static lv_obj_t *s_strip;
static lv_obj_t *s_dot_row;
static lv_timer_t *s_idle_timer;
static lv_timer_t *s_tick_timer;

static PageChangedCb s_on_changed;

static PageSlot s_slots[PAGES_MAX];
static int s_count;
static int s_current;
static bool s_animating;

// ─── Forward Declarations ───────────────────────────────────────────────────

static void rebuild_strip();
static void update_dots();
static void slide_to(int index, bool animate);
static int find_idle_target();

// ─── Animation ──────────────────────────────────────────────────────────────

static void anim_strip_x_cb(void *obj, int32_t v) {
  lv_obj_set_x(static_cast<lv_obj_t *>(obj), v);
}

static void anim_done_cb(lv_anim_t *) { s_animating = false; }

static void anim_dot_opa_cb(void *obj, int32_t v) {
  lv_obj_set_style_opa(static_cast<lv_obj_t *>(obj), v, LV_PART_MAIN);
}

// ─── Dots ───────────────────────────────────────────────────────────────────

static void rebuild_dots() {
  if (!s_dot_row)
    return;

  lv_obj_clean(s_dot_row);

  if (s_count <= 1) {
    lv_obj_add_flag(s_dot_row, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  lv_obj_remove_flag(s_dot_row, LV_OBJ_FLAG_HIDDEN);

  for (int i = 0; i < s_count; i++) {
    lv_obj_t *dot = lv_obj_create(s_dot_row);
    lv_obj_set_size(dot, DOT_SIZE, DOT_SIZE);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(
        dot, i == s_current ? COL_DOT_ACTIVE : COL_DOT_INACTIVE, LV_PART_MAIN);
    lv_obj_set_style_opa(dot, i == s_current ? LV_OPA_COVER : LV_OPA_60,
                         LV_PART_MAIN);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
  }
}

static void update_dots() {
  if (!s_dot_row || s_count <= 1)
    return;

  for (int i = 0; i < s_count; i++) {
    lv_obj_t *dot = lv_obj_get_child(s_dot_row, i);
    if (!dot)
      continue;

    bool active = (i == s_current);

    lv_anim_delete(dot, anim_dot_opa_cb);
    lv_obj_set_style_bg_color(dot, active ? COL_DOT_ACTIVE : COL_DOT_INACTIVE,
                              LV_PART_MAIN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, dot);
    lv_anim_set_exec_cb(&a, anim_dot_opa_cb);
    int32_t cur = lv_obj_get_style_opa(dot, LV_PART_MAIN);
    int32_t target = active ? LV_OPA_COVER : LV_OPA_60;
    lv_anim_set_values(&a, cur, target);
    lv_anim_set_duration(&a, SLIDE_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
  }
}

// ─── Strip Layout ───────────────────────────────────────────────────────────
//
// The strip is a wide horizontal container whose children are page containers
// laid out side-by-side, each LCD_H_RES wide. Sliding the strip's x position
// by -index * LCD_H_RES brings the desired page into view.

static void rebuild_strip() {
  if (!s_strip)
    return;

  lv_obj_clean(s_strip);
  lv_obj_set_size(s_strip, LCD_H_RES * s_count, LCD_V_RES);

  for (int i = 0; i < s_count; i++) {
    lv_obj_t *page = lv_obj_create(s_strip);
    lv_obj_set_size(page, LCD_H_RES, LCD_V_RES);
    lv_obj_set_pos(page, LCD_H_RES * i, 0);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(page, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(page, 0, LV_PART_MAIN);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(page, LV_OBJ_FLAG_CLICKABLE);

    s_slots[i].container = page;

    if (s_slots[i].def.build)
      s_slots[i].def.build(page);
  }

  slide_to(s_current, false);
  rebuild_dots();
}

// ─── Slide ──────────────────────────────────────────────────────────────────

static void slide_to(int index, bool animate) {
  if (index < 0 || index >= s_count)
    return;

  int prev = s_current;
  s_current = index;
  int32_t target_x = -index * LCD_H_RES;

  if (!animate || !s_strip) {
    if (s_strip)
      lv_obj_set_x(s_strip, target_x);
    s_animating = false;
    update_dots();
    return;
  }

  int32_t cur_x = lv_obj_get_x(s_strip);
  if (cur_x == target_x) {
    update_dots();
    return;
  }

  s_animating = true;
  lv_anim_delete(s_strip, anim_strip_x_cb);

  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, s_strip);
  lv_anim_set_exec_cb(&a, anim_strip_x_cb);
  lv_anim_set_values(&a, cur_x, target_x);
  lv_anim_set_duration(&a, SLIDE_ANIM_MS);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
  lv_anim_set_completed_cb(&a, anim_done_cb);
  lv_anim_start(&a);

  update_dots();

  if (prev != s_current && s_on_changed)
    s_on_changed(s_current, s_slots[s_current].def.id);
}

// ─── Idle Timeout ───────────────────────────────────────────────────────────

static int find_idle_target() {
  for (int i = 1; i < s_count; i++) {
    if (s_slots[i].live)
      return i;
  }
  return 0;
}

static void on_idle_timeout(lv_timer_t *) {
  lv_timer_pause(s_idle_timer);

  int target = find_idle_target();
  if (target != s_current) {
    ESP_LOGI(TAG, "Idle timeout → slide to %s (page %d)",
             s_slots[target].def.id, target);
    slide_to(target, true);
  }
}

// ─── Tick Timer ─────────────────────────────────────────────────────────────

static void on_tick(lv_timer_t *) {
  if (s_current >= 0 && s_current < s_count) {
    auto tick_fn = s_slots[s_current].def.tick;
    if (tick_fn)
      tick_fn();
  }
}

// ─── Sorted Insert Position ─────────────────────────────────────────────────

static int insert_pos_for(int priority) {
  for (int i = 1; i < s_count; i++) {
    if (s_slots[i].priority > priority)
      return i;
  }
  return s_count;
}

// ─── Public API ─────────────────────────────────────────────────────────────

void pages_init(lv_obj_t *parent, const PageDef *home,
                PageChangedCb on_changed) {
  s_parent = parent;
  s_on_changed = on_changed;
  s_count = 0;
  s_current = 0;
  s_animating = false;

  // Strip: wide container that holds all pages side-by-side
  s_strip = lv_obj_create(parent);
  lv_obj_set_style_bg_opa(s_strip, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_strip, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_strip, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(s_strip, 0, LV_PART_MAIN);
  lv_obj_remove_flag(s_strip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_strip, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_pos(s_strip, 0, 0);

  // Dot indicator row — sits on top of everything
  s_dot_row = lv_obj_create(parent);
  lv_obj_set_style_bg_opa(s_dot_row, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_border_width(s_dot_row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_all(s_dot_row, 0, LV_PART_MAIN);
  lv_obj_set_style_pad_column(s_dot_row, DOT_GAP, LV_PART_MAIN);
  lv_obj_set_size(s_dot_row, LCD_H_RES, DOT_SIZE + 4);
  lv_obj_set_flex_flow(s_dot_row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_dot_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                        LV_FLEX_ALIGN_CENTER);
  lv_obj_align(s_dot_row, LV_ALIGN_BOTTOM_MID, 0, -DOT_BOTTOM_PAD);
  lv_obj_remove_flag(s_dot_row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_dot_row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_dot_row, LV_OBJ_FLAG_HIDDEN);

  // Register home as page 0
  s_slots[0].def = *home;
  s_slots[0].priority = 0;
  s_slots[0].container = nullptr;
  s_slots[0].live = false;
  s_count = 1;

  rebuild_strip();

  // Idle timer
  s_idle_timer = lv_timer_create(on_idle_timeout, INACTIVITY_MS, nullptr);
  lv_timer_pause(s_idle_timer);

  // Tick timer (~100ms)
  s_tick_timer = lv_timer_create(on_tick, 100, nullptr);

  ESP_LOGI(TAG, "Pager initialized with home page '%s'", home->id);
}

int pages_add(const PageDef *def, int priority) {
  if (!def || !def->id || s_count >= PAGES_MAX) {
    ESP_LOGE(TAG, "Cannot add page (count=%d, max=%d)", s_count, PAGES_MAX);
    return -1;
  }

  if (pages_find(def->id) >= 0) {
    ESP_LOGW(TAG, "Page '%s' already exists", def->id);
    return pages_find(def->id);
  }

  int pos = insert_pos_for(priority);

  // Shift slots right to make room
  for (int i = s_count; i > pos; i--)
    s_slots[i] = s_slots[i - 1];

  s_slots[pos].def = *def;
  s_slots[pos].priority = priority;
  s_slots[pos].container = nullptr;
  s_slots[pos].live = true;
  s_count++;

  // If a page was inserted before or at current, shift current index
  if (pos <= s_current && s_current > 0)
    s_current++;

  rebuild_strip();

  ESP_LOGI(TAG, "Added page '%s' at index %d (priority %d), total %d", def->id,
           pos, priority, s_count);
  return pos;
}

void pages_remove(const char *id) {
  if (!id)
    return;

  int idx = pages_find(id);
  if (idx <= 0) // can't remove home (index 0), or not found (-1)
    return;

  // Call destroy if provided
  if (s_slots[idx].def.destroy)
    s_slots[idx].def.destroy();

  // If we're on the page being removed, slide left first
  if (s_current == idx) {
    s_current = idx > 0 ? idx - 1 : 0;
  } else if (s_current > idx) {
    s_current--;
  }

  // Shift slots left
  for (int i = idx; i < s_count - 1; i++)
    s_slots[i] = s_slots[i + 1];
  s_count--;

  rebuild_strip();

  ESP_LOGI(TAG, "Removed page '%s', total %d", id, s_count);
}

int pages_find(const char *id) {
  if (!id)
    return -1;
  for (int i = 0; i < s_count; i++) {
    if (s_slots[i].def.id && strcmp(s_slots[i].def.id, id) == 0)
      return i;
  }
  return -1;
}

bool pages_navigate(int delta) {
  if (s_animating)
    return false;

  int target = s_current + delta;
  if (target < 0 || target >= s_count)
    return false;

  slide_to(target, true);
  pages_poke();
  return true;
}

bool pages_go_to(const char *id) {
  int idx = pages_find(id);
  if (idx < 0 || idx == s_current)
    return false;

  slide_to(idx, true);
  pages_poke();
  return true;
}

void pages_go_home() {
  if (s_current == 0)
    return;
  slide_to(0, true);
}

void pages_poke() {
  if (!s_idle_timer)
    return;
  lv_timer_reset(s_idle_timer);
  lv_timer_resume(s_idle_timer);
}

int pages_count() { return s_count; }

int pages_current_index() { return s_current; }

const char *pages_current_id() {
  if (s_current >= 0 && s_current < s_count)
    return s_slots[s_current].def.id;
  return nullptr;
}

bool pages_is_home() { return s_current == 0; }

bool pages_wants_encoder() { return s_count > 1 && !pages_is_home(); }
