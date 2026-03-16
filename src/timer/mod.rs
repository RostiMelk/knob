//! Countdown timer module — Rust port of main/timer/timer.cpp.
//!
//! Uses a deadline-based approach with a background monitor task.
//! When the timer fires, it posts an event via the bridge and
//! updates the UI using `bridge::ui::show_timer()`.

use log::info;
use std::sync::Mutex;
use std::time::{Duration, Instant};

/// Active timer state.
struct TimerState {
    /// When the timer expires (None = no active timer).
    deadline: Option<Instant>,
    /// Total duration in seconds (for UI display).
    total_secs: u32,
    /// Human-readable label (e.g., "Pizza timer").
    label: String,
    /// Whether the monitor task has been spawned.
    monitor_spawned: bool,
}

impl TimerState {
    const fn new() -> Self {
        Self {
            deadline: None,
            total_secs: 0,
            label: String::new(),
            monitor_spawned: false,
        }
    }
}

static STATE: Mutex<TimerState> = Mutex::new(TimerState::new());

/// Start a countdown timer.
///
/// If a timer is already running, it is replaced.
/// Automatically spawns the monitor task on first use.
pub fn start(seconds: u32, label: &str) {
    let mut state = STATE.lock().unwrap();
    state.deadline = Some(Instant::now() + Duration::from_secs(seconds as u64));
    state.total_secs = seconds;
    state.label = label.to_string();

    info!("Timer started: {}s ({})", seconds, label);

    // Update UI immediately
    crate::bridge::ui::show_timer(seconds, label);

    // Post timer-started event
    if let Err(e) = crate::bridge::events::post_timer_started() {
        log::warn!("Failed to post timer-started event: {}", e);
    }

    // Spawn monitor task on first use
    if !state.monitor_spawned {
        state.monitor_spawned = true;
        std::thread::Builder::new()
            .name("timer_mon".into())
            .stack_size(4096)
            .spawn(monitor_task)
            .ok();
    }
}

/// Cancel the active timer.
pub fn cancel() {
    let mut state = STATE.lock().unwrap();
    if state.deadline.is_some() {
        info!("Timer cancelled");
        state.deadline = None;
        state.total_secs = 0;
        state.label.clear();

        // Clear the timer display
        crate::bridge::ui::show_timer(0, "");
    }
}

/// Get remaining seconds (0 if no timer active).
pub fn remaining() -> u32 {
    let state = STATE.lock().unwrap();
    match state.deadline {
        Some(deadline) => {
            let now = Instant::now();
            if now >= deadline {
                0
            } else {
                (deadline - now).as_secs() as u32
            }
        }
        None => 0,
    }
}

/// Get the label of the active timer.
pub fn label() -> String {
    STATE.lock().unwrap().label.clone()
}

/// Check if a timer is currently active.
pub fn is_active() -> bool {
    STATE.lock().unwrap().deadline.is_some()
}

/// Background task that monitors the timer and fires when it expires.
fn monitor_task() {
    loop {
        std::thread::sleep(Duration::from_secs(1));

        let mut state = STATE.lock().unwrap();
        if let Some(deadline) = state.deadline {
            let now = Instant::now();
            if now >= deadline {
                // Timer fired!
                info!("Timer fired: {}", state.label);

                // Post timer-fired event to C++ UI
                if let Err(e) = crate::bridge::events::post_timer_fired() {
                    log::warn!("Failed to post timer-fired event: {}", e);
                }

                // Show "Timer done" on UI
                crate::bridge::ui::show_timer(0, "Timer done!");

                // Clear state
                state.deadline = None;
                state.total_secs = 0;
                state.label.clear();
            } else {
                // Update UI with remaining time
                let remaining = (deadline - now).as_secs() as u32;
                let lbl = state.label.clone();
                drop(state); // Release lock before calling bridge
                crate::bridge::ui::show_timer(remaining, &lbl);
            }
        }
    }
}
