#pragma once

#include "lvgl.h"

void display_init(lv_display_t **disp, lv_indev_t **touch);

bool display_lock(int timeout_ms);
void display_unlock();
