//! Timer voice tools: set_timer, cancel_timer, get_timer_status.
//!
//! These tools bridge voice commands to the `crate::timer` module.

use super::VoiceTool;
use log::info;

// ---------------------------------------------------------------------------
// set_timer
// ---------------------------------------------------------------------------

pub struct SetTimerTool;

impl VoiceTool for SetTimerTool {
    fn name(&self) -> &str {
        "set_timer"
    }

    fn description(&self) -> &str {
        "Set a countdown timer"
    }

    fn parameters_schema(&self) -> serde_json::Value {
        serde_json::json!({
            "type": "object",
            "properties": {
                "seconds": {
                    "type": "integer",
                    "minimum": 1,
                    "description": "Timer duration in seconds"
                },
                "label": {
                    "type": "string",
                    "description": "Optional label for the timer"
                }
            },
            "required": ["seconds"]
        })
    }

    fn execute(&self, args: &str) -> anyhow::Result<String> {
        let parsed: serde_json::Value = serde_json::from_str(args)?;
        let seconds = parsed["seconds"].as_u64().unwrap_or(60) as u32;
        let label = parsed["label"].as_str().unwrap_or("Timer");

        info!("Setting timer: {}s ({})", seconds, label);

        crate::timer::start(seconds, label);

        Ok(serde_json::json!({
            "status": "started",
            "seconds": seconds,
            "label": label
        }).to_string())
    }
}

// ---------------------------------------------------------------------------
// cancel_timer
// ---------------------------------------------------------------------------

pub struct CancelTimerTool;

impl VoiceTool for CancelTimerTool {
    fn name(&self) -> &str {
        "cancel_timer"
    }

    fn description(&self) -> &str {
        "Cancel the active timer"
    }

    fn parameters_schema(&self) -> serde_json::Value {
        serde_json::json!({
            "type": "object",
            "properties": {}
        })
    }

    fn execute(&self, _args: &str) -> anyhow::Result<String> {
        info!("Cancelling timer");

        let was_active = crate::timer::is_active();
        crate::timer::cancel();

        Ok(serde_json::json!({
            "status": if was_active { "cancelled" } else { "no_timer" },
        }).to_string())
    }
}

// ---------------------------------------------------------------------------
// get_timer_status
// ---------------------------------------------------------------------------

pub struct GetTimerStatusTool;

impl VoiceTool for GetTimerStatusTool {
    fn name(&self) -> &str {
        "get_timer_status"
    }

    fn description(&self) -> &str {
        "Get the status of the active timer"
    }

    fn parameters_schema(&self) -> serde_json::Value {
        serde_json::json!({
            "type": "object",
            "properties": {}
        })
    }

    fn execute(&self, _args: &str) -> anyhow::Result<String> {
        let active = crate::timer::is_active();
        let remaining = crate::timer::remaining();
        let label = crate::timer::label();

        Ok(serde_json::json!({
            "active": active,
            "remaining_seconds": remaining,
            "label": label
        }).to_string())
    }
}
