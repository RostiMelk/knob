#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Event base for app events (used by both C and Rust) */
#define APP_EVENT_BASE "APP_EVENT"

typedef enum {
    /* Input events (C -> Rust) */
    APP_EVT_ENCODER_ROTATE = 0,
    APP_EVT_TOUCH_TAP,
    APP_EVT_TOUCH_LONG_PRESS,
    APP_EVT_TOUCH_DOUBLE_TAP,

    /* Playback control (Rust -> C, or C -> Rust) */
    APP_EVT_STATION_CHANGED,
    APP_EVT_VOLUME_CHANGED,
    APP_EVT_PLAY_REQUESTED,
    APP_EVT_STOP_REQUESTED,

    /* Sonos state (C -> Rust -> C UI) */
    APP_EVT_SONOS_STATE_UPDATE,

    /* WiFi (C -> Rust) */
    APP_EVT_WIFI_CONNECTED,
    APP_EVT_WIFI_DISCONNECTED,

    /* Voice (Rust -> C UI) */
    APP_EVT_VOICE_ACTIVATE,
    APP_EVT_VOICE_DEACTIVATE,
    APP_EVT_VOICE_STATE,
    APP_EVT_VOICE_TRANSCRIPT,

    /* Timer (Rust -> C UI) */
    APP_EVT_TIMER_STARTED,
    APP_EVT_TIMER_FIRED,
} app_event_id_t;

#ifdef __cplusplus
}
#endif
