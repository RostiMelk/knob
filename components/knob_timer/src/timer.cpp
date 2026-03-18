#include "timer.h"
#include "timer_events.h"
#include "knob_events.h"
#include "voice_tools.h"

#include "esp_event.h"
#include "esp_log.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

static constexpr const char *TAG = "timer";

// ─── State ──────────────────────────────────────────────────────────────────

static std::atomic<int> s_remaining{0};
static char s_label[64] = {};

// ─── Format Helper ──────────────────────────────────────────────────────────

static int fmt_duration(char *buf, size_t len, int seconds) {
  int m = seconds / 60;
  int s = seconds % 60;
  if (m > 0)
    return snprintf(buf, len, "%d min %d sec", m, s);
  return snprintf(buf, len, "%d seconds", s);
}

// ─── Public API ─────────────────────────────────────────────────────────────

bool timer_start(int seconds, const char *label) {
  seconds = std::clamp(seconds, 1, 3600);
  strncpy(s_label, label ? label : "Timer", sizeof(s_label) - 1);
  s_label[sizeof(s_label) - 1] = '\0';
  s_remaining.store(seconds);

  int32_t total = seconds;
  esp_event_post(APP_EVENT, APP_EVENT_TIMER_STARTED, &total, sizeof(total), 0);

  ESP_LOGI(TAG, "Started: %ds label='%s'", seconds, s_label);
  return true;
}

bool timer_cancel() {
  int was = s_remaining.exchange(0);
  s_label[0] = '\0';
  if (was > 0)
    ESP_LOGI(TAG, "Cancelled (%ds remaining)", was);
  return was > 0;
}

bool timer_is_active() { return s_remaining.load() > 0; }

int timer_remaining_sec() { return s_remaining.load(); }

void timer_get_label(char *buf, int buf_len) {
  if (buf_len <= 0)
    return;
  strncpy(buf, s_label, buf_len - 1);
  buf[buf_len - 1] = '\0';
}

void timer_tick() {
  int val = s_remaining.load();
  if (val <= 0)
    return;

  int next = val - 1;
  if (s_remaining.compare_exchange_strong(val, next) && next == 0) {
    ESP_LOGI(TAG, "Timer '%s' fired!", s_label);
    esp_event_post(APP_EVENT, APP_EVENT_TIMER_FIRED, s_label,
                   strlen(s_label) + 1, 0);
    s_label[0] = '\0';
  }
}

void timer_init() {
  s_remaining.store(0);
  s_label[0] = '\0';
  ESP_LOGI(TAG, "Initialized");
}

// ─── Voice Tools ────────────────────────────────────────────────────────────

static bool handle_set_timer(const char *args, ToolResult *r) {
  int seconds = 0;
  if (!tool_json_get_int(args, "seconds", &seconds) || seconds < 1) {
    r->success = false;
    snprintf(r->output, sizeof(r->output),
             "Invalid duration. Provide seconds between 1 and 3600.");
    return true;
  }

  char label[64] = "Timer";
  tool_json_get_string(args, "label", label, sizeof(label));

  timer_start(seconds, label);

  r->success = true;
  char dur[32];
  fmt_duration(dur, sizeof(dur), std::clamp(seconds, 1, 3600));
  snprintf(r->output, sizeof(r->output), "Timer '%s' set for %s.", label, dur);
  return true;
}

static bool handle_cancel_timer(const char *, ToolResult *r) {
  r->success = true;
  if (timer_cancel())
    snprintf(r->output, sizeof(r->output), "Timer cancelled.");
  else
    snprintf(r->output, sizeof(r->output), "No timer was running.");
  return true;
}

static bool handle_get_timer_status(const char *, ToolResult *r) {
  int rem = timer_remaining_sec();
  r->success = true;
  if (rem <= 0) {
    snprintf(r->output, sizeof(r->output), "No timer is currently running.");
  } else {
    char label[64];
    timer_get_label(label, sizeof(label));
    char dur[32];
    fmt_duration(dur, sizeof(dur), rem);
    snprintf(r->output, sizeof(r->output), "Timer '%s': %s remaining.", label,
             dur);
  }
  return true;
}

REGISTER_TOOL(
    set_timer, "set_timer",
    "Set a countdown timer. When it expires the device will beep and show a "
    "notification. Only one timer at a time.",
    R"J({"type":"object","properties":{"seconds":{"type":"integer","description":"Duration in seconds (1-3600)"},"label":{"type":"string","description":"Optional short label, e.g. 'eggs' or 'pizza'"}},"required":["seconds"]})J",
    handle_set_timer);

REGISTER_TOOL(cancel_timer, "cancel_timer",
              "Cancel the currently running timer, if any.",
              R"J({"type":"object","properties":{}})J", handle_cancel_timer);

REGISTER_TOOL(get_timer_status, "get_timer_status",
              "Check how much time is left on the current timer.",
              R"J({"type":"object","properties":{}})J",
              handle_get_timer_status);
