//! Safe wrappers for the C++ networking bridge functions.
//!
//! These wrap the existing Sonos and WiFi C++ code.

extern "C" {
    fn sonos_play_uri(uri: *const core::ffi::c_char);
    fn sonos_stop_playback();
    fn sonos_set_volume(level: i32);
    fn wifi_manager_init();
}

pub fn play_uri(uri: &str) {
    let c_uri = std::ffi::CString::new(uri).unwrap_or_default();
    unsafe { sonos_play_uri(c_uri.as_ptr()) }
}

pub fn stop_playback() {
    unsafe { sonos_stop_playback() }
}

pub fn set_volume(level: i32) {
    unsafe { sonos_set_volume(level.clamp(0, 100)) }
}

pub fn init_wifi() {
    unsafe { wifi_manager_init() }
}
