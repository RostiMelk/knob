#pragma once

#include "lvgl.h"
#include <cstdint>

// ─── Page Lifecycle ─────────────────────────────────────────────────────────
//
// Each page provides a build and destroy callback. build() creates LVGL
// objects as children of the container it receives. destroy() cleans up
// any external state (timers, subscriptions) — LVGL objects are deleted
// automatically when the page is removed from the pager.
//
// An optional tick() callback is called every LVGL timer cycle (~100ms)
// while the page is the active (visible) page. Use it for countdowns,
// animations, or polling state.

struct PageDef {
  const char *id;
  void (*build)(lv_obj_t *parent);
  void (*destroy)();
  void (*tick)();
};

// ─── Limits ─────────────────────────────────────────────────────────────────

constexpr int PAGES_MAX = 8;

// ─── Pager Lifecycle ────────────────────────────────────────────────────────

// Callback fired after the current page changes (slide animation started).
// index: new page index, id: new page id.
using PageChangedCb = void (*)(int index, const char *id);

// Call once during ui_init(). parent is the top-level screen object.
// home must be the PageDef for the main/home view (always page 0).
// on_changed is optional — called whenever the visible page changes.
void pages_init(lv_obj_t *parent, const PageDef *home,
                PageChangedCb on_changed = nullptr);

// ─── Live View Management ───────────────────────────────────────────────────
//
// Live views are pages that sit to the right of home. They're added and
// removed dynamically (e.g. when a timer starts/fires). The pager
// handles slide animation and dot indicators automatically.
//
// priority: lower = closer to home. Pages are sorted by priority so the
//           user always encounters them in a predictable order.
//           Home is implicitly priority 0. Use 10/20/30 for spacing.

int pages_add(const PageDef *def, int priority);
void pages_remove(const char *id);
int pages_find(const char *id);

// ─── Navigation ─────────────────────────────────────────────────────────────

// Slide to adjacent page. delta: -1 = left, +1 = right.
// Returns true if the page actually changed.
bool pages_navigate(int delta);

// Jump directly to a page by id. Animates the slide.
bool pages_go_to(const char *id);

// Jump to home (page 0).
void pages_go_home();

// ─── Inactivity Timeout ─────────────────────────────────────────────────────
//
// After 7 seconds of no encoder/touch activity:
//   - If a live view exists → slide to the first (highest priority) live view
//   - If no live views     → slide to home
//
// Call pages_poke() on any user interaction (encoder rotate, touch tap)
// to reset the inactivity timer.

void pages_poke();

// ─── Queries ────────────────────────────────────────────────────────────────

int pages_count();
int pages_current_index();
const char *pages_current_id();
bool pages_is_home();

// ─── Internal (called by ui.cpp) ────────────────────────────────────────────

// Whether the pager wants to consume encoder input (i.e. we're on a live
// view page or transitioning). ui.cpp checks this to decide whether
// encoder rotation means volume/browse or page swipe.
bool pages_wants_encoder();
