//! Voice tool registry — trait-based tool system.
//!
//! Replaces the C++ REGISTER_TOOL macro with a Rust trait:
//! ```
//! pub trait VoiceTool: Send + Sync {
//!     fn name(&self) -> &str;
//!     fn description(&self) -> &str;
//!     fn parameters_schema(&self) -> &str;
//!     fn execute(&self, args: &str) -> anyhow::Result<String>;
//! }
//! ```

pub mod radio;
pub mod timer;
