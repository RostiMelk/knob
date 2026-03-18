# Knob Monorepo — ESP32-S3

Multiple ESP32-S3 knobs, each running a different app. Same hardware, different firmware.
Shared components in `components/`, apps in `apps/`. The original product is the Sonos radio (`apps/radio/`).

## Build & Flash

All commands accept an app name. Defaults to `radio` if omitted.

```bash
./test.sh              # build + lint radio (default)
./test.sh homekit      # build + lint a different app
./test.sh --all        # build every app

./flash.sh -m          # flash radio + serial monitor
./flash.sh homekit -m  # flash a different app

cd apps/radio && idf.py build   # build directly from app dir
```

CI runs on every PR (`.github/workflows/build.yml`).

## Critical Constraints

- **RGB565 only.** RGB666 causes an unrecoverable boot loop.
- **No exceptions, no RTTI.** C++20 with `-fno-exceptions -fno-rtti`.
- **LVGL calls only from UI task.** Use `esp_lvgl_port_lock()`/`unlock()` from elsewhere.
- **Modules communicate via `esp_event`.** No direct cross-module calls.
- **Each flash cycle costs ~30s.** Batch fixes. Never push untested display changes.

## Monorepo Layout

- `components/knob_*` — Shared ESP-IDF components (hal, net, storage, ui, voice, sonos, timer)
- `apps/radio/` — Sonos radio app (original product)
- `apps/template/` — Starter for new knob apps
- Hardware pins: `components/knob_hal/include/hal_pins.h`
- Shared events: `components/knob_net/include/knob_events.h`
- App-specific config: each app's `main/app_config.h`

## Adding a New Knob

```bash
cp -r apps/template apps/my_knob
# Edit CMakeLists.txt, write your main.cpp and UI
./test.sh my_knob
./flash.sh my_knob -m
```

## Project Reference

See [`AGENTS.md`](AGENTS.md) for full project context — architecture, conventions, layout, and the skills index for task-specific guidance.
