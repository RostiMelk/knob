//! Voice mode module — OpenAI Realtime API integration.
//!
//! This is the primary new Rust code (Phase 2).
//! Handles WebSocket connection, audio I/O, and tool execution.

pub mod audio;
pub mod protocol;
pub mod session;
pub mod tools;
pub mod websocket;

use log::info;

pub fn init() -> anyhow::Result<()> {
    info!("Voice module initialized (skeleton)");
    Ok(())
}
