//! Countdown timer module — Rust port of main/timer/timer.cpp.
//!
//! Uses std::sync::atomic for thread-safe state (same pattern as C++ version).

use log::info;
use std::sync::atomic::{AtomicI32, Ordering};

static REMAINING: AtomicI32 = AtomicI32::new(0);

pub fn init() -> anyhow::Result<()> {
    info!("Timer initialized");
    Ok(())
}

pub fn start(seconds: i32, _label: &str) {
    REMAINING.store(seconds, Ordering::Relaxed);
    info!("Timer started: {}s", seconds);
}

pub fn cancel() {
    REMAINING.store(0, Ordering::Relaxed);
    info!("Timer cancelled");
}

pub fn remaining() -> i32 {
    REMAINING.load(Ordering::Relaxed)
}

pub fn tick() {
    let current = REMAINING.load(Ordering::Relaxed);
    if current > 0 {
        REMAINING.compare_exchange(current, current - 1, Ordering::Relaxed, Ordering::Relaxed).ok();
    }
}
