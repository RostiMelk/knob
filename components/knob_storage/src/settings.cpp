#include "settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <algorithm>
#include <cstring>

static constexpr const char *TAG = "settings";
static constexpr const char *NVS_NAMESPACE = "radio";

static constexpr int VOLUME_MIN = 0;
static constexpr int VOLUME_MAX = 100;

static constexpr const char *KEY_VOLUME = "volume";
static constexpr const char *KEY_STATION = "station";
static constexpr const char *KEY_SPEAKER_IP = "speaker_ip";
static constexpr const char *KEY_SPEAKER_NAME = "speaker_nm";
static constexpr const char *KEY_WIFI_SSID = "wifi_ssid";
static constexpr const char *KEY_WIFI_PASS = "wifi_pass";
static constexpr const char *KEY_OPENAI_KEY = "openai_key";

static nvs_handle_t s_nvs;

static int32_t read_i32(const char *key, int32_t fallback) {
  int32_t val = fallback;
  if (nvs_get_i32(s_nvs, key, &val) != ESP_OK)
    return fallback;
  return val;
}

static void write_i32(const char *key, int32_t val) {
  nvs_set_i32(s_nvs, key, val);
  nvs_commit(s_nvs);
}

static void read_str(const char *key, char *buf, size_t len,
                     const char *fallback) {
  size_t required = len;
  if (nvs_get_str(s_nvs, key, buf, &required) != ESP_OK) {
    strncpy(buf, fallback, len);
    buf[len - 1] = '\0';
  }
}

static void write_str(const char *key, const char *val) {
  nvs_set_str(s_nvs, key, val);
  nvs_commit(s_nvs);
}

void settings_init() {
  esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
    return;
  }
  ESP_LOGI(TAG, "NVS initialized");
}

int settings_get_volume() {
  return std::clamp(
      static_cast<int>(read_i32(KEY_VOLUME, CONFIG_RADIO_VOLUME_DEFAULT)),
      VOLUME_MIN, VOLUME_MAX);
}

void settings_set_volume(int volume) {
  write_i32(KEY_VOLUME, std::clamp(volume, VOLUME_MIN, VOLUME_MAX));
}

int settings_get_station_index() {
  return std::clamp(static_cast<int>(read_i32(KEY_STATION, 0)), 0, 99);
}

void settings_set_station_index(int index) {
  write_i32(KEY_STATION, std::clamp(index, 0, 99));
}

void settings_get_speaker_ip(char *buf, size_t len) {
  read_str(KEY_SPEAKER_IP, buf, len, CONFIG_RADIO_SONOS_SPEAKER_IP);
}

void settings_set_speaker_ip(const char *ip) { write_str(KEY_SPEAKER_IP, ip); }

void settings_get_speaker_name(char *buf, size_t len) {
  read_str(KEY_SPEAKER_NAME, buf, len, "");
}

void settings_set_speaker_name(const char *name) {
  write_str(KEY_SPEAKER_NAME, name);
}

bool settings_has_speaker() {
  char ip[40] = {};
  settings_get_speaker_ip(ip, sizeof(ip));
  return ip[0] != '\0' && strcmp(ip, CONFIG_RADIO_SONOS_SPEAKER_IP) != 0;
}

void settings_get_wifi_ssid(char *buf, size_t len) {
  read_str(KEY_WIFI_SSID, buf, len, CONFIG_RADIO_WIFI_SSID);
}

void settings_set_wifi_ssid(const char *ssid) {
  write_str(KEY_WIFI_SSID, ssid);
}

void settings_get_wifi_pass(char *buf, size_t len) {
  read_str(KEY_WIFI_PASS, buf, len, CONFIG_RADIO_WIFI_PASSWORD);
}

void settings_set_wifi_pass(const char *pass) {
  write_str(KEY_WIFI_PASS, pass);
}

void settings_get_openai_api_key(char *buf, size_t len) {
  const char *fallback = "";
#ifdef CONFIG_RADIO_OPENAI_API_KEY
  fallback = CONFIG_RADIO_OPENAI_API_KEY;
#endif
  read_str(KEY_OPENAI_KEY, buf, len, fallback);
}

void settings_set_openai_api_key(const char *key) {
  write_str(KEY_OPENAI_KEY, key);
}
