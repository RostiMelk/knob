#pragma once

#include "hal_pins.h"
#include "knob_events.h"
#include "weather.h"

// ─── Calendar Events ────────────────────────────────────────────────────────

enum : int32_t {
  APP_EVENT_CALENDAR_UPDATED = KNOB_EVENT_LAST,
  APP_EVENT_CALENDAR_FETCH_FAILED,
};

// ─── Task Config ────────────────────────────────────────────────────────────

constexpr int UI_TASK_STACK = 8192;
constexpr int UI_TASK_PRIO = 5;
constexpr int UI_TASK_CORE = 0;

constexpr int NET_TASK_PRIO = 4;
constexpr int NET_TASK_CORE = 0;

// ─── Calendar Limits ────────────────────────────────────────────────────────

constexpr int MAX_EVENTS = 64;
