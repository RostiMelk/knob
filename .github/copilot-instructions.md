# GitHub Copilot Instructions

See [CLAUDE.md](../CLAUDE.md) for quick reference and [AGENTS.md](../AGENTS.md) for full project context including the skills index.

This is an ESP32-S3 Sonos radio controller. Pure C++20 on ESP-IDF v5.4, LVGL v9 UI.

Key constraints:

- RGB565 only (RGB666 causes boot loop)
- No exceptions, no RTTI
- LVGL calls only from UI task (use display_lock/unlock)
- Modules communicate via esp_event, not direct calls
- Test with `./test.sh` before pushing

For task-specific guidance, see `skills/`:

- `skills/hardware.md` — Display, memory, pin, flashing constraints
- `skills/ui.md` — LVGL screens, pages, images, fonts
- `skills/sonos.md` — UPnP/SOAP, discovery, speaker control
- `skills/building.md` — Build system, Kconfig, image pipeline
- `skills/voice.md` — Voice mode, OpenAI Realtime API
