#pragma once

#include <cstdio>

#include "app_config.h"
#include "ui/ui.h"

inline void sonos_init() {}
inline void sonos_start() {}
inline void sonos_stop() {}

inline void sonos_set_speaker(const char *ip, int port = 1400) {
  printf("[sim] sonos_set_speaker(%s:%d)\n", ip, port);
}

inline void sonos_play_uri(const char *uri) {
  printf("[sim] sonos_play_uri(%s)\n", uri);
  ui_set_play_state(PlayState::Playing);
}

inline void sonos_stop_playback() {
  printf("[sim] sonos_stop_playback()\n");
  ui_set_play_state(PlayState::Stopped);
}

inline void sonos_set_volume(int level) {
  printf("[sim] sonos_set_volume(%d)\n", level);
}
