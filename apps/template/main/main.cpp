// Template knob application
// Copy this directory, rename it, and customize for your use case.
//
// Available shared components:
//   knob_hal     — display, touch, encoder, haptic (hal_pins.h, display.h, encoder.h, haptic.h)
//   knob_net     — WiFi manager (wifi_manager.h, knob_events.h)
//   knob_storage — NVS settings, SD card config (settings.h)
//   knob_ui      — LVGL page system, fonts, squircle, art decoder (ui_pages.h, fonts.h, etc.)
//   knob_voice   — OpenAI Realtime API voice pipeline (voice_task.h, voice_tools.h, etc.)
//   knob_sonos   — Sonos UPnP/SOAP control (sonos.h, discovery.h)
//   knob_timer   — Countdown timer with voice tools (timer.h)

#include "hal_pins.h"
#include "knob_events.h"
#include "display.h"
#include "encoder.h"
#include "haptic.h"
#include "wifi_manager.h"
#include "settings.h"

#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

static constexpr const char *TAG = "app";

ESP_EVENT_DEFINE_BASE(APP_EVENT);

static void on_wifi_connected(void *, esp_event_base_t, int32_t, void *) {
  ESP_LOGI(TAG, "WiFi connected");
  // Your app logic here
}

static void on_encoder_rotate(void *, esp_event_base_t, int32_t, void *data) {
  auto steps = *static_cast<int32_t *>(data);
  ESP_LOGI(TAG, "Encoder: %+d steps", (int)steps);
  // Your app logic here
}

static void init_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

extern "C" void app_main() {
  ESP_LOGI(TAG, "Knob app starting");

  init_nvs();
  settings_init();

  esp_event_loop_create_default();

  esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_CONNECTED,
                             on_wifi_connected, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_ENCODER_ROTATE,
                             on_encoder_rotate, nullptr);

  // Init hardware
  lv_display_t *disp = nullptr;
  lv_indev_t *touch = nullptr;
  display_init(&disp, &touch);

  haptic_init();
  encoder_init();

  // Start WiFi (fires APP_EVENT_WIFI_CONNECTED when ready)
  wifi_manager_init();

  ESP_LOGI(TAG, "Init complete");
}
