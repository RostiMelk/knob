#include "app_config.h"
#include "cal_fetch.h"
#include "display.h"
#include "encoder.h"
#include "haptic.h"
#include "ical_parser.h"
#include "settings.h"
#include "ui/ui.h"
#include "weather.h"
#include "wifi_manager.h"

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>
#include <ctime>

static constexpr const char *TAG = "main";

ESP_EVENT_DEFINE_BASE(APP_EVENT);

static esp_timer_handle_t s_fetch_timer;
static bool s_wifi_connected = false;
static bool s_ntp_synced = false;

// ─── Calendar Fetch ─────────────────────────────────────────────────────────

static void do_fetch() {
  size_t len = 0;
  char *ical = cal_fetch_ical(&len);
  if (!ical) {
    ESP_LOGW(TAG, "iCal fetch failed");
    return;
  }

  // Allocate events in PSRAM — too large for task stack
  auto *events = static_cast<CalEvent *>(
      heap_caps_malloc(MAX_EVENTS * sizeof(CalEvent), MALLOC_CAP_SPIRAM));
  if (!events) {
    ESP_LOGE(TAG, "PSRAM alloc failed for events");
    heap_caps_free(ical);
    return;
  }

  time_t now = time(nullptr);
  int count = ical_parse(ical, len, events, MAX_EVENTS, now);
  heap_caps_free(ical);

  ESP_LOGI(TAG, "Fetched %d upcoming event(s)", count);
  ui_set_events(events, count);
  heap_caps_free(events);
}

static void fetch_task(void *) {
  do_fetch();
  vTaskDelete(nullptr);
}

static void start_fetch() {
  xTaskCreatePinnedToCore(fetch_task, "cal_fetch", 16384, nullptr,
                          NET_TASK_PRIO, nullptr, NET_TASK_CORE);
}

static void on_fetch_timer(void *) {
  if (s_wifi_connected)
    start_fetch();
}

// ─── NTP ────────────────────────────────────────────────────────────────────

static void wait_for_ntp_task(void *) {
  // Wait up to 10s for NTP to sync
  for (int i = 0; i < 100; ++i) {
    time_t now = time(nullptr);
    if (now > 1700000000) {
      ESP_LOGI(TAG, "NTP synced");
      s_ntp_synced = true;
      start_fetch();
      vTaskDelete(nullptr);
      return;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  ESP_LOGW(TAG, "NTP sync timeout — fetching anyway");
  s_ntp_synced = true;
  start_fetch();
  vTaskDelete(nullptr);
}

// ─── Event Handlers ─────────────────────────────────────────────────────────

static void on_weather_update(void *, esp_event_base_t, int32_t, void *data) {
  auto *w = static_cast<WeatherData *>(data);
  ui_set_weather(w);
}

static void on_wifi_connected(void *, esp_event_base_t, int32_t, void *) {
  ESP_LOGI(TAG, "WiFi connected");
  s_wifi_connected = true;
  ui_set_wifi_status(true);
  weather_start();

  // Set timezone and start NTP
  setenv("TZ", CONFIG_CALENDAR_TIMEZONE, 1);
  tzset();

  static bool sntp_started = false;
  if (!sntp_started) {
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);
    sntp_started = true;
    ESP_LOGI(TAG, "SNTP started, TZ=%s", CONFIG_CALENDAR_TIMEZONE);
  }

  cal_fetch_init();

  // Wait for NTP then do first fetch
  xTaskCreatePinnedToCore(wait_for_ntp_task, "ntp_wait", 4096, nullptr,
                          NET_TASK_PRIO, nullptr, NET_TASK_CORE);

  // Start periodic refresh timer
  if (!s_fetch_timer) {
    esp_timer_create_args_t args = {};
    args.callback = on_fetch_timer;
    args.name = "cal_refresh";
    ESP_ERROR_CHECK(esp_timer_create(&args, &s_fetch_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(
        s_fetch_timer,
        static_cast<uint64_t>(CONFIG_CALENDAR_FETCH_INTERVAL_S) * 1000000ULL));
    ESP_LOGI(TAG, "Refresh timer: every %ds", CONFIG_CALENDAR_FETCH_INTERVAL_S);
  }
}

static void on_wifi_disconnected(void *, esp_event_base_t, int32_t, void *) {
  ESP_LOGW(TAG, "WiFi disconnected");
  s_wifi_connected = false;
  ui_set_wifi_status(false);
}

static void on_encoder_rotate(void *, esp_event_base_t, int32_t, void *data) {
  auto steps = *static_cast<int32_t *>(data);
  ui_on_encoder_rotate(steps);
}

// ─── Init ───────────────────────────────────────────────────────────────────

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
  ESP_LOGI(TAG, "Calendar starting");

  init_nvs();
  settings_init();

  esp_event_loop_create_default();

  esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_CONNECTED,
                             on_wifi_connected, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_DISCONNECTED,
                             on_wifi_disconnected, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_ENCODER_ROTATE,
                             on_encoder_rotate, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_WEATHER_UPDATE,
                             on_weather_update, nullptr);

  ui_init();
  haptic_init();
  encoder_init();
  weather_init();
  wifi_manager_init();

  ESP_LOGI(TAG, "Init complete");
}
