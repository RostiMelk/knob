//! Voice tool registry — trait-based tool system.
//!
//! Replaces the C++ REGISTER_TOOL macro with a Rust trait.
//! Tools are registered at startup and dispatched by name when
//! the model requests a function call.

pub mod radio;
pub mod timer;

use log::{info, warn};

/// Trait for voice-callable tools.
pub trait VoiceTool: Send + Sync {
    /// Tool name (must match the name in session.update).
    fn name(&self) -> &str;

    /// Human-readable description.
    fn description(&self) -> &str;

    /// JSON Schema for parameters.
    fn parameters_schema(&self) -> serde_json::Value;

    /// Execute the tool with JSON arguments, return JSON result.
    fn execute(&self, args: &str) -> anyhow::Result<String>;
}

/// Registry of all available voice tools.
pub struct ToolRegistry {
    tools: Vec<Box<dyn VoiceTool>>,
}

impl ToolRegistry {
    /// Create a new registry with all built-in tools.
    pub fn new() -> Self {
        let mut registry = Self { tools: Vec::new() };

        // Register built-in tools
        registry.register(Box::new(radio::PlayStationTool));
        registry.register(Box::new(radio::SetVolumeTool));
        registry.register(Box::new(radio::GetNowPlayingTool));
        registry.register(Box::new(timer::SetTimerTool));
        registry.register(Box::new(timer::CancelTimerTool));
        registry.register(Box::new(timer::GetTimerStatusTool));

        info!("Tool registry initialized with {} tools", registry.tools.len());
        registry
    }

    /// Register a new tool.
    pub fn register(&mut self, tool: Box<dyn VoiceTool>) {
        info!("Registered tool: {}", tool.name());
        self.tools.push(tool);
    }

    /// Execute a tool by name with JSON arguments.
    pub fn execute(&self, name: &str, args: &str) -> anyhow::Result<String> {
        for tool in &self.tools {
            if tool.name() == name {
                info!("Executing tool: {} with args: {}", name, args);
                return tool.execute(args);
            }
        }
        warn!("Unknown tool: {}", name);
        anyhow::bail!("Unknown tool: {}", name)
    }

    /// Get tool definitions for the session.update payload.
    pub fn tool_definitions(&self) -> Vec<serde_json::Value> {
        self.tools
            .iter()
            .map(|t| {
                serde_json::json!({
                    "type": "function",
                    "name": t.name(),
                    "description": t.description(),
                    "parameters": t.parameters_schema()
                })
            })
            .collect()
    }
}
