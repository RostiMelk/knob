#pragma once

#include <cstdint>

static constexpr int FORECAST_MAX = 4;

struct ForecastEntry {
  int hour; // 0-23
  float temperature;
  const char *icon; // Lucide icon UTF-8 string
  uint32_t color;   // Icon color
};

struct WeatherData {
  float temperature;
  float wind_speed;
  char condition[32];
  char symbol[48];
  const char *icon; // Lucide icon UTF-8 string (e.g. ICON_SUN)
  uint32_t color;   // Icon color as 0xRRGGBB
  bool valid;
  ForecastEntry forecast[FORECAST_MAX];
  int forecast_count;
};

// Weather event ID (post with WeatherData payload)
enum : int32_t {
  APP_EVENT_WEATHER_UPDATE = 400,
};

// Initialize weather module (call once at startup)
void weather_init();

// Start periodic weather fetching (call after WiFi connects)
void weather_start();
