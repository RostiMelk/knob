#include "app_config.h"
#include "kaffi/sanity_api.h"
#include "ui/ui.h"

#include "encoder.h"
#include "haptic.h"
#include "knob_events.h"
#include "settings.h"
#include "wifi_manager.h"

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <cstdlib>
#include <cstring>
#include <ctime>

static constexpr const char *TAG = "kaffi";

#ifndef CONFIG_KAFFI_TIMEZONE
#define CONFIG_KAFFI_TIMEZONE "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

ESP_EVENT_DEFINE_BASE(APP_EVENT);

// ─── Command queue: all HTTP runs on one task with a big stack (TLS heavy)

struct Cmd {
  enum class Type { Log, Refresh } type;
  KaffiLogCmd log;
};

static QueueHandle_t s_cmd_queue = nullptr;
static QueueHandle_t s_avatar_queue = nullptr;
static esp_timer_handle_t s_refresh_timer = nullptr;
static bool s_net_ready = false;   // one-time network init done
static bool s_have_people = false; // at least one successful fetch

static void enqueue(Cmd cmd);

static void cmd_task(void *) {
  Cmd cmd;
  while (true) {
    if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE)
      continue;

    switch (cmd.type) {
    case Cmd::Type::Log:
      sanity_api_log(cmd.log.person_id, cmd.log.person_name, cmd.log.kind,
                     cmd.log.quantity);
      sanity_api_fetch_people(); // reflect the new total
      break;
    case Cmd::Type::Refresh:
      if (!sanity_api_fetch_people() && !s_have_people) {
        // Nothing on screen yet — keep trying rather than parking on an
        // error. Once people are loaded, the periodic refresh heals things
        // quietly instead.
        ui_set_status("Can't reach Sanity\nRetrying...");
        vTaskDelay(pdMS_TO_TICKS(5000));
        Cmd retry;
        retry.type = Cmd::Type::Refresh;
        enqueue(retry);
      }
      break;
    }
  }
}

// Avatars run on their own task so a burst of preloads never delays a log
// write. Failures post a null-pixel AvatarReady so the UI can retry later.
static void avatar_task(void *) {
  AvatarReq req;
  while (true) {
    if (xQueueReceive(s_avatar_queue, &req, portMAX_DELAY) != pdTRUE)
      continue;
    unsigned char *px = nullptr;
    int w = 0, h = 0;
    bool ok = sanity_api_fetch_avatar(req.url, &px, &w, &h);
    AvatarReady ar = {};
    strncpy(ar.url, req.url, sizeof(ar.url) - 1);
    ar.pixels = ok ? px : nullptr;
    ar.w = w;
    ar.h = h;
    if (esp_event_post(APP_EVENT, APP_EVENT_KAFFI_AVATAR_READY, &ar, sizeof(ar),
                       pdMS_TO_TICKS(100)) != ESP_OK &&
        ar.pixels) {
      heap_caps_free(ar.pixels); // nobody will take ownership
    }
  }
}

static void enqueue(Cmd cmd) {
  if (s_cmd_queue)
    xQueueSend(s_cmd_queue, &cmd, 0);
}

// ─── Event handlers

// Runs on the event loop task, so keep it quick — the actual fetch (TLS,
// slow) is queued onto the command task.
static void on_wifi_connected(void *, esp_event_base_t, int32_t, void *) {
  ESP_LOGI(TAG, "WiFi connected");
  if (!s_net_ready) {
    s_net_ready = true;
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);
    setenv("TZ", CONFIG_KAFFI_TIMEZONE, 1);
    tzset();
    sanity_api_init();
    if (s_refresh_timer) {
      esp_timer_start_periodic(s_refresh_timer,
                               static_cast<uint64_t>(KAFFI_REFRESH_MS) * 1000);
    }
  }

  if (!s_have_people)
    ui_set_status("Loading people...");

  Cmd cmd;
  cmd.type = Cmd::Type::Refresh;
  enqueue(cmd);
}

static void on_wifi_disconnected(void *, esp_event_base_t, int32_t, void *) {
  ESP_LOGW(TAG, "WiFi disconnected");
  // With people already loaded the knob keeps working from memory — a blip
  // shouldn't yank the screen away. Reconnect refetches in the background.
  if (!s_have_people)
    ui_set_status("WiFi disconnected\nReconnecting...");
}

static void on_people_update(void *, esp_event_base_t, int32_t, void *data) {
  s_have_people = true;
  ui_update_people(static_cast<KaffiState *>(data));
}

static void on_log(void *, esp_event_base_t, int32_t, void *data) {
  Cmd cmd;
  cmd.type = Cmd::Type::Log;
  cmd.log = *static_cast<KaffiLogCmd *>(data);
  enqueue(cmd);
}

static void on_refresh(void *, esp_event_base_t, int32_t, void *) {
  Cmd cmd;
  cmd.type = Cmd::Type::Refresh;
  enqueue(cmd);
}

// Called directly by the UI. We bypass the esp_event loop here because its
// queue is small (~32) and a burst of ~35 preload requests would overflow it,
// silently dropping the last few people's avatars.
void kaffi_request_avatar(const char *url) {
  if (!s_avatar_queue || !url || !url[0])
    return;
  AvatarReq req = {};
  strncpy(req.url, url, sizeof(req.url) - 1);
  xQueueSend(s_avatar_queue, &req, 0);
}

static void on_avatar_ready(void *, esp_event_base_t, int32_t, void *data) {
  auto *ar = static_cast<AvatarReady *>(data);
  ui_set_avatar(ar->url, ar->pixels, ar->w, ar->h);
}

static void refresh_timer_cb(void *) {
  esp_event_post(APP_EVENT, APP_EVENT_KAFFI_REFRESH, nullptr, 0, 0);
}

// ─── NVS

static void init_nvs() {
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);
}

// ─── Entry point

extern "C" void app_main() {
  ESP_LOGI(TAG, "Kaffi starting");

  init_nvs();
  settings_init();

  s_cmd_queue = xQueueCreate(8, sizeof(Cmd));
  xTaskCreatePinnedToCore(cmd_task, "kaffi_cmd", 8192, nullptr, 5, nullptr, 1);

  // Avatar preloads run at lower priority so logging stays snappy.
  s_avatar_queue = xQueueCreate(KAFFI_MAX_PEOPLE + 4, sizeof(AvatarReq));
  xTaskCreatePinnedToCore(avatar_task, "kaffi_avatar", 8192, nullptr, 4,
                          nullptr, 1);

  esp_event_loop_create_default();
  esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_CONNECTED,
                             on_wifi_connected, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_DISCONNECTED,
                             on_wifi_disconnected, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_KAFFI_PEOPLE_UPDATE,
                             on_people_update, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_KAFFI_LOG, on_log, nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_KAFFI_REFRESH, on_refresh,
                             nullptr);
  esp_event_handler_register(APP_EVENT, APP_EVENT_KAFFI_AVATAR_READY,
                             on_avatar_ready, nullptr);

  esp_timer_create_args_t timer_args = {};
  timer_args.callback = refresh_timer_cb;
  timer_args.name = "kaffi_refresh";
  esp_timer_create(&timer_args, &s_refresh_timer);

  ui_init();
  ui_show_splash();
  haptic_init();
  encoder_init();

  ui_set_status("Connecting to WiFi...");
  wifi_manager_init();

  ESP_LOGI(TAG, "Init complete");
}
