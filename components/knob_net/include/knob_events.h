#pragma once

#include "esp_event.h"
#include <cstdint>

// Shared event base for all knob applications
ESP_EVENT_DECLARE_BASE(APP_EVENT);

// ─── Common Events (shared across all knob apps) ───────────────────────────
// Apps extend this enum by defining additional event IDs starting after
// KNOB_EVENT_LAST in their own app_config.h

enum : int32_t {
  APP_EVENT_WIFI_CONNECTED = 0,
  APP_EVENT_WIFI_DISCONNECTED,
  APP_EVENT_ENCODER_ROTATE, // data: int32_t delta (+/- steps)

  // Sentinel — app-specific events start after this
  KNOB_EVENT_LAST,
};
