#include "voice_task.h"
#include "voice_audio.h"
#include "voice_mic.h"
#include "voice_protocol.h"
#include "voice_session.h"
#include "voice_tools.h"

#include "app_config.h"
#include "storage/settings.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_tls.h"
#include "esp_websocket_client.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include <cstdlib>
#include <cstring>

static constexpr const char *TAG = "voice_task";

// ─── Constants ──────────────────────────────────────────────────────────────

static constexpr size_t WS_RX_BUF_SIZE = 16 * 1024;
static constexpr size_t MIC_READ_SAMPLES = 1200; // 50ms @ 24kHz
static constexpr size_t MIC_READ_BYTES = MIC_READ_SAMPLES * sizeof(int16_t);
static constexpr size_t B64_CHUNK_SIZE = 4096; // base64 output per frame
static constexpr int MIC_STREAM_INTERVAL_MS = 50;
static constexpr int IDLE_TIMEOUT_MS = VOICE_IDLE_TIMEOUT_MS;

// Event group bits
static constexpr int BIT_WS_CONNECTED = (1 << 0);
static constexpr int BIT_WS_DISCONNECTED = (1 << 1);
static constexpr int BIT_WS_ERROR = (1 << 2);
static constexpr int BIT_STOP_REQUESTED = (1 << 3);
static constexpr int BIT_SESSION_READY = (1 << 4);

// ─── State ──────────────────────────────────────────────────────────────────

static TaskHandle_t s_task_handle = nullptr;
static esp_websocket_client_handle_t s_ws_client = nullptr;
static EventGroupHandle_t s_events = nullptr;
static bool s_active = false;
static int s_saved_volume = -1;

// ─── Base64 Encoder ─────────────────────────────────────────────────────────

static constexpr char B64_ENC[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const uint8_t *in, size_t in_len, char *out,
                            size_t out_max) {
  size_t oi = 0;
  for (size_t i = 0; i < in_len && oi + 4 <= out_max; i += 3) {
    uint32_t n = static_cast<uint32_t>(in[i]) << 16;
    if (i + 1 < in_len)
      n |= static_cast<uint32_t>(in[i + 1]) << 8;
    if (i + 2 < in_len)
      n |= static_cast<uint32_t>(in[i + 2]);

    out[oi++] = B64_ENC[(n >> 18) & 0x3F];
    out[oi++] = B64_ENC[(n >> 12) & 0x3F];
    out[oi++] = (i + 1 < in_len) ? B64_ENC[(n >> 6) & 0x3F] : '=';
    out[oi++] = (i + 2 < in_len) ? B64_ENC[n & 0x3F] : '=';
  }
  if (oi < out_max)
    out[oi] = '\0';
  return oi;
}

// ─── Post Voice State Event ─────────────────────────────────────────────────

static void post_voice_state(VoiceState state) {
  esp_event_post(APP_EVENT, APP_EVENT_VOICE_STATE, &state, sizeof(state), 0);
}

static void post_transcript(const char *text) {
  if (text && text[0]) {
    esp_event_post(APP_EVENT, APP_EVENT_VOICE_TRANSCRIPT, text,
                   strlen(text) + 1, 0);
  }
}

// ─── WebSocket Event Handler ────────────────────────────────────────────────

static void ws_event_handler(void *arg, esp_event_base_t base, int32_t id,
                             void *data) {
  auto *event = static_cast<esp_websocket_event_data_t *>(data);

  switch (id) {
  case WEBSOCKET_EVENT_CONNECTED:
    ESP_LOGI(TAG, "WebSocket connected");
    xEventGroupSetBits(s_events, BIT_WS_CONNECTED);
    break;

  case WEBSOCKET_EVENT_DISCONNECTED:
    ESP_LOGW(TAG, "WebSocket disconnected");
    xEventGroupSetBits(s_events, BIT_WS_DISCONNECTED);
    break;

  case WEBSOCKET_EVENT_DATA: {
    if (event->op_code == 0x01 && event->data_len > 0) {
      // Text frame — parse as JSON event
      // Make a mutable copy (protocol parser may modify in-place)
      char *json =
          static_cast<char *>(heap_caps_malloc(event->data_len + 1, MALLOC_CAP_SPIRAM));
      if (!json) {
        ESP_LOGE(TAG, "OOM for WS frame (%d bytes)", event->data_len);
        break;
      }
      memcpy(json, event->data_ptr, event->data_len);
      json[event->data_len] = '\0';

      VoiceParsedEvent parsed = {};
      if (voice_protocol_parse(json, event->data_len, &parsed)) {
        switch (parsed.type) {
        case VoiceEvent::SessionCreated:
          ESP_LOGI(TAG, "Session created");
          xEventGroupSetBits(s_events, BIT_SESSION_READY);
          break;

        case VoiceEvent::SessionUpdated:
          ESP_LOGI(TAG, "Session updated — ready for audio");
          post_voice_state(VoiceState::Listening);
          break;

        case VoiceEvent::InputAudioBufferSpeechStarted:
          ESP_LOGD(TAG, "Speech started");
          post_voice_state(VoiceState::Listening);
          break;

        case VoiceEvent::InputAudioBufferSpeechStopped:
          ESP_LOGD(TAG, "Speech stopped");
          post_voice_state(VoiceState::Thinking);
          break;

        case VoiceEvent::ResponseCreated:
          ESP_LOGD(TAG, "Response started");
          break;

        case VoiceEvent::ResponseOutputAudioDelta:
          // Queue audio for playback
          if (parsed.audio_delta && parsed.audio_delta_len > 0) {
            voice_audio_play_b64(parsed.audio_delta, parsed.audio_delta_len);
          }
          post_voice_state(VoiceState::Speaking);
          break;

        case VoiceEvent::ResponseOutputAudioTranscriptDelta:
          if (parsed.transcript) {
            post_transcript(parsed.transcript);
          }
          break;

        case VoiceEvent::ResponseOutputAudioTranscriptDone:
          if (parsed.transcript) {
            post_transcript(parsed.transcript);
          }
          break;

        case VoiceEvent::ResponseAudioDone:
          ESP_LOGD(TAG, "Audio response complete");
          break;

        case VoiceEvent::ResponseDone:
          ESP_LOGI(TAG, "Response done");
          // Back to listening after response completes
          post_voice_state(VoiceState::Listening);
          break;

        case VoiceEvent::ResponseFunctionCallArgumentsDone: {
          // Execute tool and send result back
          VoiceToolFrames frames = {};
          if (voice_protocol_handle_tool_call(&parsed, &frames)) {
            esp_websocket_client_send_text(s_ws_client, frames.item_frame,
                                           frames.item_frame_len,
                                           portMAX_DELAY);
            esp_websocket_client_send_text(s_ws_client, frames.response_frame,
                                           frames.response_frame_len,
                                           portMAX_DELAY);
            ESP_LOGI(TAG, "Tool result sent");
          }
          break;
        }

        case VoiceEvent::ConversationItemInputAudioTranscriptionDone:
          if (parsed.transcript) {
            ESP_LOGI(TAG, "User said: %s", parsed.transcript);
          }
          break;

        case VoiceEvent::Error:
          ESP_LOGE(TAG, "API error: %s",
                   parsed.error_message ? parsed.error_message : "unknown");
          xEventGroupSetBits(s_events, BIT_WS_ERROR);
          break;

        default:
          break;
        }
      }

      heap_caps_free(json);
    }
    break;
  }

  case WEBSOCKET_EVENT_ERROR:
    ESP_LOGE(TAG, "WebSocket error");
    xEventGroupSetBits(s_events, BIT_WS_ERROR);
    break;

  default:
    break;
  }
}

// ─── Send Session Update ────────────────────────────────────────────────────

static bool send_session_update() {
  char *buf = static_cast<char *>(
      heap_caps_malloc(VOICE_SESSION_BUF_SIZE, MALLOC_CAP_SPIRAM));
  if (!buf) {
    ESP_LOGE(TAG, "OOM for session update buffer");
    return false;
  }

  int len = voice_session_build_update(buf, VOICE_SESSION_BUF_SIZE);
  if (len < 0) {
    ESP_LOGE(TAG, "Failed to build session.update");
    heap_caps_free(buf);
    return false;
  }

  int ret = esp_websocket_client_send_text(s_ws_client, buf, len, pdMS_TO_TICKS(5000));
  heap_caps_free(buf);

  if (ret < 0) {
    ESP_LOGE(TAG, "Failed to send session.update");
    return false;
  }

  ESP_LOGI(TAG, "session.update sent (%d bytes)", len);
  return true;
}

// ─── Send Mic Audio Frame ───────────────────────────────────────────────────

static bool send_audio_frame(const int16_t *samples, size_t num_samples) {
  // Build input_audio_buffer.append JSON frame
  // Format: {"type":"input_audio_buffer.append","audio":"<base64>"}
  size_t pcm_bytes = num_samples * sizeof(int16_t);
  size_t b64_len = ((pcm_bytes + 2) / 3) * 4;
  size_t frame_size = 64 + b64_len; // JSON overhead + base64 data

  char *frame = static_cast<char *>(heap_caps_malloc(frame_size, MALLOC_CAP_SPIRAM));
  if (!frame)
    return false;

  int prefix_len =
      snprintf(frame, frame_size,
               "{\"type\":\"input_audio_buffer.append\",\"audio\":\"");

  size_t encoded =
      base64_encode(reinterpret_cast<const uint8_t *>(samples), pcm_bytes,
                    frame + prefix_len, frame_size - prefix_len - 3);

  frame[prefix_len + encoded] = '"';
  frame[prefix_len + encoded + 1] = '}';
  frame[prefix_len + encoded + 2] = '\0';

  int total_len = prefix_len + encoded + 2;
  int ret = esp_websocket_client_send_text(s_ws_client, frame, total_len,
                                           pdMS_TO_TICKS(100));
  heap_caps_free(frame);

  return ret >= 0;
}

// ─── Voice Task Main Loop ───────────────────────────────────────────────────

static void voice_task_fn(void *) {
  ESP_LOGI(TAG, "Voice task started");
  post_voice_state(VoiceState::Connecting);

  // ── Get API key from NVS/settings ──
  char api_key_buf[128] = {};
  settings_get_openai_api_key(api_key_buf, sizeof(api_key_buf));
  const char *api_key = api_key_buf;

  if (!api_key[0]) {
    ESP_LOGE(TAG, "No OPENAI_API_KEY configured — set in .env on SD card");
    post_voice_state(VoiceState::Inactive);
    esp_event_post(APP_EVENT, APP_EVENT_VOICE_DEACTIVATE, nullptr, 0, 0);
    s_active = false;
    vTaskDelete(nullptr);
    return;
  }

  // ── Build auth headers ──
  char auth_header[256];
  snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);

  // ── Connect WebSocket ──
  esp_websocket_client_config_t ws_cfg = {};
  ws_cfg.uri = OPENAI_REALTIME_URL;
  ws_cfg.buffer_size = WS_RX_BUF_SIZE;
  ws_cfg.task_stack = 6144;
  ws_cfg.task_prio = VOICE_TASK_PRIO + 1;
  ws_cfg.pingpong_timeout_sec = 30;
  ws_cfg.headers = nullptr; // We'll set subprotocol + auth below

  // Build custom headers string
  char headers[512];
  snprintf(headers, sizeof(headers),
           "Authorization: %s\r\nOpenAI-Beta: realtime=v1\r\n", auth_header);
  ws_cfg.headers = headers;

  // Use default TLS bundle for certificate verification
  ws_cfg.crt_bundle_attach = esp_crt_bundle_attach;

  s_ws_client = esp_websocket_client_init(&ws_cfg);
  if (!s_ws_client) {
    ESP_LOGE(TAG, "Failed to init WebSocket client");
    post_voice_state(VoiceState::Inactive);
    s_active = false;
    vTaskDelete(nullptr);
    return;
  }

  esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY,
                                ws_event_handler, nullptr);

  xEventGroupClearBits(s_events, 0xFF);

  esp_err_t err = esp_websocket_client_start(s_ws_client);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "WebSocket start failed: %s", esp_err_to_name(err));
    esp_websocket_client_destroy(s_ws_client);
    s_ws_client = nullptr;
    post_voice_state(VoiceState::Inactive);
    s_active = false;
    vTaskDelete(nullptr);
    return;
  }

  // ── Wait for connection ──
  EventBits_t bits = xEventGroupWaitBits(
      s_events, BIT_WS_CONNECTED | BIT_WS_ERROR | BIT_STOP_REQUESTED,
      pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));

  if (!(bits & BIT_WS_CONNECTED)) {
    ESP_LOGE(TAG, "WebSocket connection timeout or error");
    goto cleanup;
  }

  // ── Wait for session.created ──
  bits = xEventGroupWaitBits(s_events,
                             BIT_SESSION_READY | BIT_WS_ERROR | BIT_STOP_REQUESTED,
                             pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));

  if (!(bits & BIT_SESSION_READY)) {
    ESP_LOGE(TAG, "session.created timeout");
    goto cleanup;
  }

  // ── Send session.update ──
  if (!send_session_update()) {
    goto cleanup;
  }

  // ── Start mic and audio ──
  voice_mic_start();
  voice_audio_start();

  // ── Duck Sonos volume ──
  // TODO: save current volume and duck to VOICE_DUCKED_VOLUME
  // This requires reading current volume from sonos module

  ESP_LOGI(TAG, "Voice pipeline active — streaming mic audio");

  // ── Main streaming loop ──
  {
    auto *mic_buf = static_cast<int16_t *>(
        heap_caps_malloc(MIC_READ_BYTES, MALLOC_CAP_SPIRAM));
    if (!mic_buf) {
      ESP_LOGE(TAG, "OOM for mic buffer");
      goto cleanup;
    }

    TickType_t last_activity = xTaskGetTickCount();

    while (s_active) {
      // Check for stop/error
      bits = xEventGroupGetBits(s_events);
      if (bits & (BIT_STOP_REQUESTED | BIT_WS_DISCONNECTED | BIT_WS_ERROR)) {
        ESP_LOGI(TAG, "Stop signal received (bits=0x%lx)", (unsigned long)bits);
        break;
      }

      // Check idle timeout
      if ((xTaskGetTickCount() - last_activity) >
          pdMS_TO_TICKS(IDLE_TIMEOUT_MS)) {
        ESP_LOGI(TAG, "Idle timeout — exiting voice mode");
        break;
      }

      // Read mic samples
      size_t samples_read =
          voice_mic_read(mic_buf, MIC_READ_SAMPLES, MIC_STREAM_INTERVAL_MS);

      if (samples_read > 0) {
        // Check if there's actual audio (simple energy check)
        int32_t energy = 0;
        for (size_t i = 0; i < samples_read; i++) {
          int32_t s = mic_buf[i];
          energy += (s * s) >> 16;
        }
        energy /= static_cast<int32_t>(samples_read);

        if (energy > 10) { // Threshold for "not silence"
          last_activity = xTaskGetTickCount();
        }

        // Send audio frame regardless (VAD is server-side)
        if (!send_audio_frame(mic_buf, samples_read)) {
          ESP_LOGW(TAG, "Failed to send audio frame");
        }
      }

      vTaskDelay(pdMS_TO_TICKS(5));
    }

    heap_caps_free(mic_buf);
  }

cleanup:
  ESP_LOGI(TAG, "Voice task shutting down");

  // Stop mic and audio
  voice_mic_stop();
  voice_audio_stop();

  // Close WebSocket
  if (s_ws_client) {
    esp_websocket_client_close(s_ws_client, pdMS_TO_TICKS(2000));
    esp_websocket_client_destroy(s_ws_client);
    s_ws_client = nullptr;
  }

  // Restore Sonos volume
  // TODO: restore s_saved_volume

  post_voice_state(VoiceState::Inactive);
  s_active = false;
  s_task_handle = nullptr;

  ESP_LOGI(TAG, "Voice task ended");
  vTaskDelete(nullptr);
}

// ─── Public API ─────────────────────────────────────────────────────────────

void voice_task_init() {
  s_events = xEventGroupCreate();
  configASSERT(s_events);
  ESP_LOGI(TAG, "Voice task initialized");
}

void voice_task_start() {
  if (s_active) {
    ESP_LOGW(TAG, "Voice task already active");
    return;
  }

  s_active = true;
  xEventGroupClearBits(s_events, 0xFF);

  // Initialize mic and audio hardware
  voice_mic_init();
  voice_audio_init();

  BaseType_t ret = xTaskCreatePinnedToCore(
      voice_task_fn, "voice", VOICE_TASK_STACK, nullptr, VOICE_TASK_PRIO,
      &s_task_handle, VOICE_TASK_CORE);

  if (ret != pdPASS) {
    ESP_LOGE(TAG, "Failed to create voice task");
    s_active = false;
  }
}

void voice_task_stop() {
  if (!s_active) {
    return;
  }

  ESP_LOGI(TAG, "Requesting voice task stop");
  xEventGroupSetBits(s_events, BIT_STOP_REQUESTED);

  // Wait for task to finish (up to 5s)
  for (int i = 0; i < 50 && s_active; i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (s_active) {
    ESP_LOGW(TAG, "Voice task did not stop gracefully");
    s_active = false;
  }
}

bool voice_task_is_active() { return s_active; }
