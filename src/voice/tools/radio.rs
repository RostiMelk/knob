//! Radio-specific voice tools: play_station, set_volume, get_now_playing.
//!
//! These tools bridge voice commands to the Sonos playback system
//! via `crate::bridge::net`.

use super::VoiceTool;
use log::info;

// ---------------------------------------------------------------------------
// play_station
// ---------------------------------------------------------------------------

pub struct PlayStationTool;

impl VoiceTool for PlayStationTool {
    fn name(&self) -> &str {
        "play_station"
    }

    fn description(&self) -> &str {
        "Play a radio station by name or index"
    }

    fn parameters_schema(&self) -> serde_json::Value {
        serde_json::json!({
            "type": "object",
            "properties": {
                "station": {
                    "type": "string",
                    "description": "Station name or index number"
                }
            },
            "required": ["station"]
        })
    }

    fn execute(&self, args: &str) -> anyhow::Result<String> {
        let parsed: serde_json::Value = serde_json::from_str(args)?;
        let station = parsed["station"].as_str().unwrap_or("0");

        info!("Playing station: {}", station);

        // TODO: Look up station URI from preset list.
        // For now, try to parse as index and use a placeholder URI.
        // The actual station list will come from storage/config.
        let station_uri = match station.parse::<u8>() {
            Ok(idx) => {
                // TODO: Map index to actual stream URI from station presets
                info!("Station index {} requested — URI lookup not yet implemented", idx);
                format!("x-rincon-mp3radio://placeholder-station-{}", idx)
            }
            Err(_) => {
                // TODO: Fuzzy match station name against preset list
                info!("Station name '{}' requested — name lookup not yet implemented", station);
                format!("x-rincon-mp3radio://placeholder-{}", station.to_lowercase().replace(' ', "-"))
            }
        };

        // Play via the Sonos bridge (uses sonos_play_uri from C++)
        crate::bridge::net::play_uri(&station_uri);

        Ok(serde_json::json!({
            "status": "playing",
            "station": station
        }).to_string())
    }
}

// ---------------------------------------------------------------------------
// set_volume
// ---------------------------------------------------------------------------

pub struct SetVolumeTool;

impl VoiceTool for SetVolumeTool {
    fn name(&self) -> &str {
        "set_volume"
    }

    fn description(&self) -> &str {
        "Set the playback volume (0-100)"
    }

    fn parameters_schema(&self) -> serde_json::Value {
        serde_json::json!({
            "type": "object",
            "properties": {
                "level": {
                    "type": "integer",
                    "minimum": 0,
                    "maximum": 100,
                    "description": "Volume level 0-100"
                }
            },
            "required": ["level"]
        })
    }

    fn execute(&self, args: &str) -> anyhow::Result<String> {
        let parsed: serde_json::Value = serde_json::from_str(args)?;
        let level = parsed["level"].as_i64().unwrap_or(50) as i32;
        let level = level.clamp(0, 100);

        info!("Setting volume to {}", level);

        // Set volume via the Sonos bridge (uses sonos_set_volume(i32) from C++)
        crate::bridge::net::set_volume(level);

        // Also update the UI
        crate::bridge::ui::set_volume(level as u8);

        // Persist to NVS
        if let Err(e) = crate::storage::set_volume(level as u8) {
            log::warn!("Failed to persist volume: {}", e);
        }

        Ok(serde_json::json!({
            "status": "ok",
            "volume": level
        }).to_string())
    }
}

// ---------------------------------------------------------------------------
// get_now_playing
// ---------------------------------------------------------------------------

pub struct GetNowPlayingTool;

impl VoiceTool for GetNowPlayingTool {
    fn name(&self) -> &str {
        "get_now_playing"
    }

    fn description(&self) -> &str {
        "Get information about what's currently playing"
    }

    fn parameters_schema(&self) -> serde_json::Value {
        serde_json::json!({
            "type": "object",
            "properties": {}
        })
    }

    fn execute(&self, _args: &str) -> anyhow::Result<String> {
        // TODO: Query current playback state from Sonos
        // For now return a placeholder
        info!("Getting now playing info");

        Ok(serde_json::json!({
            "status": "unknown",
            "message": "Now playing info not yet implemented — requires Sonos state query"
        }).to_string())
    }
}
