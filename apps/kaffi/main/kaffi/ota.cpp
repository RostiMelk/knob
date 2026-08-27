#include "kaffi/ota.h"
#include "app_config.h"
#include "kaffi/sanity_api.h"

#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>

static constexpr const char *TAG = "ota";

static volatile bool s_ota_running = false;

void kaffi_ota_mark_valid() {
  static bool done = false;
  if (done)
    return;
  done = true;
  // Only meaningful right after an OTA (state PENDING_VERIFY); harmless
  // otherwise.
  esp_ota_mark_app_valid_cancel_rollback();
}

// "1.2.3" → monotonic number; unparseable versions compare as 0 so a bad
// release doc can never trigger a downgrade loop.
static long version_num(const char *v) {
  int maj = 0;
  int min = 0;
  int pat = 0;
  if (sscanf(v, "%d.%d.%d", &maj, &min, &pat) < 2)
    return 0;
  return maj * 1000000L + min * 1000L + pat;
}

// The OTA download hits the Sanity CDN; the bearer token covers private
// datasets and is ignored on public ones.
static esp_err_t on_ota_http_init(esp_http_client_handle_t client) {
  static char auth[600];
  snprintf(auth, sizeof(auth), "Bearer %s", sanity_api_token());
  return esp_http_client_set_header(client, "Authorization", auth);
}

static void ota_task(void *) {
  char remote_ver[32];
  char bin_url[300];
  if (!sanity_api_fetch_firmware(remote_ver, sizeof(remote_ver), bin_url,
                                 sizeof(bin_url))) {
    ESP_LOGI(TAG, "No firmware release published");
    s_ota_running = false;
    vTaskDelete(nullptr);
  }

  const char *local_ver = esp_app_get_description()->version;
  if (version_num(remote_ver) <= version_num(local_ver)) {
    ESP_LOGI(TAG, "Up to date (local %s, published %s)", local_ver, remote_ver);
    s_ota_running = false;
    vTaskDelete(nullptr);
  }

  ESP_LOGW(TAG, "Updating %s -> %s", local_ver, remote_ver);

  esp_http_client_config_t http_cfg = {};
  http_cfg.url = bin_url;
  http_cfg.timeout_ms = 30000;
  http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
  http_cfg.buffer_size = 4096;
  http_cfg.keep_alive_enable = true;

  esp_https_ota_config_t ota_cfg = {};
  ota_cfg.http_config = &http_cfg;
  ota_cfg.http_client_init_cb = on_ota_http_init;

  esp_err_t err = esp_https_ota(&ota_cfg);
  if (err == ESP_OK) {
    ESP_LOGW(TAG, "Update written — rebooting into %s", remote_ver);
    esp_restart();
  }

  ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
  s_ota_running = false;
  vTaskDelete(nullptr);
}

void kaffi_ota_check() {
  if (s_ota_running)
    return;
  s_ota_running = true;
  if (xTaskCreatePinnedToCore(ota_task, "kaffi_ota", 12288, nullptr, 4, nullptr,
                              1) != pdPASS)
    s_ota_running = false;
}
