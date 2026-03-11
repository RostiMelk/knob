#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* === Lifecycle === */
void ui_bridge_init(void);
void ui_bridge_task_run(void);  /* Blocks — run in dedicated FreeRTOS task */

/* === UI Updates (thread-safe, queued internally) === */
void ui_bridge_set_volume(uint8_t volume);
void ui_bridge_set_station(uint8_t index, const char* name, uint32_t color);
void ui_bridge_set_play_state(uint8_t state);  /* 0=stopped, 1=playing, 2=transitioning */
void ui_bridge_set_speaker(const char* name);
void ui_bridge_set_wifi_status(bool connected);
void ui_bridge_show_voice_mode(bool active);
void ui_bridge_set_voice_state(uint8_t state);  /* 0=connecting, 1=listening, 2=thinking, 3=speaking */
void ui_bridge_set_voice_transcript(const char* text, bool is_user);
void ui_bridge_show_timer(uint32_t remaining_sec, const char* label);
void ui_bridge_show_speaker_picker(const char** names, const char** ips, uint8_t count);

#ifdef __cplusplus
}
#endif
