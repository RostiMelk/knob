#include "app_config.h"
#include "settings.h"
#include "voice_tools.h"

#include "esp_event.h"
#include "esp_log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr const char *TAG = "tools_radio";

// ─── Case-Insensitive Substring Match ───────────────────────────────────────

static bool icontains(const char *haystack, const char *needle) {
  if (!haystack || !needle || !*needle)
    return false;
  for (const char *h = haystack; *h; h++) {
    const char *a = h;
    const char *b = needle;
    while (*a && *b) {
      char ca = (*a >= 'A' && *a <= 'Z') ? (*a + 32) : *a;
      char cb = (*b >= 'A' && *b <= 'Z') ? (*b + 32) : *b;
      if (ca != cb)
        break;
      a++;
      b++;
    }
    if (!*b)
      return true;
  }
  return false;
}

// ─── play_station ───────────────────────────────────────────────────────────

static bool handle_play_station(const char *args, ToolResult *r) {
  char name[64] = {};
  if (!tool_json_get_string(args, "station_name", name, sizeof(name))) {
    r->success = false;
    snprintf(r->output, sizeof(r->output), "Missing station_name parameter.");
    return true;
  }

  int best = -1;
  for (int i = 0; i < STATION_COUNT; i++) {
    if (icontains(STATIONS[i].name, name)) {
      best = i;
      break;
    }
  }

  if (best < 0) {
    r->success = false;
    size_t pos = snprintf(r->output, sizeof(r->output),
                          "No station matching '%s'. Available: ", name);
    for (int i = 0; i < STATION_COUNT && pos < sizeof(r->output) - 20; i++) {
      int w = snprintf(r->output + pos, sizeof(r->output) - pos, "%s%s",
                       i > 0 ? ", " : "", STATIONS[i].name);
      if (w > 0)
        pos += w;
    }
    return true;
  }

  int32_t idx = best;
  esp_event_post(APP_EVENT, APP_EVENT_STATION_CHANGED, &idx, sizeof(idx), 0);

  r->success = true;
  snprintf(r->output, sizeof(r->output), "Now playing %s.",
           STATIONS[best].name);
  ESP_LOGI(TAG, "Switching to station: %s", STATIONS[best].name);
  return true;
}

REGISTER_TOOL(
    play_station, "play_station",
    "Switch to a radio station by name. Available stations: NRK P1 Oslo, "
    "NRK P2, NRK P3, NRK MP3, NRK Jazz, P4 Norge, P5 Hits, P9 Retro, "
    "Radio Rock, Radio Norge, NRJ Norge.",
    R"J({"type":"object","properties":{"station_name":{"type":"string","description":"Name of the station to play (case-insensitive partial match)"}},"required":["station_name"]})J",
    handle_play_station);

// ─── set_volume ─────────────────────────────────────────────────────────────

static bool handle_set_volume(const char *args, ToolResult *r) {
  int level = -1;
  if (!tool_json_get_int(args, "level", &level)) {
    r->success = false;
    snprintf(r->output, sizeof(r->output), "Missing level parameter (0-100).");
    return true;
  }

  level = std::clamp(level, VOLUME_MIN, VOLUME_MAX);
  int32_t vol = level;
  esp_event_post(APP_EVENT, APP_EVENT_VOLUME_CHANGED, &vol, sizeof(vol), 0);

  r->success = true;
  snprintf(r->output, sizeof(r->output), "Volume set to %d%%.", level);
  ESP_LOGI(TAG, "Volume set to %d", level);
  return true;
}

REGISTER_TOOL(
    set_volume, "set_volume", "Set the Sonos speaker volume.",
    R"J({"type":"object","properties":{"level":{"type":"integer","description":"Volume level 0-100"}},"required":["level"]})J",
    handle_set_volume);

// ─── get_now_playing ────────────────────────────────────────────────────────

static bool handle_get_now_playing(const char *, ToolResult *r) {
  int idx = settings_get_station_index();
  const char *station =
      (idx >= 0 && idx < STATION_COUNT) ? STATIONS[idx].name : "Unknown";

  r->success = true;
  snprintf(r->output, sizeof(r->output), "Station: %s (index %d of %d).",
           station, idx + 1, STATION_COUNT);
  return true;
}

REGISTER_TOOL(get_now_playing, "get_now_playing",
              "Get the currently playing station name, play state, and volume.",
              R"J({"type":"object","properties":{}})J", handle_get_now_playing);
