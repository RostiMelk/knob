//! Voice mode module — OpenAI Realtime API integration.
//!
//! This is the primary new Rust code (Phase 2).
//! Handles WebSocket connection, audio I/O, and tool execution.
//!
//! ## State Machine
//!
//! ```text
//! Idle → Connecting → Listening → Thinking → Speaking → Listening
//!   ↑                                                      |
//!   └──────────────────── (deactivate) ─────────────────────┘
//! ```

pub mod audio;
pub mod protocol;
pub mod session;
pub mod tools;
pub mod websocket;

use crate::bridge::ui::VoiceState;
use log::info;
use std::sync::atomic::{AtomicU8, Ordering};

/// Current voice mode state.
static VOICE_STATE: AtomicU8 = AtomicU8::new(0xFF); // 0xFF = Idle/inactive

/// Voice mode states (matches VoiceState enum values for bridge calls).
const STATE_IDLE: u8 = 0xFF;
const STATE_CONNECTING: u8 = 0;
const STATE_LISTENING: u8 = 1;
const STATE_THINKING: u8 = 2;
const STATE_SPEAKING: u8 = 3;

/// Activate voice mode — begins the connection flow.
pub fn activate() -> anyhow::Result<()> {
    info!("Voice mode activating...");

    set_state(STATE_CONNECTING);
    crate::bridge::ui::show_voice_mode(true);
    crate::bridge::ui::set_voice_state(VoiceState::Connecting);

    // TODO: Spawn WebSocket connection task
    // websocket::connect(session::build_config())?;

    Ok(())
}

/// Deactivate voice mode — tears down the connection.
pub fn deactivate() {
    info!("Voice mode deactivating...");

    set_state(STATE_IDLE);
    crate::bridge::ui::show_voice_mode(false);

    // TODO: Close WebSocket, stop audio capture
}

/// Get the current voice state.
pub fn current_state() -> u8 {
    VOICE_STATE.load(Ordering::Relaxed)
}

/// Check if voice mode is active.
pub fn is_active() -> bool {
    current_state() != STATE_IDLE
}

/// Update voice state and notify the UI bridge.
fn set_state(state: u8) {
    VOICE_STATE.store(state, Ordering::Relaxed);

    // Notify C++ UI via event bridge
    if let Err(e) = crate::bridge::events::post_voice_state(state) {
        log::warn!("Failed to post voice state event: {}", e);
    }

    // Update UI directly
    match state {
        STATE_CONNECTING => crate::bridge::ui::set_voice_state(VoiceState::Connecting),
        STATE_LISTENING => crate::bridge::ui::set_voice_state(VoiceState::Listening),
        STATE_THINKING => crate::bridge::ui::set_voice_state(VoiceState::Thinking),
        STATE_SPEAKING => crate::bridge::ui::set_voice_state(VoiceState::Speaking),
        _ => {} // Idle — no UI state to set
    }
}

/// Called when the WebSocket connection is established.
pub fn on_connected() {
    info!("Voice: WebSocket connected");
    set_state(STATE_LISTENING);
}

/// Called when the model starts generating a response.
pub fn on_thinking() {
    set_state(STATE_THINKING);
}

/// Called when the model starts speaking.
pub fn on_speaking() {
    set_state(STATE_SPEAKING);
}

/// Called when the model finishes speaking (back to listening).
pub fn on_speech_done() {
    if is_active() {
        set_state(STATE_LISTENING);
    }
}

/// Called when a transcript is received (user or assistant).
pub fn on_transcript(text: &str, is_user: bool) {
    crate::bridge::ui::set_voice_transcript(text, is_user);
}
