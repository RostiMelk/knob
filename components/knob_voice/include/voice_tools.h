#pragma once

#include <cstddef>
#include <cstdint>

// ─── Tool Result ────────────────────────────────────────────────────────────

struct ToolResult {
  bool success;
  char output[256];
};

// ─── Tool Definition ────────────────────────────────────────────────────────
//
// Each tool is a static struct with everything needed: name, schema JSON
// fragment, and handler function pointer. Tools self-register into a
// linker-section table so adding a new tool is a single-file change.
//
// Handler signature:
//   bool handler(const char *arguments_json, ToolResult *result);
//   Returns true if the tool name was recognized (even if execution failed).
//   Sets result->success and result->output.

using ToolHandler = bool (*)(const char *arguments, ToolResult *result);

struct ToolDef {
  const char *name;
  const char *description;
  const char *parameters_json; // JSON object string for "parameters" field
  ToolHandler handler;
};

// ─── Tool Registration Macro ────────────────────────────────────────────────
//
// Usage in any .cpp file:
//
//   static bool handle_my_tool(const char *args, ToolResult *r) {
//       r->success = true;
//       snprintf(r->output, sizeof(r->output), "Done.");
//       return true;
//   }
//
//   REGISTER_TOOL(my_tool, "my_tool", "Does something useful.",
//       R"({"type":"object","properties":{"x":{"type":"integer"}},"required":["x"]})",
//       handle_my_tool);
//
// The macro places a ToolDef in a known array. On embedded (ESP-IDF) we use
// a linker section; on desktop/sim we use a static-init table since custom
// linker scripts aren't portable.

#define VOICE_TOOLS_MAX 16

// Central registry — defined in voice_tools.cpp
extern ToolDef g_tool_registry[];
extern int g_tool_count;

// Static-initializer registration helper
struct ToolRegistrar {
  ToolRegistrar(const char *name, const char *desc, const char *params,
                ToolHandler handler) {
    if (g_tool_count < VOICE_TOOLS_MAX) {
      g_tool_registry[g_tool_count++] = {name, desc, params, handler};
    }
  }
};

#define REGISTER_TOOL(id, name_, desc_, params_, handler_)                     \
  static const ToolRegistrar s_reg_##id(name_, desc_, params_, handler_)

// ─── Registry API ───────────────────────────────────────────────────────────

void voice_tools_init();

int voice_tools_count();
const ToolDef *voice_tools_get(int index);
const ToolDef *voice_tools_find(const char *name);

// Build the JSON array for the "tools" field in session.update.
// Writes into buf, returns bytes written (excl. null), or -1 if too small.
int voice_tools_build_json(char *buf, size_t buf_len);

// Execute a tool by name. Returns true if recognized.
bool voice_tools_execute(const char *name, const char *arguments,
                         ToolResult *result);

// ─── JSON Argument Helpers ──────────────────────────────────────────────────
//
// Lightweight extractors for flat JSON objects (no nesting).
// Shared by all tool handlers — avoids duplicating parsers per tool.

bool tool_json_get_int(const char *json, const char *key, int *out);
bool tool_json_get_string(const char *json, const char *key, char *out,
                          size_t out_len);
bool tool_json_get_bool(const char *json, const char *key, bool *out);
