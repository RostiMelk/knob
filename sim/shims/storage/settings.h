#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

static char _wifi_ssid[33] = "SimNetwork";
static char _wifi_pass[65] = "SimPass";
static char _speaker_ip[40] = "";
static char _speaker_name[64] = "";
static int _volume = 20;
static int _station = 0;

inline void settings_init() {}

inline bool settings_load_config_from_sd() { return false; }

inline int settings_get_volume() { return _volume; }
inline void settings_set_volume(int v) { _volume = v; }

inline int settings_get_station_index() { return _station; }
inline void settings_set_station_index(int i) { _station = i; }

inline void settings_get_speaker_ip(char *buf, size_t len) {
  strncpy(buf, _speaker_ip, len - 1);
  buf[len - 1] = '\0';
}
inline void settings_set_speaker_ip(const char *ip) {
  strncpy(_speaker_ip, ip, sizeof(_speaker_ip) - 1);
}

inline void settings_get_speaker_name(char *buf, size_t len) {
  strncpy(buf, _speaker_name, len - 1);
  buf[len - 1] = '\0';
}
inline void settings_set_speaker_name(const char *name) {
  strncpy(_speaker_name, name, sizeof(_speaker_name) - 1);
}

inline bool settings_has_speaker() { return _speaker_ip[0] != '\0'; }

inline void settings_get_wifi_ssid(char *buf, size_t len) {
  strncpy(buf, _wifi_ssid, len - 1);
  buf[len - 1] = '\0';
}
inline void settings_set_wifi_ssid(const char *s) {
  strncpy(_wifi_ssid, s, sizeof(_wifi_ssid) - 1);
}

inline void settings_get_wifi_pass(char *buf, size_t len) {
  strncpy(buf, _wifi_pass, len - 1);
  buf[len - 1] = '\0';
}
inline void settings_set_wifi_pass(const char *p) {
  strncpy(_wifi_pass, p, sizeof(_wifi_pass) - 1);
}
