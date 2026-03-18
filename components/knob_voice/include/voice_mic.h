#pragma once
#include <cstddef>
#include <cstdint>

// Initialize PDM mic hardware (call once at startup or when entering voice mode)
void voice_mic_init();

// Start capturing audio into ring buffer
void voice_mic_start();

// Stop capturing, flush buffer
void voice_mic_stop();

// Read PCM samples from ring buffer. Returns bytes read (0 if empty).
// Blocks up to timeout_ms. Data is 16-bit signed PCM @ 24kHz mono.
size_t voice_mic_read(int16_t *buf, size_t max_samples, int timeout_ms);

// Check if mic is currently capturing
bool voice_mic_is_active();
