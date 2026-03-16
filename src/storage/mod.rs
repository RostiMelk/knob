//! NVS storage module — persists settings to flash.
//!
//! This is the Rust port of main/storage/settings.cpp.
//! Uses esp_idf_svc::nvs::EspNvs for non-volatile storage.

use esp_idf_svc::nvs::{EspNvs, EspNvsPartition, NvsDefault};
use log::info;
use std::sync::Mutex;

/// Namespace used in NVS for radio settings.
const NVS_NAMESPACE: &str = "radio";

/// Persisted radio settings.
#[derive(Debug, Clone)]
pub struct Settings {
    pub volume: u8,
    pub station_index: u8,
    pub speaker_name: String,
}

impl Default for Settings {
    fn default() -> Self {
        Self {
            volume: 25,
            station_index: 0,
            speaker_name: String::new(),
        }
    }
}

/// Global NVS handle, initialized once.
static NVS: Mutex<Option<EspNvs<NvsDefault>>> = Mutex::new(None);

/// Initialize NVS storage and load persisted settings.
///
/// Must be called once at startup with the default NVS partition.
pub fn init(partition: EspNvsPartition<NvsDefault>) -> anyhow::Result<Settings> {
    let nvs = EspNvs::new(partition, NVS_NAMESPACE, true)?;

    let volume = nvs.get_u8("volume")?.unwrap_or(25);
    let station_index = nvs.get_u8("station")?.unwrap_or(0);

    // Read speaker name (up to 64 bytes)
    let mut buf = [0u8; 64];
    let speaker_name = match nvs.get_str("speaker", &mut buf)? {
        Some(s) => s.trim_end_matches('\0').to_string(),
        None => String::new(),
    };

    let settings = Settings {
        volume,
        station_index,
        speaker_name,
    };

    info!("NVS loaded: vol={}, station={}, speaker={:?}",
        settings.volume, settings.station_index, settings.speaker_name);

    *NVS.lock().unwrap() = Some(nvs);

    Ok(settings)
}

/// Save volume to NVS.
pub fn set_volume(volume: u8) -> anyhow::Result<()> {
    let mut guard = NVS.lock().unwrap();
    if let Some(ref mut nvs) = *guard {
        nvs.set_u8("volume", volume)?;
    }
    Ok(())
}

/// Save station index to NVS.
pub fn set_station(index: u8) -> anyhow::Result<()> {
    let mut guard = NVS.lock().unwrap();
    if let Some(ref mut nvs) = *guard {
        nvs.set_u8("station", index)?;
    }
    Ok(())
}

/// Save speaker name to NVS.
pub fn set_speaker(name: &str) -> anyhow::Result<()> {
    let mut guard = NVS.lock().unwrap();
    if let Some(ref mut nvs) = *guard {
        nvs.set_str("speaker", name)?;
    }
    Ok(())
}

/// Get current settings snapshot (re-reads from NVS).
pub fn get_settings() -> anyhow::Result<Settings> {
    let guard = NVS.lock().unwrap();
    if let Some(ref nvs) = *guard {
        let volume = nvs.get_u8("volume")?.unwrap_or(25);
        let station_index = nvs.get_u8("station")?.unwrap_or(0);
        let mut buf = [0u8; 64];
        let speaker_name = match nvs.get_str("speaker", &mut buf)? {
            Some(s) => s.trim_end_matches('\0').to_string(),
            None => String::new(),
        };
        Ok(Settings { volume, station_index, speaker_name })
    } else {
        Ok(Settings::default())
    }
}
