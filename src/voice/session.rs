//! OpenAI Realtime API session management.
//!
//! Builds `session.update` payloads and manages session lifecycle.
//! The session config includes model selection, voice, tools, and
//! turn detection settings.

use serde::Serialize;

/// Session configuration for the OpenAI Realtime API.
#[derive(Debug, Clone, Serialize)]
pub struct SessionConfig {
    pub model: String,
    pub voice: String,
    pub instructions: String,
    pub tools: Vec<ToolDefinition>,
    pub turn_detection: TurnDetection,
    pub input_audio_format: String,
    pub output_audio_format: String,
}

/// Tool definition for the session.update payload.
#[derive(Debug, Clone, Serialize)]
pub struct ToolDefinition {
    pub r#type: String,
    pub name: String,
    pub description: String,
    pub parameters: serde_json::Value,
}

/// Server-side VAD turn detection config.
#[derive(Debug, Clone, Serialize)]
pub struct TurnDetection {
    pub r#type: String,
    pub threshold: f32,
    pub prefix_padding_ms: u32,
    pub silence_duration_ms: u32,
}

impl Default for SessionConfig {
    fn default() -> Self {
        Self {
            model: "gpt-4o-realtime-preview".to_string(),
            voice: "verse".to_string(),
            instructions: "You are a helpful radio assistant. \
                You can control music playback, set timers, and answer questions. \
                Keep responses brief and conversational.".to_string(),
            tools: Vec::new(),
            turn_detection: TurnDetection {
                r#type: "server_vad".to_string(),
                threshold: 0.5,
                prefix_padding_ms: 300,
                silence_duration_ms: 500,
            },
            input_audio_format: "pcm16".to_string(),
            output_audio_format: "pcm16".to_string(),
        }
    }
}

impl SessionConfig {
    /// Build the session config with all registered tools.
    pub fn with_tools(mut self) -> Self {
        // Radio tools
        self.tools.push(ToolDefinition {
            r#type: "function".to_string(),
            name: "play_station".to_string(),
            description: "Play a radio station by name or index".to_string(),
            parameters: serde_json::json!({
                "type": "object",
                "properties": {
                    "station": {
                        "type": "string",
                        "description": "Station name or index number"
                    }
                },
                "required": ["station"]
            }),
        });

        self.tools.push(ToolDefinition {
            r#type: "function".to_string(),
            name: "set_volume".to_string(),
            description: "Set the playback volume (0-100)".to_string(),
            parameters: serde_json::json!({
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
            }),
        });

        // Timer tools
        self.tools.push(ToolDefinition {
            r#type: "function".to_string(),
            name: "set_timer".to_string(),
            description: "Set a countdown timer".to_string(),
            parameters: serde_json::json!({
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
            }),
        });

        self.tools.push(ToolDefinition {
            r#type: "function".to_string(),
            name: "cancel_timer".to_string(),
            description: "Cancel the active timer".to_string(),
            parameters: serde_json::json!({
                "type": "object",
                "properties": {}
            }),
        });

        self
    }

    /// Serialize to the session.update event payload.
    pub fn to_session_update(&self) -> serde_json::Value {
        serde_json::json!({
            "type": "session.update",
            "session": {
                "model": self.model,
                "voice": self.voice,
                "instructions": self.instructions,
                "tools": self.tools,
                "turn_detection": self.turn_detection,
                "input_audio_format": self.input_audio_format,
                "output_audio_format": self.output_audio_format,
            }
        })
    }
}
