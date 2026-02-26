#include "sonos.h"
#include "app_config.h"
#include "storage/settings.h"

#include "esp_http_client.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr const char *TAG = "sonos";

static constexpr const char *AV_TRANSPORT_PATH =
    "/MediaRenderer/AVTransport/Control";
static constexpr const char *RENDERING_CONTROL_PATH =
    "/MediaRenderer/RenderingControl/Control";

#define AV_TRANSPORT_NS "urn:schemas-upnp-org:service:AVTransport:1"
#define RENDERING_CONTROL_NS "urn:schemas-upnp-org:service:RenderingControl:1"

// ─── SOAP Templates ─────────────────────────────────────────────────────────

static constexpr const char *SOAP_ENVELOPE_FMT =
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
    " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
    "<s:Body>%s</s:Body>"
    "</s:Envelope>";

static constexpr const char *SET_URI_FMT =
    "<u:SetAVTransportURI xmlns:u=\"" AV_TRANSPORT_NS "\">"
    "<InstanceID>0</InstanceID>"
    "<CurrentURI>%s</CurrentURI>"
    "<CurrentURIMetaData></CurrentURIMetaData>"
    "</u:SetAVTransportURI>";

static constexpr const char *PLAY_BODY =
    "<u:Play xmlns:u=\"" AV_TRANSPORT_NS "\">"
    "<InstanceID>0</InstanceID>"
    "<Speed>1</Speed>"
    "</u:Play>";

static constexpr const char *STOP_BODY =
    "<u:Stop xmlns:u=\"" AV_TRANSPORT_NS "\">"
    "<InstanceID>0</InstanceID>"
    "</u:Stop>";

static constexpr const char *GET_TRANSPORT_INFO_BODY =
    "<u:GetTransportInfo xmlns:u=\"" AV_TRANSPORT_NS "\">"
    "<InstanceID>0</InstanceID>"
    "</u:GetTransportInfo>";

static constexpr const char *GET_VOLUME_BODY =
    "<u:GetVolume xmlns:u=\"" RENDERING_CONTROL_NS "\">"
    "<InstanceID>0</InstanceID>"
    "<Channel>Master</Channel>"
    "</u:GetVolume>";

static constexpr const char *SET_VOLUME_FMT =
    "<u:SetVolume xmlns:u=\"" RENDERING_CONTROL_NS "\">"
    "<InstanceID>0</InstanceID>"
    "<Channel>Master</Channel>"
    "<DesiredVolume>%d</DesiredVolume>"
    "</u:SetVolume>";

// ─── Command Queue ──────────────────────────────────────────────────────────

enum class CmdType : uint8_t { PlayUri, Stop, SetVolume };

struct Command {
  CmdType type;
  union {
    char uri[256];
    int volume;
  };
};

static QueueHandle_t s_cmd_queue;
static TaskHandle_t s_task;
static volatile bool s_running;
static char s_speaker_ip[40];
static int s_speaker_port = SONOS_PORT;

// ─── HTTP Helpers ───────────────────────────────────────────────────────────

struct Response {
  char *data;
  int len;
  int capacity;
};

static esp_err_t on_http_event(esp_http_client_event_t *evt) {
  auto *resp = static_cast<Response *>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && resp && evt->data_len > 0) {
    int needed = resp->len + evt->data_len;
    if (needed < resp->capacity) {
      memcpy(resp->data + resp->len, evt->data, evt->data_len);
      resp->len += evt->data_len;
      resp->data[resp->len] = '\0';
    }
  }
  return ESP_OK;
}

static bool soap_request(const char *path, const char *action_name,
                         const char *ns, const char *body, Response *resp) {
  char url[80];
  snprintf(url, sizeof(url), "http://%s:%d%s", s_speaker_ip, s_speaker_port,
           path);

  char soap_action[128];
  snprintf(soap_action, sizeof(soap_action), "\"%s#%s\"", ns, action_name);

  char envelope[1024];
  int envelope_len =
      snprintf(envelope, sizeof(envelope), SOAP_ENVELOPE_FMT, body);
  if (envelope_len < 0 || envelope_len >= static_cast<int>(sizeof(envelope))) {
    ESP_LOGE(TAG, "SOAP envelope overflow");
    return false;
  }

  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = SONOS_HTTP_TIMEOUT_MS;
  cfg.event_handler = on_http_event;
  cfg.user_data = resp;

  auto *client = esp_http_client_init(&cfg);
  if (!client)
    return false;

  esp_http_client_set_header(client, "Content-Type",
                             "text/xml; charset=\"utf-8\"");
  esp_http_client_set_header(client, "SOAPAction", soap_action);
  esp_http_client_set_post_field(client, envelope, envelope_len);

  esp_err_t err = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK) {
    ESP_LOGE(TAG, "%s failed: %s", action_name, esp_err_to_name(err));
    return false;
  }
  if (status != 200) {
    ESP_LOGW(TAG, "%s HTTP %d", action_name, status);
    return false;
  }
  return true;
}

static bool soap_fire(const char *path, const char *action_name, const char *ns,
                      const char *body) {
  return soap_request(path, action_name, ns, body, nullptr);
}

// ─── XML Parsing (minimal — no allocator, no library) ───────────────────────

static bool xml_extract(const char *xml, const char *tag, char *out,
                        size_t out_len) {
  char open[64], close[64];
  snprintf(open, sizeof(open), "<%s>", tag);
  snprintf(close, sizeof(close), "</%s>", tag);

  const char *start = strstr(xml, open);
  if (!start)
    return false;
  start += strlen(open);

  const char *end = strstr(start, close);
  if (!end)
    return false;

  size_t len = std::min(static_cast<size_t>(end - start), out_len - 1);
  memcpy(out, start, len);
  out[len] = '\0';
  return true;
}

// ─── Commands ───────────────────────────────────────────────────────────────

static void exec_play_uri(const char *uri) {
  char inner[512];
  snprintf(inner, sizeof(inner), SET_URI_FMT, uri);

  if (soap_fire(AV_TRANSPORT_PATH, "SetAVTransportURI", AV_TRANSPORT_NS,
                inner)) {
    soap_fire(AV_TRANSPORT_PATH, "Play", AV_TRANSPORT_NS, PLAY_BODY);
    ESP_LOGI(TAG, "Playing: %s", uri);
  }
}

static void exec_stop_playback() {
  soap_fire(AV_TRANSPORT_PATH, "Stop", AV_TRANSPORT_NS, STOP_BODY);
}

static void exec_set_volume(int level) {
  char inner[256];
  snprintf(inner, sizeof(inner), SET_VOLUME_FMT,
           std::clamp(level, VOLUME_MIN, VOLUME_MAX));
  soap_fire(RENDERING_CONTROL_PATH, "SetVolume", RENDERING_CONTROL_NS, inner);
}

// ─── Polling ────────────────────────────────────────────────────────────────

static void poll_state() {
  static char resp_buf[1024];
  Response resp = {resp_buf, 0, static_cast<int>(sizeof(resp_buf))};

  SonosState state = {};
  state.station_index = -1;
  state.play_state = PlayState::Unknown;

  resp.len = 0;
  if (soap_request(AV_TRANSPORT_PATH, "GetTransportInfo", AV_TRANSPORT_NS,
                   GET_TRANSPORT_INFO_BODY, &resp)) {
    char val[32];
    if (xml_extract(resp.data, "CurrentTransportState", val, sizeof(val))) {
      if (strcmp(val, "PLAYING") == 0)
        state.play_state = PlayState::Playing;
      else if (strcmp(val, "STOPPED") == 0)
        state.play_state = PlayState::Stopped;
      else if (strcmp(val, "PAUSED_PLAYBACK") == 0)
        state.play_state = PlayState::Paused;
      else if (strcmp(val, "TRANSITIONING") == 0)
        state.play_state = PlayState::Transitioning;
    }
  }

  resp.len = 0;
  if (soap_request(RENDERING_CONTROL_PATH, "GetVolume", RENDERING_CONTROL_NS,
                   GET_VOLUME_BODY, &resp)) {
    char val[8];
    if (xml_extract(resp.data, "CurrentVolume", val, sizeof(val))) {
      state.volume = std::clamp(atoi(val), VOLUME_MIN, VOLUME_MAX);
    }
  }

  esp_event_post(APP_EVENT, APP_EVENT_SONOS_STATE_UPDATE, &state, sizeof(state),
                 0);
}

// ─── Task ───────────────────────────────────────────────────────────────────

static void net_task(void *) {
  TickType_t poll_ticks = pdMS_TO_TICKS(CONFIG_RADIO_SONOS_POLL_INTERVAL_MS);
  TickType_t last_poll = 0;

  while (s_running) {
    Command cmd;
    if (xQueueReceive(s_cmd_queue, &cmd, pdMS_TO_TICKS(200)) == pdTRUE) {
      switch (cmd.type) {
      case CmdType::PlayUri:
        exec_play_uri(cmd.uri);
        break;
      case CmdType::Stop:
        exec_stop_playback();
        break;
      case CmdType::SetVolume:
        exec_set_volume(cmd.volume);
        break;
      }
    }

    TickType_t now = xTaskGetTickCount();
    if (s_running && (now - last_poll) >= poll_ticks) {
      poll_state();
      last_poll = now;
    }
  }

  vTaskDelete(nullptr);
}

// ─── Public API ─────────────────────────────────────────────────────────────

static void enqueue(const Command &cmd) {
  if (s_cmd_queue) {
    xQueueSend(s_cmd_queue, &cmd, pdMS_TO_TICKS(50));
  }
}

void sonos_init() {
  settings_get_speaker_ip(s_speaker_ip, sizeof(s_speaker_ip));
  s_cmd_queue = xQueueCreate(8, sizeof(Command));
  ESP_LOGI(TAG, "Initialized — speaker: %s:%d", s_speaker_ip, s_speaker_port);
}

void sonos_set_speaker(const char *ip, int port) {
  strncpy(s_speaker_ip, ip, sizeof(s_speaker_ip) - 1);
  s_speaker_ip[sizeof(s_speaker_ip) - 1] = '\0';
  s_speaker_port = port;
  settings_set_speaker_ip(ip);
  ESP_LOGI(TAG, "Speaker changed: %s:%d", s_speaker_ip, s_speaker_port);
}

void sonos_start() {
  if (s_task)
    return;
  s_running = true;
  xTaskCreatePinnedToCore(net_task, "net", NET_TASK_STACK, nullptr,
                          NET_TASK_PRIO, &s_task, NET_TASK_CORE);
}

void sonos_stop() {
  s_running = false;
  if (s_task) {
    xQueueReset(s_cmd_queue);
    s_task = nullptr;
  }
}

void sonos_play_uri(const char *uri) {
  Command cmd = {.type = CmdType::PlayUri, .uri = {}};
  strncpy(cmd.uri, uri, sizeof(cmd.uri) - 1);
  enqueue(cmd);
}

void sonos_stop_playback() {
  Command cmd = {.type = CmdType::Stop, .uri = {}};
  enqueue(cmd);
}

void sonos_set_volume(int level) {
  Command cmd = {.type = CmdType::SetVolume, .volume = {}};
  cmd.volume = std::clamp(level, VOLUME_MIN, VOLUME_MAX);
  enqueue(cmd);
}
