#pragma once

#include "app_config.h"
#include "sonos/discovery.h"

void ui_init();

void ui_set_volume(int level);
void ui_set_play_state(PlayState state);
void ui_set_station(int index);
void ui_set_wifi_status(bool connected);
void ui_set_speaker_name(const char *name);

void ui_show_speaker_picker(const DiscoveryResult *speakers);
void ui_show_scanning();

void ui_on_encoder_rotate(int32_t steps);
void ui_on_touch_tap();
