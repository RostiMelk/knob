#pragma once

#include <cstddef>
#include <cstdint>

void settings_init();

bool settings_load_config_from_sd();

int settings_get_volume();
void settings_set_volume(int volume);

int settings_get_station_index();
void settings_set_station_index(int index);

void settings_get_speaker_ip(char *buf, size_t len);
void settings_set_speaker_ip(const char *ip);

void settings_get_speaker_name(char *buf, size_t len);
void settings_set_speaker_name(const char *name);

bool settings_has_speaker();

void settings_get_wifi_ssid(char *buf, size_t len);
void settings_set_wifi_ssid(const char *ssid);

void settings_get_wifi_pass(char *buf, size_t len);
void settings_set_wifi_pass(const char *pass);
