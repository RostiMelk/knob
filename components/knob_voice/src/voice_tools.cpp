#include "voice_tools.h"
#include "esp_log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

static constexpr const char *TAG = "voice_tools";

// ─── Global Registry ────────────────────────────────────────────────────────

ToolDef g_tool_registry[VOICE_TOOLS_MAX] = {};
int g_tool_count = 0;

// ─── JSON Argument Helpers ──────────────────────────────────────────────────

bool tool_json_get_int(const char *json, const char *key, int *out) {
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *pos = strstr(json, pattern);
  if (!pos)
    return false;
  pos += strlen(pattern);
  while (*pos == ' ' || *pos == ':' || *pos == '\t' || *pos == '\n')
    pos++;
  if (*pos == '\0')
    return false;
  *out = atoi(pos);
  return true;
}

bool tool_json_get_string(const char *json, const char *key, char *out,
                          size_t out_len) {
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *pos = strstr(json, pattern);
  if (!pos)
    return false;
  pos += strlen(pattern);
  while (*pos == ' ' || *pos == ':' || *pos == '\t' || *pos == '\n')
    pos++;
  if (*pos != '"')
    return false;
  pos++;
  size_t i = 0;
  while (*pos && *pos != '"' && i < out_len - 1) {
    if (*pos == '\\' && *(pos + 1))
      pos++;
    out[i++] = *pos++;
  }
  out[i] = '\0';
  return i > 0;
}

bool tool_json_get_bool(const char *json, const char *key, bool *out) {
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *pos = strstr(json, pattern);
  if (!pos)
    return false;
  pos += strlen(pattern);
  while (*pos == ' ' || *pos == ':' || *pos == '\t' || *pos == '\n')
    pos++;
  if (strncmp(pos, "true", 4) == 0) {
    *out = true;
    return true;
  }
  if (strncmp(pos, "false", 5) == 0) {
    *out = false;
    return true;
  }
  return false;
}

// ─── Build JSON ─────────────────────────────────────────────────────────────

int voice_tools_build_json(char *buf, size_t buf_len) {
  if (!buf || buf_len < 3)
    return -1;

  size_t pos = 0;
  buf[pos++] = '[';

  for (int i = 0; i < g_tool_count; i++) {
    const auto &t = g_tool_registry[i];
    int written =
        snprintf(buf + pos, buf_len - pos,
                 "%s{\"type\":\"function\",\"name\":\"%s\","
                 "\"description\":\"%s\",\"parameters\":%s}",
                 i > 0 ? "," : "", t.name, t.description, t.parameters_json);
    if (written < 0 || pos + written >= buf_len)
      return -1;
    pos += written;
  }

  if (pos + 2 > buf_len)
    return -1;
  buf[pos++] = ']';
  buf[pos] = '\0';
  return static_cast<int>(pos);
}

// ─── Execute ────────────────────────────────────────────────────────────────

bool voice_tools_execute(const char *name, const char *arguments,
                         ToolResult *result) {
  if (!name || !result)
    return false;

  const ToolDef *tool = voice_tools_find(name);
  if (!tool) {
    ESP_LOGW(TAG, "Unknown tool: %s", name);
    result->success = false;
    snprintf(result->output, sizeof(result->output), "Unknown tool: %s", name);
    return false;
  }

  ESP_LOGI(TAG, "Executing tool: %s", name);
  return tool->handler(arguments ? arguments : "{}", result);
}

// ─── Registry Queries ───────────────────────────────────────────────────────

int voice_tools_count() { return g_tool_count; }

const ToolDef *voice_tools_get(int index) {
  if (index < 0 || index >= g_tool_count)
    return nullptr;
  return &g_tool_registry[index];
}

const ToolDef *voice_tools_find(const char *name) {
  if (!name)
    return nullptr;
  for (int i = 0; i < g_tool_count; i++) {
    if (strcmp(g_tool_registry[i].name, name) == 0)
      return &g_tool_registry[i];
  }
  return nullptr;
}

// ─── Init ───────────────────────────────────────────────────────────────────

void voice_tools_init() {
  ESP_LOGI(TAG, "Voice tools initialized (%d tools registered)", g_tool_count);
  for (int i = 0; i < g_tool_count; i++) {
    ESP_LOGI(TAG, "  [%d] %s", i, g_tool_registry[i].name);
  }
}
