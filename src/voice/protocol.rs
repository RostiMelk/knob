//! OpenAI Realtime API frame parser.
//!
//! Parses JSON frames from the WebSocket connection.
//! Uses serde_json for type-safe parsing (unlike the C++ hand-rolled parser).

use serde::Deserialize;

/// Top-level server event — we only parse the `type` field first,
/// then match on it to parse the full payload.
#[derive(Debug, Deserialize)]
pub struct ServerEvent {
    pub r#type: String,

    /// For error events.
    pub error: Option<ErrorPayload>,
}

#[derive(Debug, Deserialize)]
pub struct ErrorPayload {
    pub message: String,
    pub code: Option<String>,
}

/// Parsed server events we care about.
#[derive(Debug)]
pub enum ParsedEvent {
    /// Session created/updated confirmation.
    SessionUpdated,

    /// Input audio buffer speech started (VAD detected speech).
    SpeechStarted,

    /// Input audio buffer speech stopped.
    SpeechStopped,

    /// Response audio delta — contains base64-encoded PCM16 audio.
    AudioDelta { delta: String },

    /// Response audio done.
    AudioDone,

    /// Response text/transcript delta.
    TranscriptDelta { delta: String },

    /// Full transcript for a completed response.
    TranscriptDone { transcript: String },

    /// Input audio transcription completed.
    InputTranscript { transcript: String },

    /// Tool call requested by the model.
    ToolCall {
        call_id: String,
        name: String,
        arguments: String,
    },

    /// Response completed.
    ResponseDone,

    /// Error from the server.
    Error { message: String },

    /// Unrecognized event type.
    Unknown { event_type: String },
}

/// Parse a raw JSON frame from the WebSocket into a typed event.
pub fn parse_frame(json: &str) -> ParsedEvent {
    let value: serde_json::Value = match serde_json::from_str(json) {
        Ok(v) => v,
        Err(e) => {
            return ParsedEvent::Error {
                message: format!("JSON parse error: {}", e),
            };
        }
    };

    let event_type = value["type"].as_str().unwrap_or("");

    match event_type {
        "session.created" | "session.updated" => ParsedEvent::SessionUpdated,

        "input_audio_buffer.speech_started" => ParsedEvent::SpeechStarted,

        "input_audio_buffer.speech_stopped" => ParsedEvent::SpeechStopped,

        "response.audio.delta" => ParsedEvent::AudioDelta {
            delta: value["delta"].as_str().unwrap_or("").to_string(),
        },

        "response.audio.done" => ParsedEvent::AudioDone,

        "response.audio_transcript.delta" => ParsedEvent::TranscriptDelta {
            delta: value["delta"].as_str().unwrap_or("").to_string(),
        },

        "response.audio_transcript.done" => ParsedEvent::TranscriptDone {
            transcript: value["transcript"].as_str().unwrap_or("").to_string(),
        },

        "conversation.item.input_audio_transcription.completed" => {
            ParsedEvent::InputTranscript {
                transcript: value["transcript"].as_str().unwrap_or("").to_string(),
            }
        }

        "response.function_call_arguments.done" => ParsedEvent::ToolCall {
            call_id: value["call_id"].as_str().unwrap_or("").to_string(),
            name: value["name"].as_str().unwrap_or("").to_string(),
            arguments: value["arguments"].as_str().unwrap_or("").to_string(),
        },

        "response.done" => ParsedEvent::ResponseDone,

        "error" => {
            let msg = value["error"]["message"]
                .as_str()
                .unwrap_or("Unknown error")
                .to_string();
            ParsedEvent::Error { message: msg }
        }

        _ => ParsedEvent::Unknown {
            event_type: event_type.to_string(),
        },
    }
}
