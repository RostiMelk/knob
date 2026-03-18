#pragma once

#include <cstdint>

enum : int32_t {
  APP_EVENT_TIMER_STARTED = 300, // data: int32_t total seconds
  APP_EVENT_TIMER_FIRED = 301,   // data: null-terminated label string
};
