#pragma once

#include "lvgl.h"

void ui_timer_init();

void ui_timer_show(int seconds, const char *label);
void ui_timer_dismiss();
bool ui_timer_is_visible();

const char *ui_timer_page_id();
