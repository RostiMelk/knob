#pragma once

#include <stdint.h>

void encoder_init();

// Atomically read and reset the accumulated step count.
// Positive = CW, negative = CCW. Called from the UI task to
// coalesce rapid encoder ticks into a single update.
int32_t encoder_take_steps();
