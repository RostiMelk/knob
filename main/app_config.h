#pragma once

#include "esp_event.h"
#include <cstdint>

// ─── Display: ST77916 QSPI 360×360 ─────────────────────────────────────────

constexpr int PIN_LCD_CS = 14;
constexpr int PIN_LCD_CLK = 13;
constexpr int PIN_LCD_SIO0 = 15;
constexpr int PIN_LCD_SIO1 = 16;
constexpr int PIN_LCD_SIO2 = 17;
constexpr int PIN_LCD_SIO3 = 18;
constexpr int PIN_LCD_RST = 21;
constexpr int PIN_LCD_BL = 47;

constexpr int LCD_H_RES = 360;
constexpr int LCD_V_RES = 360;
constexpr int LCD_DRAW_ROWS =
    72; // partial rows per flush — 5 flushes/frame, ~52KB per DMA buffer
constexpr int LCD_BL_LEDC_CH = 0;

// ─── Touch: CST816D I2C ─────────────────────────────────────────────────────

constexpr int PIN_TOUCH_SDA = 11;
constexpr int PIN_TOUCH_SCL = 12;
constexpr int PIN_TOUCH_INT = 9;
constexpr int PIN_TOUCH_RST = 10;

constexpr int TOUCH_I2C_NUM = 0;
constexpr int TOUCH_I2C_FREQ = 400'000;

// ─── Rotary Encoder (no push button — selection is via touchscreen) ─────────

constexpr int PIN_ENC_A = 8;
constexpr int PIN_ENC_B = 7;

// ─── I2C Bus (shared: touch + DRV2605 haptics) ─────────────────────────────

constexpr uint8_t DRV2605_ADDR = 0x5A;
constexpr uint8_t TOUCH_I2C_ADDR = 0x15;

// ─── Audio (Phase 2 — not used in MVP) ──────────────────────────────────────

constexpr int PIN_I2S_BCLK = 39;
constexpr int PIN_I2S_WS = 40;
constexpr int PIN_I2S_DOUT = 41;
constexpr int PIN_PDM_CLK = 45;
constexpr int PIN_PDM_DATA = 46;

// ─── SD Card (SDMMC 4-wire) ─────────────────────────────────────────────────

constexpr int PIN_SD_CMD = 3;
constexpr int PIN_SD_CLK = 4;
constexpr int PIN_SD_D0 = 5;
constexpr int PIN_SD_D1 = 6;
constexpr int PIN_SD_D2 = 42;
constexpr int PIN_SD_D3 = 2;

constexpr const char *SD_MOUNT_POINT = "/sdcard";
constexpr const char *SD_CONFIG_PATH = "/sdcard/.env";

// ─── Sonos ──────────────────────────────────────────────────────────────────

constexpr int SONOS_PORT = 1400;
constexpr int SONOS_HTTP_TIMEOUT_MS = 4000;

// ─── Volume ─────────────────────────────────────────────────────────────────

constexpr int VOLUME_MIN = 0;
constexpr int VOLUME_MAX = 100;
constexpr int VOLUME_STEP = 2;
constexpr int VOLUME_OVERLAY_TIMEOUT_MS = 1500;

// ─── Radio Stations ─────────────────────────────────────────────────────────

struct Station {
  const char *name;
  const char *url;
  const char *logo;
  uint32_t color;
};

constexpr Station STATIONS[] = {
    {"NRK P1 Oslo",
     "https://cdn0-47115-liveicecast0.dna.contentdelivery.net/p1_mp3_h",
     "nrk_p1.png", 0x1A1A2E},
    {"NRK P2",
     "https://cdn0-47115-liveicecast0.dna.contentdelivery.net/p2_aac_h",
     "nrk_p2.png", 0x16213E},
    {"NRK P3",
     "https://cdn0-47115-liveicecast0.dna.contentdelivery.net/p3_mp3_h",
     "nrk_p3.png", 0x0F3460},
    {"NRK MP3",
     "https://cdn0-47115-liveicecast0.dna.contentdelivery.net/mp3_mp3_h",
     "nrk_mp3.png", 0x1A2A4A},
    {"NRK Jazz",
     "http://cdn0-47115-liveicecast0.dna.contentdelivery.net/jazz_mp3_h",
     "nrk_jazz.png", 0x2D1B69},
    {"P4 Norge", "https://p4.p4groupaudio.com/P04_MM", "p4.png", 0x8B1A1A},
    {"P5 Hits", "https://p5.p4groupaudio.com/P05_MM", "p5.png", 0x1A5C2E},
    {"P9 Retro", "https://p9.p4groupaudio.com/P09_MH", "p9.png", 0x6B3A1A},
    {"Radio Rock", "https://live-bauerno.sharp-stream.com/radiorock_no_mp3",
     "radio_rock.png", 0x5C1010},
    {"Radio Norge", "https://live-bauerno.sharp-stream.com/radionorge_no_mp3",
     "radio_norge.png", 0x1A3A5C},
    {"NRJ Norge", "https://live-bauerno.sharp-stream.com/kiss_no_mp3",
     "nrj.png", 0x5C1A3A},
};

constexpr int STATION_COUNT = sizeof(STATIONS) / sizeof(STATIONS[0]);

// ─── Voice Mode ─────────────────────────────────────────────────────────────

constexpr int VOICE_TASK_STACK = 12288;
constexpr int VOICE_TASK_PRIO = 5;
constexpr int VOICE_TASK_CORE = 1;

constexpr int VOICE_DUCKED_VOLUME = 1;
constexpr int VOICE_IDLE_TIMEOUT_MS = 8000;
constexpr int DOUBLE_TAP_WINDOW_MS = 300;

constexpr const char *OPENAI_REALTIME_URL =
    "wss://api.openai.com/v1/realtime?model=gpt-realtime-1.5";
constexpr const char *OPENAI_VOICE = "cedar";

enum class VoiceState : uint8_t {
  Inactive,
  Connecting,
  Listening,
  Thinking,
  Speaking,
};

// ─── Application Events ─────────────────────────────────────────────────────

ESP_EVENT_DECLARE_BASE(APP_EVENT);

enum : int32_t {
  APP_EVENT_WIFI_CONNECTED,
  APP_EVENT_WIFI_DISCONNECTED,
  APP_EVENT_ENCODER_ROTATE,     // data: int32_t delta (+/- steps)
  APP_EVENT_STATION_CHANGED,    // data: int32_t station index
  APP_EVENT_VOLUME_CHANGED,     // data: int32_t new volume 0–100
  APP_EVENT_PLAY_REQUESTED,     // user wants to start playing current station
  APP_EVENT_STOP_REQUESTED,     // user wants to stop and release speaker
  APP_EVENT_SONOS_STATE_UPDATE, // data: SonosState*
  APP_EVENT_VOICE_ACTIVATE,     // double-tap detected → enter voice mode
  APP_EVENT_VOICE_DEACTIVATE,   // timeout or user exit → leave voice mode
  APP_EVENT_VOICE_STATE,        // data: VoiceState enum
  APP_EVENT_VOICE_TRANSCRIPT, // data: null-terminated string (user or AI text)
  APP_EVENT_TIMER_STARTED,    // data: int32_t total seconds
  APP_EVENT_TIMER_FIRED,      // data: null-terminated label string
};

// ─── Sonos State (passed with APP_EVENT_SONOS_STATE_UPDATE) ─────────────────

enum class PlayState : uint8_t {
  Stopped,
  Playing,
  Paused,
  Transitioning,
  Unknown,
};

struct SonosState {
  PlayState play_state;
  int volume;
  int station_index; // -1 if URI doesn't match any known station
};

// ─── Task Config ────────────────────────────────────────────────────────────

constexpr int UI_TASK_STACK = 8192;
constexpr int UI_TASK_PRIO = 5;
constexpr int UI_TASK_CORE = 0;

constexpr int NET_TASK_STACK = 6144;
constexpr int NET_TASK_PRIO = 4;
constexpr int NET_TASK_CORE = 1;
