#pragma once

#include "lvgl.h"

void display_init(lv_display_t **disp, lv_indev_t **touch);

bool display_lock(int timeout_ms);
void display_unlock();

#ifndef SIMULATOR
#include "driver/i2c_master.h"
i2c_master_bus_handle_t display_get_i2c_bus();
#endif
