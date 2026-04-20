#include "cal_fetch.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include <cstring>

static constexpr const char *TAG = "cal_fetch";

static constexpr size_t INITIAL_BUF_SIZE = 64 * 1024; // 64 KB
static constexpr size_t MAX_BUF_SIZE =
    4 * 1024 * 1024; // 4 MB (Google iCal includes full history)

struct FetchCtx {
  char *buf;
  size_t len;
  size_t cap;
  bool error;
};

static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
  auto *ctx = static_cast<FetchCtx *>(evt->user_data);
  if (!ctx)
    return ESP_OK;

  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    if (ctx->error || evt->data_len == 0)
      return ESP_OK;

    // +1 for null terminator
    size_t needed = ctx->len + evt->data_len + 1;
    if (needed > MAX_BUF_SIZE) {
      ESP_LOGE(TAG, "Response exceeds %zu byte limit", MAX_BUF_SIZE);
      ctx->error = true;
      return ESP_OK;
    }

    if (needed > ctx->cap) {
      size_t new_cap = ctx->cap;
      while (new_cap < needed) {
        new_cap *= 2;
      }
      if (new_cap > MAX_BUF_SIZE) {
        new_cap = MAX_BUF_SIZE;
      }

      char *new_buf = static_cast<char *>(
          heap_caps_realloc(ctx->buf, new_cap, MALLOC_CAP_SPIRAM));
      if (!new_buf) {
        ESP_LOGE(TAG, "PSRAM realloc failed (requested %zu)", new_cap);
        ctx->error = true;
        return ESP_OK;
      }
      ctx->buf = new_buf;
      ctx->cap = new_cap;
    }

    memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
    ctx->len += evt->data_len;
    ctx->buf[ctx->len] = '\0';
  }

  return ESP_OK;
}

void cal_fetch_init() { ESP_LOGI(TAG, "Calendar fetcher initialised"); }

char *cal_fetch_ical(size_t *out_len) {
  if (out_len)
    *out_len = 0;

  // Allocate initial buffer in PSRAM
  char *buf = static_cast<char *>(
      heap_caps_malloc(INITIAL_BUF_SIZE, MALLOC_CAP_SPIRAM));
  if (!buf) {
    ESP_LOGE(TAG, "PSRAM alloc failed (%zu bytes)", INITIAL_BUF_SIZE);
    return nullptr;
  }
  buf[0] = '\0';

  FetchCtx ctx = {
      .buf = buf,
      .len = 0,
      .cap = INITIAL_BUF_SIZE,
      .error = false,
  };

  esp_http_client_config_t cfg = {};
  cfg.url = CONFIG_CALENDAR_ICAL_URL;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 10000;
  cfg.buffer_size = 4096;
  cfg.buffer_size_tx = 2048;
  cfg.event_handler = http_event_handler;
  cfg.user_data = &ctx;

  esp_http_client_handle_t client = esp_http_client_init(&cfg);
  if (!client) {
    ESP_LOGE(TAG, "Failed to init HTTP client");
    heap_caps_free(ctx.buf);
    return nullptr;
  }

  esp_err_t err = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    heap_caps_free(ctx.buf);
    return nullptr;
  }

  if (ctx.error) {
    ESP_LOGE(TAG, "Error during response accumulation");
    heap_caps_free(ctx.buf);
    return nullptr;
  }

  if (status != 200) {
    ESP_LOGE(TAG, "HTTP status %d", status);
    heap_caps_free(ctx.buf);
    return nullptr;
  }

  if (ctx.len == 0) {
    ESP_LOGE(TAG, "Empty response body");
    heap_caps_free(ctx.buf);
    return nullptr;
  }

  ESP_LOGI(TAG, "Fetched %zu bytes from iCal feed", ctx.len);

  if (out_len)
    *out_len = ctx.len;
  return ctx.buf;
}
