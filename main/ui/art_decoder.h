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
