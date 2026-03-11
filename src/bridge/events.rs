//! Event bridge between C++ and Rust via esp_event.

use esp_idf_svc::eventloop::EspSystemEventLoop;
use log::info;

/// Event IDs matching app_events.h
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum AppEvent {
    EncoderRotate = 0,
    TouchTap = 1,
    TouchLongPress = 2,
    TouchDoubleTap = 3,
    StationChanged = 4,
    VolumeChanged = 5,
    PlayRequested = 6,
    StopRequested = 7,
    SonosStateUpdate = 8,
    WifiConnected = 9,
    WifiDisconnected = 10,
    VoiceActivate = 11,
    VoiceDeactivate = 12,
    VoiceState = 13,
    VoiceTranscript = 14,
    TimerStarted = 15,
    TimerFired = 16,
}

pub fn subscribe_all(_sys_loop: &EspSystemEventLoop) -> anyhow::Result<()> {
    // TODO: Subscribe to input events from C++ UI
    // This will be implemented when the event bridge is wired up
    info!("Event subscriptions registered (placeholder)");
    Ok(())
}
