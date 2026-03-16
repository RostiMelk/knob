//! WebSocket client for OpenAI Realtime API.
//!
//! Uses esp_idf_svc::ws::client for TLS WebSocket connections.
//!
//! ## Connection Flow
//!
//! 1. Connect to `wss://api.openai.com/v1/realtime`
//! 2. Send `session.update` with config + tools
//! 3. Enter event loop: read frames, dispatch to protocol parser
//! 4. Audio capture task sends `input_audio_buffer.append` frames

use log::{info, warn};

/// WebSocket connection state.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Error,
}

/// OpenAI Realtime API endpoint.
const API_URL: &str = "wss://api.openai.com/v1/realtime";

/// Connect to the OpenAI Realtime API.
///
/// This is a blocking function that should be called from a dedicated task.
pub fn connect(api_key: &str, model: &str) -> anyhow::Result<()> {
    let url = format!("{}?model={}", API_URL, model);
    info!("Connecting to OpenAI Realtime API: {}", url);

    // TODO: Create esp_idf_svc::ws::client::EspWebSocketClient
    // with headers:
    //   Authorization: Bearer <api_key>
    //   OpenAI-Beta: realtime=v1
    let _ = api_key; // suppress unused warning

    // TODO: Implement connection and event loop
    // 1. Connect with TLS
    // 2. Send session.update
    // 3. Loop: read frame → parse → dispatch
    // 4. Handle reconnection on error

    warn!("WebSocket connection not yet implemented");
    Ok(())
}

/// Send a JSON text frame over the WebSocket.
pub fn send_text(_json: &str) -> anyhow::Result<()> {
    // TODO: Send via EspWebSocketClient
    warn!("WebSocket send_text not yet implemented");
    Ok(())
}

/// Send an audio buffer append frame.
///
/// The audio data should be base64-encoded PCM16.
pub fn send_audio(_base64_audio: &str) -> anyhow::Result<()> {
    // TODO: Build input_audio_buffer.append frame and send
    // {
    //   "type": "input_audio_buffer.append",
    //   "audio": "<base64>"
    // }
    Ok(())
}

/// Send a tool call result back to the API.
pub fn send_tool_result(call_id: &str, result: &str) -> anyhow::Result<()> {
    let payload = serde_json::json!({
        "type": "conversation.item.create",
        "item": {
            "type": "function_call_output",
            "call_id": call_id,
            "output": result
        }
    });

    send_text(&payload.to_string())
}

/// Request the model to generate a response after a tool result.
pub fn request_response() -> anyhow::Result<()> {
    send_text("{\"type\": \"response.create\"}")
}
