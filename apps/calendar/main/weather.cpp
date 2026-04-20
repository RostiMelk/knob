#include "weather.h"
#include "app_config.h"
#include "fonts.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstring>
#include <initializer_list>

static constexpr const char *TAG = "weather";
static constexpr int FETCH_INTERVAL_MS = 30 * 60 * 1000;
static constexpr int RESP_BUF_SIZE = 48 * 1024;
static constexpr int TASK_STACK = 8192;
static constexpr const char *USER_AGENT =
    "knob-radio/1.0 github.com/rostimelk/knob";

static WeatherData s_data = {};
static TaskHandle_t s_task = nullptr;
static bool s_started = false;

// ─── Symbol Code Mapping ────────────────────────────────────────────────────

enum class TimeVariant { Day, Night, None };

struct SymbolEntry {
  const char *code;
  const char *text;
  const char *icon_day;
  const char *icon_night;
  uint32_t color_day;
  uint32_t color_night;
};

// Colors
static constexpr uint32_t COL_SUN = 0xFFD60A;
static constexpr uint32_t COL_MOON = 0xB0B0C0;
static constexpr uint32_t COL_CLOUD = 0x8E8E93;
static constexpr uint32_t COL_RAIN = 0x64D2FF;
static constexpr uint32_t COL_SNOW = 0xE0E0E8;
static constexpr uint32_t COL_THUNDER = 0xFFD60A;
static constexpr uint32_t COL_FOG = 0x8E8E93;
static constexpr uint32_t COL_SLEET = 0x86C8E8;

static constexpr SymbolEntry SYMBOLS[] = {
    {"clearsky", "Clear", ICON_SUN, ICON_MOON, COL_SUN, COL_MOON},
    {"fair", "Fair", ICON_CLOUD_SUN, ICON_CLOUD_MOON, COL_SUN, COL_MOON},
    {"partlycloudy", "Partly cloudy", ICON_CLOUD_SUN, ICON_CLOUD_MOON, COL_SUN,
     COL_MOON},
    {"cloudy", "Cloudy", ICON_CLOUD, ICON_CLOUD, COL_CLOUD, COL_CLOUD},
    {"fog", "Fog", ICON_CLOUD_FOG, ICON_CLOUD_FOG, COL_FOG, COL_FOG},
    {"lightrain", "Light rain", ICON_CLOUD_DRIZZLE, ICON_CLOUD_DRIZZLE,
     COL_RAIN, COL_RAIN},
    {"rain", "Rain", ICON_CLOUD_RAIN, ICON_CLOUD_RAIN, COL_RAIN, COL_RAIN},
    {"heavyrain", "Heavy rain", ICON_CLOUD_RAIN_WIND, ICON_CLOUD_RAIN_WIND,
     COL_RAIN, COL_RAIN},
    {"lightrainshowers", "Showers", ICON_CLOUD_SUN_RAIN, ICON_CLOUD_MOON_RAIN,
     COL_RAIN, COL_RAIN},
    {"rainshowers", "Showers", ICON_CLOUD_SUN_RAIN, ICON_CLOUD_MOON_RAIN,
     COL_RAIN, COL_RAIN},
    {"heavyrainshowers", "Heavy showers", ICON_CLOUD_RAIN_WIND,
     ICON_CLOUD_RAIN_WIND, COL_RAIN, COL_RAIN},
    {"lightrainandthunder", "Thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"rainandthunder", "Thunder", ICON_CLOUD_LIGHTNING, ICON_CLOUD_LIGHTNING,
     COL_THUNDER, COL_THUNDER},
    {"heavyrainandthunder", "Thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"lightsnow", "Light snow", ICON_SNOWFLAKE, ICON_SNOWFLAKE, COL_SNOW,
     COL_SNOW},
    {"snow", "Snow", ICON_CLOUD_SNOW, ICON_CLOUD_SNOW, COL_SNOW, COL_SNOW},
    {"heavysnow", "Heavy snow", ICON_CLOUD_SNOW, ICON_CLOUD_SNOW, COL_SNOW,
     COL_SNOW},
    {"lightsnowshowers", "Snow showers", ICON_CLOUD_SNOW, ICON_CLOUD_SNOW,
     COL_SNOW, COL_SNOW},
    {"snowshowers", "Snow showers", ICON_CLOUD_SNOW, ICON_CLOUD_SNOW, COL_SNOW,
     COL_SNOW},
    {"heavysnowshowers", "Heavy snow", ICON_CLOUD_SNOW, ICON_CLOUD_SNOW,
     COL_SNOW, COL_SNOW},
    {"snowandthunder", "Snow & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"lightsleet", "Sleet", ICON_CLOUD_HAIL, ICON_CLOUD_HAIL, COL_SLEET,
     COL_SLEET},
    {"sleet", "Sleet", ICON_CLOUD_HAIL, ICON_CLOUD_HAIL, COL_SLEET, COL_SLEET},
    {"heavysleet", "Heavy sleet", ICON_CLOUD_HAIL, ICON_CLOUD_HAIL, COL_SLEET,
     COL_SLEET},
    {"lightsleetshowers", "Sleet", ICON_CLOUD_HAIL, ICON_CLOUD_HAIL, COL_SLEET,
     COL_SLEET},
    {"sleetshowers", "Sleet", ICON_CLOUD_HAIL, ICON_CLOUD_HAIL, COL_SLEET,
     COL_SLEET},
    {"heavysleetshowers", "Heavy sleet", ICON_CLOUD_HAIL, ICON_CLOUD_HAIL,
     COL_SLEET, COL_SLEET},
    {"sleetandthunder", "Sleet & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"lightssnowshowersandthunder", "Snow & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"snowshowersandthunder", "Snow & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"heavysnowshowersandthunder", "Snow & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"lightrainshowersandthunder", "Thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"rainshowersandthunder", "Thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"heavyrainshowersandthunder", "Thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"lightssleetshowersandthunder", "Sleet & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"sleetshowersandthunder", "Sleet & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"heavysleetshowersandthunder", "Sleet & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"lightsleetandthunder", "Sleet & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"heavysleetandthunder", "Sleet & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"lightsnowandthunder", "Snow & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
    {"heavysnowandthunder", "Snow & thunder", ICON_CLOUD_LIGHTNING,
     ICON_CLOUD_LIGHTNING, COL_THUNDER, COL_THUNDER},
};

static TimeVariant parse_symbol(const char *symbol, char *base,
                                size_t base_len) {
  strncpy(base, symbol, base_len - 1);
  base[base_len - 1] = '\0';

  TimeVariant variant = TimeVariant::None;
  char *pos = strstr(base, "_polartwilight");
  if (pos) {
    *pos = '\0';
    return TimeVariant::Day;
  }
  pos = strstr(base, "_night");
  if (pos) {
    *pos = '\0';
    return TimeVariant::Night;
  }
  pos = strstr(base, "_day");
  if (pos) {
    *pos = '\0';
    return TimeVariant::Day;
  }
  return variant;
}

static const SymbolEntry *find_symbol(const char *base) {
  for (const auto &entry : SYMBOLS) {
    if (strcmp(base, entry.code) == 0)
      return &entry;
  }
  return nullptr;
}

// ─── HTTP ───────────────────────────────────────────────────────────────────

struct HttpBuf {
  uint8_t *data;
  int len;
  int capacity;
};

static esp_err_t on_http_event(esp_http_client_event_t *evt) {
  auto *buf = static_cast<HttpBuf *>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && buf && evt->data_len > 0) {
    int needed = buf->len + evt->data_len;
    if (needed < buf->capacity) {
      memcpy(buf->data + buf->len, evt->data, evt->data_len);
      buf->len += evt->data_len;
      buf->data[buf->len] = '\0';
    }
  }
  return ESP_OK;
}

// ─── Fetch & Parse ──────────────────────────────────────────────────────────

static bool fetch_and_parse() {
  char url[128];
  snprintf(url, sizeof(url),
           "https://api.met.no/weatherapi/locationforecast/2.0/"
           "compact?lat=%s&lon=%s",
           CONFIG_CALENDAR_WEATHER_LAT, CONFIG_CALENDAR_WEATHER_LON);

  auto *raw = static_cast<uint8_t *>(
      heap_caps_malloc(RESP_BUF_SIZE, MALLOC_CAP_SPIRAM));
  if (!raw) {
    ESP_LOGE(TAG, "Failed to allocate response buffer");
    return false;
  }

  HttpBuf resp = {raw, 0, RESP_BUF_SIZE};

  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = 10000;
  cfg.event_handler = on_http_event;
  cfg.user_data = &resp;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.buffer_size = 2048;
  cfg.buffer_size_tx = 512;

  auto *client = esp_http_client_init(&cfg);
  if (!client) {
    heap_caps_free(raw);
    return false;
  }

  esp_http_client_set_header(client, "User-Agent", USER_AGENT);

  esp_err_t err = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || status != 200) {
    ESP_LOGW(TAG, "Fetch failed: err=%s status=%d", esp_err_to_name(err),
             status);
    heap_caps_free(raw);
    return false;
  }

  ESP_LOGI(TAG, "Fetched %d bytes", resp.len);

  cJSON *root = cJSON_Parse(reinterpret_cast<char *>(raw));
  heap_caps_free(raw);

  if (!root) {
    ESP_LOGE(TAG, "JSON parse failed");
    return false;
  }

  bool ok = false;

  cJSON *props = cJSON_GetObjectItem(root, "properties");
  cJSON *series = props ? cJSON_GetObjectItem(props, "timeseries") : nullptr;
  cJSON *first = series ? cJSON_GetArrayItem(series, 0) : nullptr;
  cJSON *data = first ? cJSON_GetObjectItem(first, "data") : nullptr;

  if (data) {
    cJSON *instant = cJSON_GetObjectItem(data, "instant");
    cJSON *details =
        instant ? cJSON_GetObjectItem(instant, "details") : nullptr;

    if (details) {
      cJSON *temp = cJSON_GetObjectItem(details, "air_temperature");
      cJSON *wind = cJSON_GetObjectItem(details, "wind_speed");

      if (temp && cJSON_IsNumber(temp)) {
        s_data.temperature = static_cast<float>(temp->valuedouble);
        ok = true;
      }
      if (wind && cJSON_IsNumber(wind))
        s_data.wind_speed = static_cast<float>(wind->valuedouble);
    }

    // Symbol code from the shortest available forecast period
    const char *symbol = nullptr;
    for (const char *period :
         {"next_1_hours", "next_6_hours", "next_12_hours"}) {
      cJSON *next = cJSON_GetObjectItem(data, period);
      cJSON *summary = next ? cJSON_GetObjectItem(next, "summary") : nullptr;
      cJSON *sym =
          summary ? cJSON_GetObjectItem(summary, "symbol_code") : nullptr;
      if (sym && cJSON_IsString(sym)) {
        symbol = sym->valuestring;
        break;
      }
    }

    if (symbol) {
      strncpy(s_data.symbol, symbol, sizeof(s_data.symbol) - 1);

      char base[64];
      TimeVariant variant = parse_symbol(symbol, base, sizeof(base));
      bool is_night = (variant == TimeVariant::Night);

      const SymbolEntry *entry = find_symbol(base);
      if (entry) {
        strncpy(s_data.condition, entry->text, sizeof(s_data.condition) - 1);
        s_data.icon = is_night ? entry->icon_night : entry->icon_day;
        s_data.color = is_night ? entry->color_night : entry->color_day;
      } else {
        strncpy(s_data.condition, base, sizeof(s_data.condition) - 1);
        s_data.icon = ICON_CLOUD;
        s_data.color = COL_CLOUD;
      }
    }

    // Parse forecast entries at ~3-hour intervals
    int series_count = cJSON_GetArraySize(series);
    static constexpr int FORECAST_INDICES[] = {3, 7, 11, 15};
    s_data.forecast_count = 0;

    for (int fi = 0; fi < FORECAST_MAX; fi++) {
      int idx = FORECAST_INDICES[fi];
      if (idx >= series_count)
        break;

      cJSON *entry = cJSON_GetArrayItem(series, idx);
      cJSON *etime = entry ? cJSON_GetObjectItem(entry, "time") : nullptr;
      cJSON *edata = entry ? cJSON_GetObjectItem(entry, "data") : nullptr;
      if (!edata || !etime || !cJSON_IsString(etime))
        continue;

      // Extract hour from "YYYY-MM-DDThh:mm:ssZ"
      const char *tstr = etime->valuestring;
      int hour = (tstr[11] - '0') * 10 + (tstr[12] - '0');

      // Temperature
      cJSON *einst = cJSON_GetObjectItem(edata, "instant");
      cJSON *edet = einst ? cJSON_GetObjectItem(einst, "details") : nullptr;
      cJSON *etemp =
          edet ? cJSON_GetObjectItem(edet, "air_temperature") : nullptr;
      if (!etemp || !cJSON_IsNumber(etemp))
        continue;

      // Symbol
      const char *esym = nullptr;
      for (const char *period :
           {"next_1_hours", "next_6_hours", "next_12_hours"}) {
        cJSON *next = cJSON_GetObjectItem(edata, period);
        cJSON *sum = next ? cJSON_GetObjectItem(next, "summary") : nullptr;
        cJSON *sc = sum ? cJSON_GetObjectItem(sum, "symbol_code") : nullptr;
        if (sc && cJSON_IsString(sc)) {
          esym = sc->valuestring;
          break;
        }
      }

      auto &fc = s_data.forecast[s_data.forecast_count];
      fc.hour = hour;
      fc.temperature = static_cast<float>(etemp->valuedouble);

      if (esym) {
        char base[64];
        TimeVariant variant = parse_symbol(esym, base, sizeof(base));
        bool is_night = (variant == TimeVariant::Night);
        const SymbolEntry *fentry = find_symbol(base);
        if (fentry) {
          fc.icon = is_night ? fentry->icon_night : fentry->icon_day;
          fc.color = is_night ? fentry->color_night : fentry->color_day;
        } else {
          fc.icon = ICON_CLOUD;
          fc.color = COL_CLOUD;
        }
      } else {
        fc.icon = ICON_CLOUD;
        fc.color = COL_CLOUD;
      }

      s_data.forecast_count++;
    }
  }

  cJSON_Delete(root);

  if (ok) {
    s_data.valid = true;
    ESP_LOGI(TAG, "Weather: %.1f\xC2\xB0 C, %s (%s), %d forecast entries",
             s_data.temperature, s_data.condition, s_data.symbol,
             s_data.forecast_count);
    esp_event_post(APP_EVENT, APP_EVENT_WEATHER_UPDATE, &s_data, sizeof(s_data),
                   pdMS_TO_TICKS(100));
  }

  return ok;
}

// ─── Task ───────────────────────────────────────────────────────────────────

static bool time_synced() { return time(nullptr) > 1'700'000'000; }

static void weather_task(void *) {
  // Wait for NTP — TLS cert validation requires a valid system clock
  for (int i = 0; i < 300 && !time_synced(); i++)
    vTaskDelay(pdMS_TO_TICKS(100));

  while (true) {
    if (fetch_and_parse()) {
      vTaskDelay(pdMS_TO_TICKS(FETCH_INTERVAL_MS));
    } else {
      vTaskDelay(pdMS_TO_TICKS(10000)); // retry sooner on failure
    }
  }
}

void weather_init() {}

void weather_start() {
  if (s_started)
    return;
  s_started = true;

  xTaskCreatePinnedToCore(weather_task, "weather", TASK_STACK, nullptr,
                          NET_TASK_PRIO - 1, &s_task, NET_TASK_CORE);
  ESP_LOGI(TAG, "Started (lat=%s, lon=%s)", CONFIG_CALENDAR_WEATHER_LAT,
           CONFIG_CALENDAR_WEATHER_LON);
}
