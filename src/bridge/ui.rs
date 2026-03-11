//! Safe wrappers for the C++ UI bridge functions.
//!
//! All functions here are thread-safe — they enqueue commands
//! that are processed in the LVGL task.

use std::ffi::CString;

extern "C" {
    pub fn ui_bridge_init();
    pub fn ui_bridge_task_run();
    fn ui_bridge_set_volume(volume: u8);
    fn ui_bridge_set_station(index: u8, name: *const core::ffi::c_char, color: u32);
    fn ui_bridge_set_play_state(state: u8);
    fn ui_bridge_set_wifi_status(connected: bool);
    fn ui_bridge_set_speaker(name: *const core::ffi::c_char);
    fn ui_bridge_show_voice_mode(active: bool);
    fn ui_bridge_set_voice_state(state: u8);
    fn ui_bridge_set_voice_transcript(text: *const core::ffi::c_char, is_user: bool);
    fn ui_bridge_show_timer(remaining_sec: u32, label: *const core::ffi::c_char);
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum PlayState {
    Stopped = 0,
    Playing = 1,
    Transitioning = 2,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum VoiceState {
    Connecting = 0,
    Listening = 1,
    Thinking = 2,
    Speaking = 3,
}

pub fn set_volume(volume: u8) {
    unsafe { ui_bridge_set_volume(volume.min(100)) }
}

pub fn set_station(index: u8, name: &str, color: u32) {
    let c_name = CString::new(name).unwrap_or_default();
    unsafe { ui_bridge_set_station(index, c_name.as_ptr(), color) }
}

pub fn set_play_state(state: PlayState) {
    unsafe { ui_bridge_set_play_state(state as u8) }
}

pub fn set_wifi_status(connected: bool) {
    unsafe { ui_bridge_set_wifi_status(connected) }
}

pub fn set_speaker(name: &str) {
    let c_name = CString::new(name).unwrap_or_default();
    unsafe { ui_bridge_set_speaker(c_name.as_ptr()) }
}

pub fn show_voice_mode(active: bool) {
    unsafe { ui_bridge_show_voice_mode(active) }
}

pub fn set_voice_state(state: VoiceState) {
    unsafe { ui_bridge_set_voice_state(state as u8) }
}

pub fn set_voice_transcript(text: &str, is_user: bool) {
    let c_text = CString::new(text).unwrap_or_default();
    unsafe { ui_bridge_set_voice_transcript(c_text.as_ptr(), is_user) }
}

pub fn show_timer(remaining_sec: u32, label: &str) {
    let c_label = CString::new(label).unwrap_or_default();
    unsafe { ui_bridge_show_timer(remaining_sec, c_label.as_ptr()) }
}
