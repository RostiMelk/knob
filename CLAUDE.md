# Radio — ESP32-S3 Sonos Controller

ESP32-S3 internet radio controlling Sonos speakers via UPnP/SOAP.
Pure C++20 on ESP-IDF v5.4. LVGL v9 UI on ST77916 360×360 round QSPI LCD.

## Build & Test

```bash
./test.sh              # build firmware + lint (run before every push)
./test.sh --sim        # build SDL2 simulator
./flash.sh -m          # build + flash + serial monitor
```

CI runs on every PR (`.github/workflows/build.yml`).

## Critical Constraints

- **RGB565 only.** RGB666 causes an unrecoverable boot loop.
- **No exceptions, no RTTI.** C++20 with `-fno-exceptions -fno-rtti`.
- **LVGL calls only from UI task.** Use `esp_lvgl_port_lock()`/`unlock()` from elsewhere.
- **Modules communicate via `esp_event`.** No direct cross-module calls.
- **Each flash cycle costs ~30s.** Batch fixes. Never push untested display changes.

## Project Reference

See [`AGENTS.md`](AGENTS.md) for full project context — architecture, conventions, layout, and the skills index for task-specific guidance.
