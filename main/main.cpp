#include "app_config.h"
#include "input/encoder.h"
#include "net/wifi_manager.h"
#include "sonos/discovery.h"
#include "sonos/sonos.h"
#include "storage/settings.h"
#include "ui/ui.h"

#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>

static constexpr const char *TAG = "main";

ESP_EVENT_DEFINE_BASE(APP_EVENT);

static int s_volume;
static int s_station_index;

static void on_encoder_rotate(void *, esp_event_base_t, int32_t, void *data) {
  auto delta = *static_cast<int32_t *>(data);
  ui_on_encoder_rotate(delta);
}

static void on_station_changed(void *, esp_event_base_t, int32_t, void *data) {
  auto index = *static_cast<int32_t *>(data);
  if (index < 0 || index >= STATION_COUNT)
    return;
  s_station_index = index;
  settings_set_station_index(s_station_index);
  sonos_play_uri(STATIONS[s_station_index].url);
}

static void on_volume_changed(void *, esp_event_base_t, int32_t, void *data) {
  auto vol = *static_cast<int32_t *>(data);
  s_volume = vol;
  settings_set_volume(vol);
}

static void on_sonos_state(void *, esp_event_base_t, int32_t, void *data) {
  auto *state = static_cast<SonosState *>(data);
  ui_set_play_state(state->play_state);
  if (state->volume != s_volume) {
    s_volume = state->volume;
    ui_set_volume(s_volume);
  }
}

static void discover_and_connect_task(void *) {
  ui_show_scanning();

  DiscoveryResult result = {};
  discovery_scan(&result);

  if (result.count == 1) {
    auto &speaker = result.speakers[0];
    ESP_LOGI(TAG, "Single speaker found — auto-selecting: %s", speaker.name);
    sonos_set_speaker(speaker.ip, speaker.port);
    settings_set_speaker_name(speaker.name);
    ui_set_speaker_name(speaker.name);
    sonos_start();
  } else if (result.count > 1) {
    ui_show_speaker_picker(&result);
  } else {
    ESP_LOGW(TAG, "No speakers found — showing picker with empty state");
    ui_show_speaker_picker(&result);
  }

  vTaskDelete(nullptr);
}

static void start_with_saved_speaker() {
  char ip[40] = {};
  char name[64] = {};
  settings_get_speaker_ip(ip, sizeof(ip));
  settings_get_speaker_name(name, sizeof(name));

  ESP_LOGI(TAG, "Reconnecting to saved speaker: %s (%s)", name, ip);
  sonos_set_speaker(ip);
  ui_set_speaker_name(name[0] ? name : ip);
  sonos_start();
}

static void on_wifi_connected(void *, esp_event_base_t, int32_t, void *) {
  ESP_LOGI(TAG, "WiFi connected");
  ui_set_wifi_status(true);

  if (settings_has_speaker()) {
    start_with_saved_speaker();
  } else {
    xTaskCreatePinnedToCore(discover_and_connect_task, "discover", 6144,
                            nullptr, NET_TASK_PRIO, nullptr, NET_TASK_CORE);
  }
}

static void on_wifi_disconnected(void *, esp_event_base_t, int32_t, void *) {
  ESP_LOGW(TAG, "WiFi disconnected");
  ui_set_wifi_status(false);
  sonos_stop();
}

static void on_play_requested(void *, esp_event_base_t, int32_t, void *) {
  ESP_LOGI(TAG, "Play requested — station: %s", STATIONS[s_station_index].name);
  sonos_play_uri(STATIONS[s_station_index].url);
}

static void on_stop_requested(void *, esp_event_base_t, int32_t, void *) {
  ESP_LOGI(TAG, "Stop requested");
  sonos_stop_playback();
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

static void register_events() {
  esp_event_loop_create_default();

  esp_event_handler_register(APP_EVENT, APP_EVENT_ENCODER_ROTATE,
                             on_encoder_rotate, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_STATION_CHANGED,
                             on_station_changed, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_VOLUME_CHANGED,
                             on_volume_changed, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_PLAY_REQUESTED,
                             on_play_requested, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_STOP_REQUESTED,
                             on_stop_requested, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_CONNECTED,
                             on_wifi_connected, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_DISCONNECTED,
                             on_wifi_disconnected, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_SONOS_STATE_UPDATE,
                             on_sonos_state, nullptr);
}

extern "C" void app_main() {
  ESP_LOGI(TAG, "Sonos Radio starting");

  init_nvs();
  settings_init();
  settings_load_config_from_sd();

  s_volume = settings_get_volume();
  s_station_index = settings_get_station_index();

  register_events();
  ui_init();
  encoder_init();
  discovery_init();
  sonos_init();
  wifi_manager_init();

  ESP_LOGI(TAG, "Init complete — station: %s, volume: %d",
           STATIONS[s_station_index].name, s_volume);
}
