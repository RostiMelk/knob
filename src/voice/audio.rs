//! Audio I/O — PDM microphone capture and I2S DAC playback.
//!
//! Uses esp_idf_hal::i2s for hardware audio interfaces.
//!
//! ## Audio Pipeline
//!
//! ```text
//! PDM Mic → capture_task → [ring buffer] → WebSocket (base64 PCM16)
//! WebSocket (base64 PCM16) → [ring buffer] → playback_task → I2S DAC
//! ```

use log::info;
use std::sync::atomic::{AtomicBool, Ordering};

/// Whether audio capture is currently active.
static CAPTURING: AtomicBool = AtomicBool::new(false);

/// Whether audio playback is currently active.
static PLAYING: AtomicBool = AtomicBool::new(false);

/// Audio format constants matching OpenAI Realtime API requirements.
pub const SAMPLE_RATE: u32 = 24000;
pub const BITS_PER_SAMPLE: u32 = 16;
pub const CHANNELS: u32 = 1;

/// Size of audio chunks sent over WebSocket (100ms of audio).
pub const CAPTURE_CHUNK_SAMPLES: usize = (SAMPLE_RATE as usize) / 10; // 2400 samples
pub const CAPTURE_CHUNK_BYTES: usize = CAPTURE_CHUNK_SAMPLES * 2; // 4800 bytes (16-bit)

/// Start audio capture from the PDM microphone.
pub fn start_capture() -> anyhow::Result<()> {
    if CAPTURING.load(Ordering::Relaxed) {
        return Ok(());
    }

    info!("Starting audio capture ({}Hz, {}bit, {}ch)",
        SAMPLE_RATE, BITS_PER_SAMPLE, CHANNELS);

    CAPTURING.store(true, Ordering::Relaxed);

    // TODO: Initialize I2S PDM RX channel
    // TODO: Spawn capture task that reads from I2S and pushes to ring buffer

    Ok(())
}

/// Stop audio capture.
pub fn stop_capture() {
    if CAPTURING.load(Ordering::Relaxed) {
        info!("Stopping audio capture");
        CAPTURING.store(false, Ordering::Relaxed);
        // TODO: Stop I2S PDM RX channel
    }
}

/// Start audio playback to the I2S DAC.
pub fn start_playback() -> anyhow::Result<()> {
    if PLAYING.load(Ordering::Relaxed) {
        return Ok(());
    }

    info!("Starting audio playback");
    PLAYING.store(true, Ordering::Relaxed);

    // TODO: Initialize I2S TX channel
    // TODO: Spawn playback task that reads from ring buffer and writes to I2S

    Ok(())
}

/// Stop audio playback.
pub fn stop_playback() {
    if PLAYING.load(Ordering::Relaxed) {
        info!("Stopping audio playback");
        PLAYING.store(false, Ordering::Relaxed);
        // TODO: Stop I2S TX channel
    }
}

/// Feed decoded PCM16 audio data into the playback buffer.
pub fn feed_playback_data(_data: &[u8]) {
    // TODO: Push data into the playback ring buffer
}

/// Read captured audio data (returns None if no data available).
pub fn read_capture_data() -> Option<Vec<u8>> {
    // TODO: Pop data from the capture ring buffer
    None
}

pub fn is_capturing() -> bool {
    CAPTURING.load(Ordering::Relaxed)
}

pub fn is_playing() -> bool {
    PLAYING.load(Ordering::Relaxed)
}
