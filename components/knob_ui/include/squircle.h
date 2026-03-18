#pragma once

#include "lvgl.h"

// Generate a squircle-shaped A8 alpha mask overlay and return an LVGL image
// that can be placed on top of any image to clip it to a squircle shape.
// The mask is black outside the squircle (alpha=255) and transparent inside
// (alpha=0). Call once at init — the mask is allocated in PSRAM and reused.
//
// size: width/height of the square mask (e.g. 120)
// n:    superellipse exponent (4.0 = iOS-style squircle, 2.0 = circle)
void squircle_mask_init(int size = 120, float n = 4.0f);

// Get the generated mask image descriptor. Returns nullptr before init.
const lv_image_dsc_t *squircle_mask_get();
