#pragma once

#include <cstdint>

void timer_init();

bool timer_start(int seconds, const char *label = "Timer");
bool timer_cancel();

bool timer_is_active();
int timer_remaining_sec();
void timer_get_label(char *buf, int buf_len);

void timer_tick();
