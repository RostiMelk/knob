//! Event bridge between C++ and Rust via esp_event.
//!
//! Provides:
//! - `AppEvent` enum matching `app_events.h` (sequential IDs 0–16)
//! - `EventSubscriber` builder for registering typed callbacks
//! - `post_event()` for Rust → C++ event posting
//!
//! TODO: The C++ side needs `ESP_EVENT_DEFINE_BASE(APP_EVENT_BASE)` in a .c file
//! to create the actual event base variable. The `#define APP_EVENT_BASE "APP_EVENT"`
//! alone is not sufficient for esp_event. For now we use the string approach on
//! the Rust side.

use log::{info, warn};
use std::ffi::c_void;
use std::sync::Mutex;

/// Event IDs matching app_events.h — sequential integers starting at 0.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum AppEvent {
    EncoderRotate     = 0,
    TouchTap          = 1,
    TouchLongPress    = 2,
    TouchDoubleTap    = 3,
    StationChanged    = 4,
    VolumeChanged     = 5,
    PlayRequested     = 6,
    StopRequested     = 7,
    SonosStateUpdate  = 8,
    WifiConnected     = 9,
    WifiDisconnected  = 10,
    VoiceActivate     = 11,
    VoiceDeactivate   = 12,
    VoiceState        = 13,
    VoiceTranscript   = 14,
    TimerStarted      = 15,
    TimerFired        = 16,
}

impl AppEvent {
    /// Convert from raw i32 event ID to typed enum.
    pub fn from_id(id: i32) -> Option<Self> {
        match id {
            0  => Some(Self::EncoderRotate),
            1  => Some(Self::TouchTap),
            2  => Some(Self::TouchLongPress),
            3  => Some(Self::TouchDoubleTap),
            4  => Some(Self::StationChanged),
            5  => Some(Self::VolumeChanged),
            6  => Some(Self::PlayRequested),
            7  => Some(Self::StopRequested),
            8  => Some(Self::SonosStateUpdate),
            9  => Some(Self::WifiConnected),
            10 => Some(Self::WifiDisconnected),
            11 => Some(Self::VoiceActivate),
            12 => Some(Self::VoiceDeactivate),
            13 => Some(Self::VoiceState),
            14 => Some(Self::VoiceTranscript),
            15 => Some(Self::TimerStarted),
            16 => Some(Self::TimerFired),
            _  => None,
        }
    }
}

/// The event base string matching APP_EVENT_BASE in app_events.h.
///
/// TODO: For esp_event to fully work, the C++ side must call
/// `ESP_EVENT_DEFINE_BASE(APP_EVENT_BASE)` somewhere to create the actual
/// event base variable. The `#define` alone isn't enough.
fn app_event_base() -> esp_idf_svc::sys::esp_event_base_t {
    b"APP_EVENT\0".as_ptr() as esp_idf_svc::sys::esp_event_base_t
}

// ---------------------------------------------------------------------------
// Event data structs (passed as event_data from C++)
// ---------------------------------------------------------------------------

/// Encoder rotation event data.
#[derive(Debug, Clone, Copy)]
#[repr(C)]
pub struct EncoderData {
    pub delta: i32,
}

/// Touch event data.
#[derive(Debug, Clone, Copy)]
#[repr(C)]
pub struct TouchData {
    pub x: i16,
    pub y: i16,
}

/// Station changed event data.
#[derive(Debug, Clone, Copy)]
#[repr(C)]
pub struct StationData {
    pub index: u8,
}

/// Volume changed event data.
#[derive(Debug, Clone, Copy)]
#[repr(C)]
pub struct VolumeData {
    pub level: u8,
}

// ---------------------------------------------------------------------------
// EventSubscriber — builder pattern for typed event callbacks
// ---------------------------------------------------------------------------

type EncoderCallback = Box<dyn Fn(&EncoderData) + Send + 'static>;
type WifiCallback = Box<dyn Fn(bool) + Send + 'static>;
type VoiceButtonCallback = Box<dyn Fn() + Send + 'static>;
type StationCallback = Box<dyn Fn(&StationData) + Send + 'static>;
type VolumeCallback = Box<dyn Fn(&VolumeData) + Send + 'static>;
type PlaybackCallback = Box<dyn Fn() + Send + 'static>;

/// Builder for subscribing to app events with typed callbacks.
pub struct EventSubscriber {
    on_encoder: Option<EncoderCallback>,
    on_wifi: Option<WifiCallback>,
    on_voice_button: Option<VoiceButtonCallback>,
    on_station: Option<StationCallback>,
    on_volume: Option<VolumeCallback>,
    on_play: Option<PlaybackCallback>,
    on_stop: Option<PlaybackCallback>,
}

impl EventSubscriber {
    pub fn new() -> Self {
        Self {
            on_encoder: None,
            on_wifi: None,
            on_voice_button: None,
            on_station: None,
            on_volume: None,
            on_play: None,
            on_stop: None,
        }
    }

    pub fn on_encoder_turn<F>(mut self, f: F) -> Self
    where
        F: Fn(&EncoderData) + Send + 'static,
    {
        self.on_encoder = Some(Box::new(f));
        self
    }

    pub fn on_wifi<F>(mut self, f: F) -> Self
    where
        F: Fn(bool) + Send + 'static,
    {
        self.on_wifi = Some(Box::new(f));
        self
    }

    pub fn on_voice_button<F>(mut self, f: F) -> Self
    where
        F: Fn() + Send + 'static,
    {
        self.on_voice_button = Some(Box::new(f));
        self
    }

    pub fn on_station_changed<F>(mut self, f: F) -> Self
    where
        F: Fn(&StationData) + Send + 'static,
    {
        self.on_station = Some(Box::new(f));
        self
    }

    pub fn on_volume_changed<F>(mut self, f: F) -> Self
    where
        F: Fn(&VolumeData) + Send + 'static,
    {
        self.on_volume = Some(Box::new(f));
        self
    }

    pub fn on_play_requested<F>(mut self, f: F) -> Self
    where
        F: Fn() + Send + 'static,
    {
        self.on_play = Some(Box::new(f));
        self
    }

    pub fn on_stop_requested<F>(mut self, f: F) -> Self
    where
        F: Fn() + Send + 'static,
    {
        self.on_stop = Some(Box::new(f));
        self
    }

    /// Register all callbacks with the esp_event system.
    ///
    /// The subscriber is moved into a static Mutex so the C callback
    /// can dispatch to the correct Rust closure.
    pub fn subscribe(self) -> anyhow::Result<()> {
        // Store the subscriber in a static so the C callback can access it
        static SUBSCRIBER: Mutex<Option<EventSubscriber>> = Mutex::new(None);

        *SUBSCRIBER.lock().unwrap() = Some(self);

        // Register a single handler for all APP_EVENT events
        let ret = unsafe {
            esp_idf_svc::sys::esp_event_handler_register(
                app_event_base(),
                esp_idf_svc::sys::ESP_EVENT_ANY_ID,
                Some(event_handler),
                std::ptr::null_mut(),
            )
        };

        if ret != esp_idf_svc::sys::ESP_OK {
            anyhow::bail!("Failed to register event handler: {}", ret);
        }

        info!("Event subscriptions registered");

        /// C-compatible event handler that dispatches to Rust callbacks.
        unsafe extern "C" fn event_handler(
            _handler_arg: *mut c_void,
            _event_base: esp_idf_svc::sys::esp_event_base_t,
            event_id: i32,
            event_data: *mut c_void,
        ) {
            let guard = SUBSCRIBER.lock().unwrap();
            let sub = match guard.as_ref() {
                Some(s) => s,
                None => return,
            };

            match AppEvent::from_id(event_id) {
                Some(AppEvent::EncoderRotate) => {
                    if let Some(ref cb) = sub.on_encoder {
                        if !event_data.is_null() {
                            let data = &*(event_data as *const EncoderData);
                            cb(data);
                        }
                    }
                }
                Some(AppEvent::WifiConnected) => {
                    if let Some(ref cb) = sub.on_wifi {
                        cb(true);
                    }
                }
                Some(AppEvent::WifiDisconnected) => {
                    if let Some(ref cb) = sub.on_wifi {
                        cb(false);
                    }
                }
                Some(AppEvent::VoiceActivate) => {
                    if let Some(ref cb) = sub.on_voice_button {
                        cb();
                    }
                }
                Some(AppEvent::StationChanged) => {
                    if let Some(ref cb) = sub.on_station {
                        if !event_data.is_null() {
                            let data = &*(event_data as *const StationData);
                            cb(data);
                        }
                    }
                }
                Some(AppEvent::VolumeChanged) => {
                    if let Some(ref cb) = sub.on_volume {
                        if !event_data.is_null() {
                            let data = &*(event_data as *const VolumeData);
                            cb(data);
                        }
                    }
                }
                Some(AppEvent::PlayRequested) => {
                    if let Some(ref cb) = sub.on_play {
                        cb();
                    }
                }
                Some(AppEvent::StopRequested) => {
                    if let Some(ref cb) = sub.on_stop {
                        cb();
                    }
                }
                Some(evt) => {
                    info!("Unhandled app event: {:?}", evt);
                }
                None => {
                    warn!("Unknown event ID: {}", event_id);
                }
            }
        }

        Ok(())
    }
}

// ---------------------------------------------------------------------------
// Event posting — Rust → C++ (for timer, voice state, etc.)
// ---------------------------------------------------------------------------

/// Post an event to the esp_event loop (Rust → C++).
///
/// `data` is optional; pass `None` for events with no payload.
pub fn post_event(event: AppEvent, data: Option<&[u8]>) -> anyhow::Result<()> {
    let (data_ptr, data_len) = match data {
        Some(d) => (d.as_ptr() as *const c_void, d.len()),
        None => (std::ptr::null(), 0),
    };

    let ret = unsafe {
        esp_idf_svc::sys::esp_event_post(
            app_event_base(),
            event as i32,
            data_ptr as *mut c_void,
            data_len as i32,
            0, // portMAX_DELAY equivalent; 0 = don't block
        )
    };

    if ret != esp_idf_svc::sys::ESP_OK {
        anyhow::bail!("Failed to post event {:?}: {}", event, ret);
    }

    Ok(())
}

/// Convenience: post a timer-started event.
pub fn post_timer_started() -> anyhow::Result<()> {
    post_event(AppEvent::TimerStarted, None)
}

/// Convenience: post a timer-fired event.
pub fn post_timer_fired() -> anyhow::Result<()> {
    post_event(AppEvent::TimerFired, None)
}

/// Convenience: post a voice state change event.
pub fn post_voice_state(state: u8) -> anyhow::Result<()> {
    post_event(AppEvent::VoiceState, Some(&[state]))
}
