#include "app_config.h"
#include "spotify/spotify_auth.h"
#include "spotify/spotify_api.h"
#include "ui/ui.h"

#include "hal_pins.h"
#include "knob_events.h"
#include "display.h"
#include "encoder.h"
#include "haptic.h"
#include "wifi_manager.h"
#include "settings.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "lvgl.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include <cstdlib>
#include <cstring>

static constexpr const char *TAG = "spotify";

ESP_EVENT_DEFINE_BASE(APP_EVENT);

// ─── Kconfig accessors (from sdkconfig)

extern "C" const char *spotify_client_id(void) {
#ifdef CONFIG_SPOTIFY_CLIENT_ID
    return CONFIG_SPOTIFY_CLIENT_ID;
#else
    return "";
#endif
}

extern "C" const char *spotify_refresh_token(void) {
#ifdef CONFIG_SPOTIFY_REFRESH_TOKEN
    return CONFIG_SPOTIFY_REFRESH_TOKEN;
#else
    return "";
#endif
}

// ─── Command queue for API calls (runs on a dedicated task with enough stack)

enum class CmdType : uint8_t { SetVolume, Play, Pause, Next, Prev, DjSpin };

struct Cmd {
    CmdType type;
    int32_t value; // for SetVolume
};

static QueueHandle_t s_cmd_queue = nullptr;

static void cmd_task(void *) {
    Cmd cmd;
    while (true) {
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE)
            continue;

        // Drain any queued volume changes — only send the latest
        if (cmd.type == CmdType::SetVolume) {
            Cmd newer;
            while (xQueueReceive(s_cmd_queue, &newer, 0) == pdTRUE) {
                if (newer.type == CmdType::SetVolume) {
                    cmd.value = newer.value; // keep latest volume
                } else {
                    // Non-volume command — process volume first, then this
                    spotify_api_set_volume(cmd.value);
                    cmd = newer;
                    break;
                }
            }
        }

        switch (cmd.type) {
        case CmdType::SetVolume:
            spotify_api_set_volume(cmd.value);
            break;
        case CmdType::Play:
            spotify_api_play_pause(false); // not playing -> play
            break;
        case CmdType::Pause:
            spotify_api_play_pause(true); // playing -> pause
            break;
        case CmdType::Next:
            spotify_api_next();
            break;
        case CmdType::Prev:
            spotify_api_prev();
            break;
        case CmdType::DjSpin:
            haptic_buzz();
            spotify_api_play_random_liked();
            break;
        }
    }
}

static void enqueue_cmd(CmdType type, int32_t value = 0) {
    Cmd cmd = {type, value};
    xQueueSend(s_cmd_queue, &cmd, 0);
}

// ─── Volume debounce
static int s_pending_volume = -1;
static int64_t s_volume_send_at = 0;

// ─── Event handlers

static void on_wifi_connected(void *, esp_event_base_t, int32_t, void *) {
    ESP_LOGI(TAG, "WiFi connected");

    ui_set_status("WiFi connected\nFetching token...");
    spotify_auth_init();

    const char *token = spotify_auth_get_token();
    if (token) {
        ui_set_status("Connected\nWaiting for playback...");
        spotify_api_init();
        spotify_api_start();
    } else {
        ESP_LOGE(TAG, "Failed to get Spotify token — check credentials");
        ui_set_status("Token failed\nCheck credentials in\nsdkconfig.defaults.local");
    }
}

static void on_wifi_disconnected(void *, esp_event_base_t, int32_t, void *) {
    ESP_LOGW(TAG, "WiFi disconnected");
    spotify_api_stop();
    ui_set_status("WiFi disconnected\nReconnecting...");
}

static void on_spotify_state(void *, esp_event_base_t, int32_t, void *data) {
    auto *state = static_cast<SpotifyState *>(data);
    ui_update_state(state);
}

static void on_volume_set(void *, esp_event_base_t, int32_t, void *data) {
    auto vol = *static_cast<int32_t *>(data);
    s_pending_volume = vol;
    s_volume_send_at = esp_timer_get_time() + 200000;
}

static void on_play(void *, esp_event_base_t, int32_t, void *) {
    enqueue_cmd(CmdType::Play);
}

static void on_pause(void *, esp_event_base_t, int32_t, void *) {
    enqueue_cmd(CmdType::Pause);
}

static void on_next(void *, esp_event_base_t, int32_t, void *) {
    enqueue_cmd(CmdType::Next);
}

static void on_prev(void *, esp_event_base_t, int32_t, void *) {
    enqueue_cmd(CmdType::Prev);
}

static void on_dj_spin(void *, esp_event_base_t, int32_t, void *) {
    ESP_LOGI(TAG, "DJ SPIN!");
    enqueue_cmd(CmdType::DjSpin);
}

// ─── Volume debounce timer — just enqueues, no HTTP here
static void volume_timer_cb(void *) {
    if (s_pending_volume >= 0 &&
        esp_timer_get_time() >= s_volume_send_at) {
        enqueue_cmd(CmdType::SetVolume, s_pending_volume);
        s_pending_volume = -1;
    }
}

// ─── NVS init

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
    ESP_LOGI(TAG, "Spotify Knob starting");

    init_nvs();
    settings_init();

    // Command queue + task for API calls (needs big stack for HTTP+TLS)
    s_cmd_queue = xQueueCreate(8, sizeof(Cmd));
    xTaskCreatePinnedToCore(cmd_task, "spotify_cmd", 8192, nullptr, 5,
                            nullptr, 1);

    // Event loop
    esp_event_loop_create_default();

    esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_CONNECTED,
                               on_wifi_connected, nullptr);
    esp_event_handler_register(APP_EVENT, APP_EVENT_WIFI_DISCONNECTED,
                               on_wifi_disconnected, nullptr);
    esp_event_handler_register(APP_EVENT, APP_EVENT_SPOTIFY_STATE_UPDATE,
                               on_spotify_state, nullptr);
    esp_event_handler_register(APP_EVENT, APP_EVENT_SPOTIFY_VOLUME_SET,
                               on_volume_set, nullptr);
    esp_event_handler_register(APP_EVENT, APP_EVENT_SPOTIFY_PLAY,
                               on_play, nullptr);
    esp_event_handler_register(APP_EVENT, APP_EVENT_SPOTIFY_PAUSE,
                               on_pause, nullptr);
    esp_event_handler_register(APP_EVENT, APP_EVENT_SPOTIFY_NEXT,
                               on_next, nullptr);
    esp_event_handler_register(APP_EVENT, APP_EVENT_SPOTIFY_PREV,
                               on_prev, nullptr);
    esp_event_handler_register(APP_EVENT, APP_EVENT_SPOTIFY_DJ_SPIN,
                               on_dj_spin, nullptr);

    // Hardware + UI
    ui_init();
    haptic_init();
    encoder_init();

    // Volume debounce timer (100ms periodic) — only enqueues, doesn't do HTTP
    esp_timer_create_args_t timer_args = {};
    timer_args.callback = volume_timer_cb;
    timer_args.name = "vol_debounce";
    esp_timer_handle_t vol_timer = nullptr;
    esp_timer_create(&timer_args, &vol_timer);
    esp_timer_start_periodic(vol_timer, 100000); // 100ms

    // Start WiFi
    wifi_manager_init();

    ESP_LOGI(TAG, "Init complete");
}
