// voice_audio.cpp — I2S audio playback for PCM5100A DAC (ESP32-S3)
// ESP-IDF v5.4, i2s_std new driver API, 24kHz 16-bit mono → stereo
#include "voice_audio.h"

#include <cstring>

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

static const char *TAG = "voice_audio";

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
static constexpr gpio_num_t PIN_BCLK = GPIO_NUM_39;
static constexpr gpio_num_t PIN_WS   = GPIO_NUM_40;
static constexpr gpio_num_t PIN_DOUT = GPIO_NUM_41;

static constexpr uint32_t SAMPLE_RATE      = 24000;
static constexpr size_t   RINGBUF_SIZE     = 96 * 1024;          // 96 KB in PSRAM
static constexpr size_t   PLAYBACK_CHUNK   = 480;                // mono samples per iteration (20 ms @ 24 kHz)
static constexpr size_t   TASK_STACK       = 4096;
static constexpr UBaseType_t TASK_PRIO     = 6;
static constexpr BaseType_t  TASK_CORE     = 1;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
static i2s_chan_handle_t  s_tx_chan   = nullptr;
static RingbufHandle_t    s_ringbuf  = nullptr;
static TaskHandle_t       s_task     = nullptr;
static volatile bool      s_running  = false;

// ---------------------------------------------------------------------------
// Base64 decoder (no external library)
// ---------------------------------------------------------------------------
// clang-format off
static const uint8_t B64_TABLE[256] = {
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,  // 0-15
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,  // 16-31
    64,64,64,64,64,64,64,64,64,64,64,62,64,64,64,63,  // 32-47 ('+' = 43 -> 62, '/' = 47 -> 63)
    52,53,54,55,56,57,58,59,60,61,64,64,64,64,64,64,  // 48-63 ('0'-'9' -> 52-61)
    64, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,  // 64-79 ('A'-'O' -> 0-14)
    15,16,17,18,19,20,21,22,23,24,25,64,64,64,64,64,  // 80-95 ('P'-'Z' -> 15-25)
    64,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,  // 96-111 ('a'-'o' -> 26-40)
    41,42,43,44,45,46,47,48,49,50,51,64,64,64,64,64,  // 112-127 ('p'-'z' -> 41-51)
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,  // 128-143
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,  // 144-159
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,  // 160-175
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,  // 176-191
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,  // 192-207
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,  // 208-223
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,  // 224-239
    64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,64,  // 240-255
};
// clang-format on

/// Decode base64 into `out`. Returns number of bytes written.
static size_t base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_max) {
    size_t o = 0;
    uint32_t accum = 0;
    int bits = 0;

    for (size_t i = 0; i < in_len && o < out_max; ++i) {
        uint8_t val = B64_TABLE[static_cast<uint8_t>(in[i])];
        if (val >= 64) {
            continue;  // skip padding ('='), whitespace, invalid chars
        }
        accum = (accum << 6) | val;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out[o++] = static_cast<uint8_t>((accum >> bits) & 0xFF);
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// Mono → Stereo duplication
// ---------------------------------------------------------------------------
static void mono_to_stereo(const int16_t *mono, int16_t *stereo, size_t num_samples) {
    for (size_t i = 0; i < num_samples; ++i) {
        stereo[i * 2]     = mono[i];
        stereo[i * 2 + 1] = mono[i];
    }
}

// ---------------------------------------------------------------------------
// Playback task
// ---------------------------------------------------------------------------
static void playback_task(void * /*arg*/) {
    // Stereo buffer on stack: PLAYBACK_CHUNK * 2 channels * 2 bytes = 1920 bytes
    int16_t stereo_buf[PLAYBACK_CHUNK * 2];
    // Silence frame for underrun
    static const int16_t silence[PLAYBACK_CHUNK * 2] = {};

    ESP_LOGI(TAG, "playback task started");

    while (s_running) {
        size_t item_size = 0;
        // Try to receive mono samples from ring buffer (wait up to 20 ms)
        void *item = xRingbufferReceiveUpTo(
            s_ringbuf,
            &item_size,
            pdMS_TO_TICKS(20),
            PLAYBACK_CHUNK * sizeof(int16_t));

        const int16_t *write_ptr;
        size_t write_bytes;

        if (item != nullptr && item_size > 0) {
            size_t mono_samples = item_size / sizeof(int16_t);
            mono_to_stereo(static_cast<const int16_t *>(item), stereo_buf, mono_samples);
            vRingbufferReturnItem(s_ringbuf, item);

            write_ptr  = stereo_buf;
            write_bytes = mono_samples * 2 * sizeof(int16_t);  // stereo frame size
        } else {
            // Underrun — write silence to keep DAC clocked
            write_ptr  = silence;
            write_bytes = PLAYBACK_CHUNK * 2 * sizeof(int16_t);
        }

        size_t bytes_written = 0;
        esp_err_t err = i2s_channel_write(
            s_tx_chan,
            write_ptr,
            write_bytes,
            &bytes_written,
            pdMS_TO_TICKS(100));

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "i2s_channel_write error: %s", esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "playback task exiting");
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void voice_audio_init() {
    ESP_LOGI(TAG, "init: I2S_NUM_1, %lu Hz, 16-bit stereo (Philips), BCLK=%d WS=%d DOUT=%d",
             (unsigned long)SAMPLE_RATE, PIN_BCLK, PIN_WS, PIN_DOUT);

    // --- Create I2S TX channel ---
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_tx_chan, nullptr));

    // --- Configure standard (Philips) mode ---
    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
    std_cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                           I2S_SLOT_MODE_STEREO);
    std_cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.bclk = PIN_BCLK;
    std_cfg.gpio_cfg.ws   = PIN_WS;
    std_cfg.gpio_cfg.dout = PIN_DOUT;
    std_cfg.gpio_cfg.din  = I2S_GPIO_UNUSED;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = false;
    std_cfg.gpio_cfg.invert_flags.ws_inv   = false;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_tx_chan, &std_cfg));

    // --- Create ring buffer in PSRAM ---
    s_ringbuf = xRingbufferCreateWithCaps(RINGBUF_SIZE, RINGBUF_TYPE_BYTEBUF, MALLOC_CAP_SPIRAM);
    if (s_ringbuf == nullptr) {
        ESP_LOGE(TAG, "failed to create ring buffer (%u bytes in PSRAM)", (unsigned)RINGBUF_SIZE);
        return;
    }

    ESP_LOGI(TAG, "init complete, ring buffer %u KB in PSRAM", (unsigned)(RINGBUF_SIZE / 1024));
}

void voice_audio_start() {
    if (s_running) {
        ESP_LOGW(TAG, "already running");
        return;
    }
    if (s_tx_chan == nullptr) {
        ESP_LOGE(TAG, "not initialised — call voice_audio_init() first");
        return;
    }

    // Enable I2S channel
    ESP_ERROR_CHECK(i2s_channel_enable(s_tx_chan));

    s_running = true;

    BaseType_t ret = xTaskCreatePinnedToCore(
        playback_task,
        "audio_play",
        TASK_STACK,
        nullptr,
        TASK_PRIO,
        &s_task,
        TASK_CORE);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create playback task");
        s_running = false;
        i2s_channel_disable(s_tx_chan);
        return;
    }

    ESP_LOGI(TAG, "playback started (core %d, prio %u)", (int)TASK_CORE, (unsigned)TASK_PRIO);
}

void voice_audio_stop() {
    if (!s_running) {
        return;
    }

    ESP_LOGI(TAG, "stopping playback");

    // Signal task to exit
    s_running = false;

    // Wait for task to finish (it checks s_running each loop iteration)
    if (s_task != nullptr) {
        // Give the task time to notice the flag and exit
        vTaskDelay(pdMS_TO_TICKS(150));
        s_task = nullptr;
    }

    // Disable I2S
    if (s_tx_chan != nullptr) {
        i2s_channel_disable(s_tx_chan);
    }

    // Flush ring buffer
    if (s_ringbuf != nullptr) {
        // Drain any remaining items
        size_t item_size = 0;
        void *item;
        while ((item = xRingbufferReceive(s_ringbuf, &item_size, 0)) != nullptr) {
            vRingbufferReturnItem(s_ringbuf, item);
        }
    }

    ESP_LOGI(TAG, "playback stopped");
}

void voice_audio_play_b64(const char *base64_data, size_t base64_len) {
    if (base64_data == nullptr || base64_len == 0) {
        return;
    }

    // Worst-case decoded size: 3/4 of input length (+ a little margin)
    size_t max_decoded = (base64_len * 3) / 4 + 4;

    // Allocate temp buffer for decoded PCM (prefer PSRAM for large chunks)
    uint8_t *decoded = static_cast<uint8_t *>(
        heap_caps_malloc(max_decoded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (decoded == nullptr) {
        // Fall back to internal RAM
        decoded = static_cast<uint8_t *>(malloc(max_decoded));
    }
    if (decoded == nullptr) {
        ESP_LOGW(TAG, "b64 decode: alloc failed (%u bytes)", (unsigned)max_decoded);
        return;
    }

    size_t decoded_len = base64_decode(base64_data, base64_len, decoded, max_decoded);

    if (decoded_len >= sizeof(int16_t)) {
        size_t num_samples = decoded_len / sizeof(int16_t);
        voice_audio_play_pcm(reinterpret_cast<const int16_t *>(decoded), num_samples);
    }

    free(decoded);
}

void voice_audio_play_pcm(const int16_t *samples, size_t num_samples) {
    if (s_ringbuf == nullptr || samples == nullptr || num_samples == 0) {
        return;
    }

    size_t bytes = num_samples * sizeof(int16_t);

    // Try to send to ring buffer with a short timeout so we don't block the
    // WebSocket handler for too long.  If the buffer is full we drop the data
    // (better than blocking the network task).
    BaseType_t ok = xRingbufferSend(s_ringbuf, samples, bytes, pdMS_TO_TICKS(50));
    if (ok != pdTRUE) {
        ESP_LOGW(TAG, "ring buffer full — dropped %u samples", (unsigned)num_samples);
    }
}

bool voice_audio_drain(int timeout_ms) {
    if (s_ringbuf == nullptr) {
        return true;
    }

    TickType_t start = xTaskGetTickCount();
    TickType_t deadline = start + pdMS_TO_TICKS(timeout_ms);

    while (xTaskGetTickCount() < deadline) {
        // Check free size — when it's close to total capacity, the buffer is drained.
        // Byte-buffer ring buffers have a small internal overhead, so we allow a
        // margin of 64 bytes rather than comparing to exact RINGBUF_SIZE.
        UBaseType_t free_size = xRingbufferGetCurFreeSize(s_ringbuf);
        if (free_size >= (RINGBUF_SIZE - 64)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ESP_LOGW(TAG, "drain timeout (%d ms)", timeout_ms);
    return false;
}

bool voice_audio_is_playing() {
    if (!s_running || s_ringbuf == nullptr) {
        return false;
    }

    UBaseType_t free_size = xRingbufferGetCurFreeSize(s_ringbuf);
    // If ring buffer has data in it, we're still playing
    return free_size < (RINGBUF_SIZE - 64);
}
