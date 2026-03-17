# GitHub Copilot Instructions

See [CLAUDE.md](../CLAUDE.md) and [AGENTS.md](../AGENTS.md) for full project context.

This is an ESP32-S3 Sonos radio controller. Pure C++20 on ESP-IDF v5.4, LVGL v9 UI.

Key constraints:
- RGB565 only (RGB666 causes boot loop)
- No exceptions, no RTTI
- LVGL calls only from UI task (use display_lock/unlock)
- Modules communicate via esp_event, not direct calls
- Test with `./test.sh` before pushing
