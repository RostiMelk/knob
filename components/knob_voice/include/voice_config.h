// voice_config.h — Voice mode constants, state enum, and event IDs
#pragma once

#include "esp_event.h"
#include <cstdint>

// ─── Voice Task Constants ───────────────────────────────────────────────────

constexpr int VOICE_TASK_STACK = 12288;
constexpr int VOICE_TASK_PRIO = 5;
constexpr int VOICE_TASK_CORE = 1;

constexpr int VOICE_DUCKED_VOLUME = 1;
constexpr int VOICE_IDLE_TIMEOUT_MS = 8000;
constexpr int DOUBLE_TAP_WINDOW_MS = 300;

constexpr const char *OPENAI_REALTIME_URL =
    "wss://api.openai.com/v1/realtime?model=gpt-realtime-1.5";
constexpr const char *OPENAI_VOICE = "cedar";

// ─── Voice State ────────────────────────────────────────────────────────────

enum class VoiceState : uint8_t {
  Inactive,
  Connecting,
  Listening,
  Thinking,
  Speaking,
};

// ─── Voice Event IDs ────────────────────────────────────────────────────────
// Uses APP_EVENT base declared in knob_events.h.
// Offset from 100 to avoid collisions with common knob events and
// app-specific events. Apps register handlers for these on APP_EVENT.

enum : int32_t {
  APP_EVENT_VOICE_ACTIVATE = 100,
  APP_EVENT_VOICE_DEACTIVATE,
  APP_EVENT_VOICE_STATE,      // data: VoiceState enum
  APP_EVENT_VOICE_TRANSCRIPT, // data: null-terminated string
};
