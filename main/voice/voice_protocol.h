#pragma once

#include <cstddef>

// ─── OpenAI Realtime API Protocol Handler ───────────────────────────────────
//
// Parses incoming WebSocket text frames from the OpenAI Realtime API,
// dispatches tool calls via voice_tools, and produces response frames
// to send back over the WebSocket.
//
// Designed to be called from the voice task's WebSocket event handler.
// Does NOT own the WebSocket connection — caller is responsible for
// sending any output frames produced by these functions.

// ─── Incoming Event Types (subset we care about) ────────────────────────────

enum class VoiceEvent : int {
  Unknown,
  SessionCreated,
  SessionUpdated,
  InputAudioBufferSpeechStarted,
  InputAudioBufferSpeechStopped,
  ResponseCreated,
  ResponseOutputAudioDelta,
  ResponseOutputAudioTranscriptDelta,
  ResponseOutputAudioTranscriptDone,
  ResponseAudioDone,
  ResponseDone,
  ResponseFunctionCallArgumentsDone,
  ConversationItemInputAudioTranscriptionDelta,
  ConversationItemInputAudioTranscriptionDone,
  Error,
};

// ─── Parsed Event ───────────────────────────────────────────────────────────
//
// Fields are valid only for the lifetime of the JSON buffer passed to
// voice_protocol_parse(). Callers must consume or copy before returning.

struct VoiceParsedEvent {
  VoiceEvent type;

  // For ResponseOutputAudioDelta: base64-encoded PCM chunk
  const char *audio_delta;
  size_t audio_delta_len;

  // For transcript events: partial or complete text
  const char *transcript;

  // For ResponseFunctionCallArgumentsDone:
  const char *call_id;
  const char *function_name;
  const char *arguments;

  // For Error:
  const char *error_message;

  // For ResponseDone: the response_id
  const char *response_id;
};

// ─── Parse ──────────────────────────────────────────────────────────────────
//
// Parses a text frame JSON string from the Realtime API into a
// VoiceParsedEvent. The json buffer may be modified in-place (for
// null-termination of values).
//
// Returns true if the event was successfully parsed (even if type is Unknown).

bool voice_protocol_parse(char *json, size_t len, VoiceParsedEvent *out);

// ─── Tool Call Response Builder ─────────────────────────────────────────────
//
// After receiving a ResponseFunctionCallArgumentsDone event:
//   1. Execute the tool via voice_tools_execute()
//   2. Call voice_protocol_build_tool_result() to build the
//      conversation.item.create JSON
//   3. Send that frame over the WebSocket
//   4. Call voice_protocol_build_response_create() to ask the model
//      to continue generating after processing the tool result
//   5. Send that frame over the WebSocket
//
// buf/buf_len: output buffer for the JSON frame
// Returns bytes written (excluding null terminator), or -1 if buffer too small.

int voice_protocol_build_tool_result(const char *call_id,
                                     const char *tool_output, char *buf,
                                     size_t buf_len);

int voice_protocol_build_response_create(char *buf, size_t buf_len);

// ─── Convenience: Execute Tool Call and Build Response ───────────────────────
//
// All-in-one: executes the tool from a parsed function call event, then
// writes two JSON frames into the provided buffers:
//   item_buf   → conversation.item.create (tool result)
//   resp_buf   → response.create (trigger model continuation)
//
// Returns true if the tool was executed and both frames were built.
// The caller must send item_buf first, then resp_buf.

struct VoiceToolFrames {
  char item_frame[1024];
  int item_frame_len;
  char response_frame[256];
  int response_frame_len;
};

bool voice_protocol_handle_tool_call(const VoiceParsedEvent *event,
                                     VoiceToolFrames *frames);
