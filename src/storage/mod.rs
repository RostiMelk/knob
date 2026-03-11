//! NVS storage module — persists settings to flash.
//!
//! This is the Rust port of main/storage/settings.cpp.

use log::info;

pub fn init() -> anyhow::Result<()> {
    // TODO: Initialize NVS and load settings
    // Will use esp_idf_svc::nvs::EspNvs
    info!("Storage initialized (placeholder)");
    Ok(())
}
