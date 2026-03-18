#include "voice_mic.h"

#include <cstring>

#include "driver/i2s_pdm.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"
#include "freertos/task.h"

static constexpr const char *TAG = "voice_mic";

// Hardware configuration
static constexpr gpio_num_t PDM_CLK_GPIO  = GPIO_NUM_45;
static constexpr gpio_num_t PDM_DATA_GPIO = GPIO_NUM_46;
static constexpr uint32_t   SAMPLE_RATE   = 24000;

// Ring buffer: 48KB in PSRAM (~1 second of 16-bit mono @ 24kHz)
static constexpr size_t RING_BUF_SIZE = 48000;

// Internal I2S read task parameters
static constexpr size_t   MIC_TASK_STACK    = 4096;
static constexpr UBaseType_t MIC_TASK_PRIO  = 6;
static constexpr BaseType_t  MIC_TASK_CORE  = 1;

// DMA read chunk: 512 samples × 2 bytes = 1024 bytes
// Sized to balance latency vs. overhead
static constexpr size_t DMA_READ_SAMPLES = 512;
static constexpr size_t DMA_READ_BYTES   = DMA_READ_SAMPLES * sizeof(int16_t);

// ── Module state ────────────────────────────────────────────────────────────

static i2s_chan_handle_t s_rx_chan   = nullptr;
static RingbufHandle_t   s_ring_buf = nullptr;
static TaskHandle_t      s_mic_task = nullptr;
static volatile bool     s_active   = false;

// ── Internal read task ──────────────────────────────────────────────────────

static void mic_read_task(void * /*arg*/)
{
    // Stack-local DMA buffer — fits comfortably in 4 KB stack
    int16_t dma_buf[DMA_READ_SAMPLES];

    ESP_LOGI(TAG, "mic read task started on core %d", xPortGetCoreID());

    while (s_active) {
        size_t bytes_read = 0;
        esp_err_t err = i2s_channel_read(s_rx_chan, dma_buf, DMA_READ_BYTES,
                                         &bytes_read, pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            // Channel disabled (stop in progress) or timeout — just loop
            if (err != ESP_ERR_INVALID_STATE) {
                ESP_LOGW(TAG, "i2s read error: %s", esp_err_to_name(err));
            }
            continue;
        }

        if (bytes_read == 0) {
            continue;
        }

        // Push into ring buffer; drop data if full (non-blocking-ish)
        BaseType_t ok = xRingbufferSend(s_ring_buf, dma_buf, bytes_read,
                                        pdMS_TO_TICKS(10));
        if (ok != pdTRUE) {
            ESP_LOGD(TAG, "ring buffer full, dropped %u bytes",
                     (unsigned)bytes_read);
        }
    }

    ESP_LOGI(TAG, "mic read task exiting");
    vTaskDelete(nullptr);  // self-delete
}

// ── Public API ──────────────────────────────────────────────────────────────

void voice_mic_init()
{
    if (s_rx_chan != nullptr) {
        ESP_LOGW(TAG, "already initialised");
        return;
    }

    // 1. Allocate I2S RX channel
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0,
                                                           I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &s_rx_chan));

    // 2. Configure PDM RX mode
    i2s_pdm_rx_config_t pdm_cfg = {
        .clk_cfg  = I2S_PDM_RX_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                    I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = PDM_CLK_GPIO,
            .din = PDM_DATA_GPIO,
            .invert_flags = { .clk_inv = false },
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(s_rx_chan, &pdm_cfg));

    // 3. Create ring buffer in PSRAM
    s_ring_buf = xRingbufferCreateWithCaps(RING_BUF_SIZE,
                                           RINGBUF_TYPE_BYTEBUF,
                                           MALLOC_CAP_SPIRAM);
    if (s_ring_buf == nullptr) {
        ESP_LOGE(TAG, "failed to allocate ring buffer in PSRAM");
        // Fatal for voice mode — let caller handle via ESP_ERROR_CHECK style
        abort();
    }

    ESP_LOGI(TAG, "initialised: PDM RX on CLK=%d DATA=%d, %lu Hz, ring=%u B",
             (int)PDM_CLK_GPIO, (int)PDM_DATA_GPIO,
             (unsigned long)SAMPLE_RATE, (unsigned)RING_BUF_SIZE);
}

void voice_mic_start()
{
    if (s_active) {
        ESP_LOGW(TAG, "already capturing");
        return;
    }
    if (s_rx_chan == nullptr) {
        ESP_LOGE(TAG, "not initialised — call voice_mic_init() first");
        return;
    }

    // Enable the I2S channel (starts clocking the PDM mic)
    ESP_ERROR_CHECK(i2s_channel_enable(s_rx_chan));

    s_active = true;

    // Launch the read task pinned to Core 1
    BaseType_t ret = xTaskCreatePinnedToCore(mic_read_task,
                                             "mic_read",
                                             MIC_TASK_STACK,
                                             nullptr,
                                             MIC_TASK_PRIO,
                                             &s_mic_task,
                                             MIC_TASK_CORE);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create mic read task");
        s_active = false;
        i2s_channel_disable(s_rx_chan);
        return;
    }

    ESP_LOGI(TAG, "capture started");
}

void voice_mic_stop()
{
    if (!s_active) {
        ESP_LOGD(TAG, "not active, nothing to stop");
        return;
    }

    // Signal the task to exit
    s_active = false;

    // Wait for the task to finish (it self-deletes, but we give it time)
    // The task checks s_active each loop iteration; i2s_channel_read has a
    // 100 ms timeout so worst-case latency is ~100 ms.
    if (s_mic_task != nullptr) {
        // Give the task time to notice s_active == false and exit
        vTaskDelay(pdMS_TO_TICKS(150));
        s_mic_task = nullptr;
    }

    // Disable I2S channel (stops DMA + clocks)
    ESP_ERROR_CHECK(i2s_channel_disable(s_rx_chan));

    // Flush the ring buffer — drain all remaining items
    size_t item_size;
    while (true) {
        void *item = xRingbufferReceiveUpTo(s_ring_buf, &item_size, 0,
                                            RING_BUF_SIZE);
        if (item == nullptr) {
            break;
        }
        vRingbufferReturnItem(s_ring_buf, item);
    }

    ESP_LOGI(TAG, "capture stopped, buffer flushed");
}

size_t voice_mic_read(int16_t *buf, size_t max_samples, int timeout_ms)
{
    if (buf == nullptr || max_samples == 0) {
        return 0;
    }
    if (s_ring_buf == nullptr) {
        return 0;
    }

    const size_t max_bytes = max_samples * sizeof(int16_t);

    size_t item_size = 0;
    void *item = xRingbufferReceiveUpTo(s_ring_buf, &item_size,
                                        pdMS_TO_TICKS(timeout_ms),
                                        max_bytes);
    if (item == nullptr) {
        return 0;
    }

    // Ensure we copy an even number of bytes (whole samples)
    const size_t copy_bytes = item_size & ~(size_t)1;
    std::memcpy(buf, item, copy_bytes);
    vRingbufferReturnItem(s_ring_buf, item);

    return copy_bytes;
}

bool voice_mic_is_active()
{
    return s_active;
}
