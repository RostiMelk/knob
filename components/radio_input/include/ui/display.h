#pragma once

// Minimal header for radio_input — only declares the I2C bus accessor
// that haptic.cpp needs. Avoids pulling in LVGL via the full display.h.
#include "driver/i2c_master.h"
i2c_master_bus_handle_t display_get_i2c_bus();
