#pragma once

void sonos_init();
void sonos_start();
void sonos_stop();

void sonos_set_speaker(const char *ip, int port = 1400);

void sonos_play_uri(const char *uri);
void sonos_stop_playback();
void sonos_set_volume(int level);
