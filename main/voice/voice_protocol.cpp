#include "voice_protocol.h"
#include "voice_tools.h"

#include "esp_log.h"

#include <cstdio>
#include <cstring>

static constexpr const char *TAG = "voice_proto";

// ─── Minimal JSON Helpers ───────────────────────────────────────────────────
//
// The Realtime API sends well-formed JSON. We avoid pulling in cJSON to keep
// the binary small. These helpers are intentionally simple — they work for
// the flat / shallow-nested structures the API produces.

static const char *json_find_key(const char *json, const char *key) {
  char pattern[128];
  int n = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  if (n < 0 || n >= static_cast<int>(sizeof(pattern)))
    return nullptr;

  const char *pos = json;
  while ((pos = strstr(pos, pattern)) != nullptr) {
    pos += strlen(pattern);
    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')
      pos++;
    if (*pos == ':') {
      pos++;
      while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')
        pos++;
      return pos;
    }
  }
  return nullptr;
}

static bool json_get_string(const char *json, const char *key, const char **out,
                            size_t *out_len) {
  const char *val = json_find_key(json, key);
  if (!val || *val != '"')
    return false;
  val++;
  const char *end = val;
  while (*end && *end != '"') {
    if (*end == '\\' && *(end + 1))
      end++;
    end++;
  }
  *out = val;
  *out_len = end - val;
  return true;
}

// Null-terminate a JSON string value in-place and return pointer.
// Modifies the buffer — only call once per key.
static char *json_get_string_mut(char *json, const char *key) {
  const char *val_const;
  size_t len;
  if (!json_get_string(json, key, &val_const, &len))
    return nullptr;
  char *val = json + (val_const - json);
  val[len] = '\0';
  return val;
}

// Extract a nested JSON object as a raw string (finds matching braces).
static const char *json_get_object(const char *json, const char *key,
                                   size_t *out_len) {
  const char *val = json_find_key(json, key);
  if (!val || *val != '{')
    return nullptr;
  int depth = 0;
  const char *p = val;
  bool in_str = false;
  while (*p) {
    if (*p == '"' && (p == val || *(p - 1) != '\\'))
      in_str = !in_str;
    if (!in_str) {
      if (*p == '{')
        depth++;
      else if (*p == '}') {
        depth--;
        if (depth == 0) {
          *out_len = (p - val) + 1;
          return val;
        }
      }
    }
    p++;
  }
  return nullptr;
}

// ─── Event Type Mapping ─────────────────────────────────────────────────────

struct EventTypeEntry {
  const char *name;
  VoiceEvent type;
};

static constexpr EventTypeEntry EVENT_MAP[] = {
    {"session.created", VoiceEvent::SessionCreated},
    {"session.updated", VoiceEvent::SessionUpdated},
    {"input_audio_buffer.speech_started",
     VoiceEvent::InputAudioBufferSpeechStarted},
    {"input_audio_buffer.speech_stopped",
     VoiceEvent::InputAudioBufferSpeechStopped},
    {"response.created", VoiceEvent::ResponseCreated},
    {"response.audio.delta", VoiceEvent::ResponseOutputAudioDelta},
    {"response.audio_transcript.delta",
     VoiceEvent::ResponseOutputAudioTranscriptDelta},
    {"response.audio_transcript.done",
     VoiceEvent::ResponseOutputAudioTranscriptDone},
    {"response.audio.done", VoiceEvent::ResponseAudioDone},
    {"response.done", VoiceEvent::ResponseDone},
    {"response.function_call_arguments.done",
     VoiceEvent::ResponseFunctionCallArgumentsDone},
    {"conversation.item.input_audio_transcription.delta",
     VoiceEvent::ConversationItemInputAudioTranscriptionDelta},
    {"conversation.item.input_audio_transcription.completed",
     VoiceEvent::ConversationItemInputAudioTranscriptionDone},
    {"error", VoiceEvent::Error},
};

static constexpr int EVENT_MAP_COUNT = sizeof(EVENT_MAP) / sizeof(EVENT_MAP[0]);

static VoiceEvent resolve_event_type(const char *type_str, size_t len) {
  for (int i = 0; i < EVENT_MAP_COUNT; i++) {
    if (strlen(EVENT_MAP[i].name) == len &&
        memcmp(EVENT_MAP[i].name, type_str, len) == 0) {
      return EVENT_MAP[i].type;
    }
  }
  return VoiceEvent::Unknown;
}

// ─── Parse ──────────────────────────────────────────────────────────────────

bool voice_protocol_parse(char *json, size_t len, VoiceParsedEvent *out) {
  if (!json || len == 0 || !out)
    return false;

  memset(out, 0, sizeof(*out));
  out->type = VoiceEvent::Unknown;

  json[len] = '\0';

  const char *type_str;
  size_t type_len;
  if (!json_get_string(json, "type", &type_str, &type_len))
    return false;

  out->type = resolve_event_type(type_str, type_len);

  switch (out->type) {
  case VoiceEvent::ResponseOutputAudioDelta: {
    const char *delta;
    size_t delta_len;
    if (json_get_string(json, "delta", &delta, &delta_len)) {
      out->audio_delta = delta;
      out->audio_delta_len = delta_len;
    }
    break;
  }

  case VoiceEvent::ResponseOutputAudioTranscriptDelta: {
    out->transcript = json_get_string_mut(json, "delta");
    break;
  }

  case VoiceEvent::ResponseOutputAudioTranscriptDone: {
    out->transcript = json_get_string_mut(json, "transcript");
    break;
  }

  case VoiceEvent::ConversationItemInputAudioTranscriptionDelta: {
    out->transcript = json_get_string_mut(json, "delta");
    break;
  }

  case VoiceEvent::ConversationItemInputAudioTranscriptionDone: {
    out->transcript = json_get_string_mut(json, "transcript");
    break;
  }

  case VoiceEvent::ResponseFunctionCallArgumentsDone: {
    out->call_id = json_get_string_mut(json, "call_id");
    out->function_name = json_get_string_mut(json, "name");
    out->arguments = json_get_string_mut(json, "arguments");
    if (out->function_name) {
      ESP_LOGI(TAG, "Function call: %s (call_id: %s)", out->function_name,
               out->call_id ? out->call_id : "?");
    }
    break;
  }

  case VoiceEvent::ResponseDone: {
    out->response_id = json_get_string_mut(json, "response_id");
    break;
  }

  case VoiceEvent::Error: {
    size_t err_obj_len;
    const char *err_obj = json_get_object(json, "error", &err_obj_len);
    if (err_obj) {
      // "error" is a nested object with "message" inside
      // Point into the sub-object to find the message key
      char *err_mut = json + (err_obj - json);
      out->error_message = json_get_string_mut(err_mut, "message");
    }
    if (out->error_message) {
      ESP_LOGE(TAG, "API error: %s", out->error_message);
    }
    break;
  }

  case VoiceEvent::Unknown: {
    // Log unrecognized events at debug level
    char type_buf[80] = {};
    size_t copy_len =
        type_len < sizeof(type_buf) - 1 ? type_len : sizeof(type_buf) - 1;
    memcpy(type_buf, type_str, copy_len);
    type_buf[copy_len] = '\0';
    ESP_LOGD(TAG, "Unhandled event type: %s", type_buf);
    break;
  }

  default:
    break;
  }

  return true;
}

// ─── Tool Result Frame Builder ──────────────────────────────────────────────

static void json_escape_into(char *dst, size_t dst_len, const char *src) {
  size_t di = 0;
  for (size_t si = 0; src[si] && di < dst_len - 1; si++) {
    char c = src[si];
    if (c == '"' || c == '\\') {
      if (di + 2 >= dst_len)
        break;
      dst[di++] = '\\';
      dst[di++] = c;
    } else if (c == '\n') {
      if (di + 2 >= dst_len)
        break;
      dst[di++] = '\\';
      dst[di++] = 'n';
    } else if (c == '\r') {
      if (di + 2 >= dst_len)
        break;
      dst[di++] = '\\';
      dst[di++] = 'r';
    } else if (c == '\t') {
      if (di + 2 >= dst_len)
        break;
      dst[di++] = '\\';
      dst[di++] = 't';
    } else {
      dst[di++] = c;
    }
  }
  dst[di] = '\0';
}

int voice_protocol_build_tool_result(const char *call_id,
                                     const char *tool_output, char *buf,
                                     size_t buf_len) {
  if (!call_id || !tool_output || !buf || buf_len == 0)
    return -1;

  char escaped_output[512];
  json_escape_into(escaped_output, sizeof(escaped_output), tool_output);

  int written = snprintf(buf, buf_len,
                         "{"
                         "\"type\":\"conversation.item.create\","
                         "\"item\":{"
                         "\"type\":\"function_call_output\","
                         "\"call_id\":\"%s\","
                         "\"output\":\"%s\""
                         "}"
                         "}",
                         call_id, escaped_output);

  if (written < 0 || static_cast<size_t>(written) >= buf_len)
    return -1;

  return written;
}

int voice_protocol_build_response_create(char *buf, size_t buf_len) {
  if (!buf || buf_len == 0)
    return -1;

  int written = snprintf(buf, buf_len, "{\"type\":\"response.create\"}");

  if (written < 0 || static_cast<size_t>(written) >= buf_len)
    return -1;

  return written;
}

// ─── All-in-One Tool Call Handler ───────────────────────────────────────────

bool voice_protocol_handle_tool_call(const VoiceParsedEvent *event,
                                     VoiceToolFrames *frames) {
  if (!event || !frames)
    return false;
  if (event->type != VoiceEvent::ResponseFunctionCallArgumentsDone)
    return false;
  if (!event->call_id || !event->function_name)
    return false;

  memset(frames, 0, sizeof(*frames));

  ToolResult result = {};
  bool recognized =
      voice_tools_execute(event->function_name, event->arguments, &result);

  const char *output =
      recognized ? result.output : "Tool not recognized or execution failed.";

  ESP_LOGI(TAG, "Tool %s → %s: %s", event->function_name,
           result.success ? "OK" : "FAIL", output);

  frames->item_frame_len = voice_protocol_build_tool_result(
      event->call_id, output, frames->item_frame, sizeof(frames->item_frame));

  if (frames->item_frame_len < 0) {
    ESP_LOGE(TAG, "Failed to build tool result frame (buffer too small)");
    return false;
  }

  frames->response_frame_len = voice_protocol_build_response_create(
      frames->response_frame, sizeof(frames->response_frame));

  if (frames->response_frame_len < 0) {
    ESP_LOGE(TAG, "Failed to build response.create frame");
    return false;
  }

  return true;
}
