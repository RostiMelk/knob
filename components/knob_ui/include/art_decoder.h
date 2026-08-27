#pragma once

#include <cstdint>

// Decode a JPEG image from memory into an RGB565 pixel buffer in PSRAM.
// The output is byte-swapped RGB565 (big-endian) matching
// LV_COLOR_FORMAT_RGB565_SWAPPED.
//
// jpeg_data / jpeg_len: raw JPEG bytes
// out_buf:   receives a heap_caps_malloc'd PSRAM buffer (caller must free)
// out_w:     receives decoded image width (after scaling)
// out_h:     receives decoded image height (after scaling)
// max_dim:   target max dimension — the decoder picks a scale factor (1/1, 1/2,
// 1/4, 1/8)
//            so that the output fits within max_dim × max_dim
//
// Returns true on success. On failure, *out_buf is nullptr.
bool art_decode_jpeg(const uint8_t *jpeg_data, int jpeg_len, uint8_t **out_buf,
                     int *out_w, int *out_h, int max_dim = 120);

// Box-average resample of a swapped-RGB565 image into a new PSRAM buffer of
// exactly dst_w × dst_h. Scaled lv_image transforms run through LVGL's slow
// per-pixel CPU path on every frame, so pre-scale once to the widget size and
// render untransformed instead.
//
// Returns a heap_caps_malloc'd buffer the caller must free, or nullptr on
// allocation failure.
uint8_t *art_scale_rgb565(const uint8_t *src, int src_w, int src_h, int dst_w,
                          int dst_h);
