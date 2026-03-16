#include "ui_bridge.h"
#include "app_events.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <cstring>

static constexpr const char *TAG = "ui_bridge";

// Command types for the UI queue
typedef enum {
    CMD_SET_VOLUME,
    CMD_SET_STATION,
    CMD_SET_PLAY_STATE,
    CMD_SET_SPEAKER,
    CMD_SET_WIFI_STATUS,
    CMD_SHOW_VOICE,
    CMD_SET_VOICE_STATE,
    CMD_SET_VOICE_TRANSCRIPT,
    CMD_SHOW_TIMER,
    CMD_SHOW_SPEAKER_PICKER,
} ui_cmd_type_t;

typedef struct {
    ui_cmd_type_t type;
    union {
        uint8_t volume;
        struct { uint8_t index; char name[32]; uint32_t color; } station;
        uint8_t play_state;
        char speaker_name[32];
        bool wifi_connected;
        bool voice_active;
        uint8_t voice_state;
        struct { char text[128]; bool is_user; } transcript;
        struct { uint32_t remaining; char label[32]; } timer;
    };
} ui_cmd_t;

static QueueHandle_t s_cmd_queue = nullptr;

void ui_bridge_init(void) {
    s_cmd_queue = xQueueCreate(16, sizeof(ui_cmd_t));
    ESP_LOGI(TAG, "UI bridge initialized");
    // TODO: Call existing ui_init() here
}

void ui_bridge_task_run(void) {
    ESP_LOGI(TAG, "UI bridge task started");
    ui_cmd_t cmd;
    while (true) {
        // Process queued commands
        while (xQueueReceive(s_cmd_queue, &cmd, 0) == pdTRUE) {
            switch (cmd.type) {
                case CMD_SET_VOLUME:
                    ESP_LOGI(TAG, "Set volume: %d", cmd.volume);
                    // TODO: Call ui_set_volume(cmd.volume)
                    break;
                case CMD_SET_PLAY_STATE:
                    ESP_LOGI(TAG, "Set play state: %d", cmd.play_state);
                    // TODO: Call ui_set_play_state(...)
                    break;
                case CMD_SET_WIFI_STATUS:
                    ESP_LOGI(TAG, "Set WiFi: %s", cmd.wifi_connected ? "connected" : "disconnected");
                    // TODO: Call ui_set_wifi_status(cmd.wifi_connected)
                    break;
                default:
                    break;
            }
        }
        // TODO: Call lv_timer_handler()
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ui_bridge_set_volume(uint8_t volume) {
    if (!s_cmd_queue) return;
    ui_cmd_t cmd = {};
    cmd.type = CMD_SET_VOLUME;
    cmd.volume = volume;
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

void ui_bridge_set_station(uint8_t index, const char* name, uint32_t color) {
    if (!s_cmd_queue) return;
    ui_cmd_t cmd = {};
    cmd.type = CMD_SET_STATION;
    cmd.station.index = index;
    strncpy(cmd.station.name, name ? name : "", sizeof(cmd.station.name) - 1);
    cmd.station.color = color;
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

void ui_bridge_set_play_state(uint8_t state) {
    if (!s_cmd_queue) return;
    ui_cmd_t cmd = {};
    cmd.type = CMD_SET_PLAY_STATE;
    cmd.play_state = state;
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

void ui_bridge_set_speaker(const char* name) {
    if (!s_cmd_queue) return;
    ui_cmd_t cmd = {};
    cmd.type = CMD_SET_SPEAKER;
    strncpy(cmd.speaker_name, name ? name : "", sizeof(cmd.speaker_name) - 1);
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

void ui_bridge_set_wifi_status(bool connected) {
    if (!s_cmd_queue) return;
    ui_cmd_t cmd = {};
    cmd.type = CMD_SET_WIFI_STATUS;
    cmd.wifi_connected = connected;
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

void ui_bridge_show_voice_mode(bool active) {
    if (!s_cmd_queue) return;
    ui_cmd_t cmd = {};
    cmd.type = CMD_SHOW_VOICE;
    cmd.voice_active = active;
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

void ui_bridge_set_voice_state(uint8_t state) {
    if (!s_cmd_queue) return;
    ui_cmd_t cmd = {};
    cmd.type = CMD_SET_VOICE_STATE;
    cmd.voice_state = state;
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

void ui_bridge_set_voice_transcript(const char* text, bool is_user) {
    if (!s_cmd_queue) return;
    ui_cmd_t cmd = {};
    cmd.type = CMD_SET_VOICE_TRANSCRIPT;
    strncpy(cmd.transcript.text, text ? text : "", sizeof(cmd.transcript.text) - 1);
    cmd.transcript.is_user = is_user;
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

void ui_bridge_show_timer(uint32_t remaining_sec, const char* label) {
    if (!s_cmd_queue) return;
    ui_cmd_t cmd = {};
    cmd.type = CMD_SHOW_TIMER;
    cmd.timer.remaining = remaining_sec;
    strncpy(cmd.timer.label, label ? label : "", sizeof(cmd.timer.label) - 1);
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50));
}

void ui_bridge_show_speaker_picker(const char** names, const char** ips, uint8_t count) {
    // TODO: Implement speaker picker bridge
    ESP_LOGI(TAG, "Speaker picker requested (%d speakers)", count);
}
