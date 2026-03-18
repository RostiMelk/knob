// voice_audio.h — I2S audio playback for PCM5100A DAC (ESP32-S3)
// ESP-IDF v5.4, i2s_std new driver API
#pragma once
#include <cstddef>
#include <cstdint>

// Initialize I2S output hardware (I2S_NUM_1, PCM5100A on GPIO39/40/41)
void voice_audio_init();

// Start playback task (reads from internal ring buffer, writes to I2S)
void voice_audio_start();

// Stop playback, flush buffer, disable I2S
void voice_audio_stop();

// Queue base64-encoded PCM audio for playback.
// Decodes base64 → PCM16 and writes to ring buffer.
// Called from WebSocket event handler when receiving OpenAI audio deltas.
void voice_audio_play_b64(const char *base64_data, size_t base64_len);

// Queue raw PCM16 samples for playback
void voice_audio_play_pcm(const int16_t *samples, size_t num_samples);

// Wait for playback buffer to drain (up to timeout_ms). Returns true if drained.
bool voice_audio_drain(int timeout_ms);

// Check if audio is currently playing (ring buffer non-empty or task active)
bool voice_audio_is_playing();
