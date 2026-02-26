#pragma once

#include <cstdio>

inline void sonos_init() {}
inline void sonos_start() {}
inline void sonos_stop() {}

inline void sonos_set_speaker(const char *ip, int port = 1400) {
  printf("[sim] sonos_set_speaker(%s:%d)\n", ip, port);
}

inline void sonos_play_uri(const char *uri) {
  printf("[sim] sonos_play_uri(%s)\n", uri);
}

inline void sonos_stop_playback() { printf("[sim] sonos_stop_playback()\n"); }

inline void sonos_set_volume(int level) {
  printf("[sim] sonos_set_volume(%d)\n", level);
}
