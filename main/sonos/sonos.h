#pragma once

#include <cstdint>

void sonos_init();
void sonos_start();
void sonos_stop();

void sonos_set_speaker(const char *ip, int port = 1400);

void sonos_play_uri(const char *uri);
void sonos_play();
void sonos_pause();
void sonos_stop_playback();
void sonos_set_volume(int level);

// Download album art JPEG from the given URL into a caller-provided buffer.
// Returns the number of bytes written, or 0 on failure.
int sonos_fetch_art(const char *url, uint8_t *buf, int buf_size);
