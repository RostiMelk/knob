#include "settings.h"
#include "app_config.h"

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdmmc_cmd.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr const char *TAG = "settings";
static constexpr const char *NVS_NAMESPACE = "radio";

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
  return std::clamp(static_cast<int>(read_i32(KEY_STATION, 0)), 0,
                    STATION_COUNT - 1);
}

void settings_set_station_index(int index) {
  write_i32(KEY_STATION, std::clamp(index, 0, STATION_COUNT - 1));
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

// ─── SD Card Config File ────────────────────────────────────────────────────
// Standard .env format: KEY=value, one per line. # comments.
// See .env.template in project root.

static bool mount_sd() {
  esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
  mount_cfg.format_if_mount_failed = false;
  mount_cfg.max_files = 2;
  mount_cfg.allocation_unit_size = 16 * 1024;

  sdmmc_host_t host = SDMMC_HOST_DEFAULT();
  host.max_freq_khz = SDMMC_FREQ_DEFAULT;

  sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
  slot.width = 4;
  slot.clk = static_cast<gpio_num_t>(PIN_SD_CLK);
  slot.cmd = static_cast<gpio_num_t>(PIN_SD_CMD);
  slot.d0 = static_cast<gpio_num_t>(PIN_SD_D0);
  slot.d1 = static_cast<gpio_num_t>(PIN_SD_D1);
  slot.d2 = static_cast<gpio_num_t>(PIN_SD_D2);
  slot.d3 = static_cast<gpio_num_t>(PIN_SD_D3);

  sdmmc_card_t *card = nullptr;
  esp_err_t err =
      esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mount_cfg, &card);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "SD card not mounted: %s", esp_err_to_name(err));
    return false;
  }
  ESP_LOGI(TAG, "SD card mounted");
  return true;
}

static void unmount_sd() {
  esp_vfs_fat_sdcard_unmount(SD_MOUNT_POINT, nullptr);
}

static void apply_config_line(char *line) {
  while (*line == ' ' || *line == '\t')
    line++;
  if (*line == '#' || *line == '\0' || *line == '\n')
    return;

  char *newline = strchr(line, '\n');
  if (newline)
    *newline = '\0';
  char *cr = strchr(line, '\r');
  if (cr)
    *cr = '\0';

  char *eq = strchr(line, '=');
  if (!eq)
    return;

  *eq = '\0';
  const char *key = line;
  const char *val = eq + 1;

  if (strcmp(key, "WIFI_SSID") == 0) {
    settings_set_wifi_ssid(val);
    ESP_LOGI(TAG, ".env: WIFI_SSID=%s", val);
  } else if (strcmp(key, "WIFI_PASS") == 0) {
    settings_set_wifi_pass(val);
    ESP_LOGI(TAG, ".env: WIFI_PASS=***");
  } else if (strcmp(key, "SPEAKER_IP") == 0) {
    settings_set_speaker_ip(val);
    ESP_LOGI(TAG, ".env: SPEAKER_IP=%s", val);
  } else if (strcmp(key, "VOLUME") == 0) {
    settings_set_volume(atoi(val));
    ESP_LOGI(TAG, ".env: VOLUME=%s", val);
  } else if (strcmp(key, "STATION") == 0) {
    settings_set_station_index(atoi(val));
    ESP_LOGI(TAG, ".env: STATION=%s", val);
  } else if (strcmp(key, "OPENAI_API_KEY") == 0) {
    settings_set_openai_api_key(val);
    ESP_LOGI(TAG, ".env: OPENAI_API_KEY=sk-***");
  } else {
    ESP_LOGW(TAG, "config: unknown key '%s'", key);
  }
}

bool settings_load_config_from_sd() {
  if (!mount_sd()) {
    ESP_LOGI(TAG, "No SD card");
    return false;
  }

  FILE *f = fopen(SD_CONFIG_PATH, "r");
  if (!f) {
    ESP_LOGI(TAG, "No .env on SD card");
    unmount_sd();
    return true;
  }

  ESP_LOGI(TAG, "Reading .env from SD card");
  char line[256];
  int count = 0;
  while (fgets(line, sizeof(line), f)) {
    apply_config_line(line);
    count++;
  }
  fclose(f);
  unmount_sd();

  ESP_LOGI(TAG, "Applied %d lines from .env", count);
  return true;
}
