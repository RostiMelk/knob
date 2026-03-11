#include "app_events.h"
#include "esp_event.h"

// Define the event base for cross-module communication.
// This must exist exactly once in the binary. Both C++ components
// and Rust (via esp_event_post/subscribe) reference this symbol.
ESP_EVENT_DEFINE_BASE(APP_EVENT_BASE);
