# AGENTS.md

## Project

ESP-IDF C++20 firmware for a Sonos radio controller on the Waveshare ESP32-S3-Knob-Touch-LCD-1.8.

## Read First

1. `docs/hardware.md` — Pin map, display/touch/encoder/audio specs
2. `docs/architecture.md` — Module layout, task model, event system
3. `docs/sonos-upnp.md` — Sonos UPnP SOAP commands we implement
4. `docs/setup.md` — Hardware setup, build, flash, first boot

## Stack

- **MCU**: ESP32-S3R8 (8MB PSRAM, 16MB flash)
- **Framework**: ESP-IDF v5.4+ (C++20, `-fno-exceptions -fno-rtti`)
- **UI**: LVGL v9 via `esp_lvgl_port`
- **Display**: ST77916 360×360 QSPI LCD
- **Touch**: CST816 I2C
- **Build**: `idf.py build` / `idf.py flash monitor`

## Project Layout

```
main/
  main.cpp            Entry point, task creation, event loop setup
  app_config.h        Pin definitions, constants, station list
  net/                WiFi STA, reconnect logic
  sonos/              UPnP/SOAP control (AVTransport, RenderingControl)
  input/              Rotary encoder via PCNT, button via GPIO ISR
  storage/            NVS wrapper for persistent settings
  ui/                 LVGL screens and state machine
docs/                 Hardware specs, architecture, protocol refs
```

## Conventions

- **Language**: C++20 with ESP-IDF. No exceptions, no RTTI. Use `constexpr`, `std::clamp`, `std::string_view`, RAII where it helps. All files are `.cpp`/`.h`.
- Modules communicate via `esp_event`. No direct cross-module calls except through public headers.
- LVGL calls only from the UI task. Use `lvgl_port_lock`/`unlock` if touching LVGL from elsewhere.
- Large buffers go in PSRAM via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`.
- Keep ISRs minimal — post to FreeRTOS queues, not direct logic.
- No barrel files. No unnecessary comments. Self-documenting code.
- Prefix public symbols with module name: `wifi_manager_init()`, `sonos_play_uri()`, `settings_get_volume()`.

## Common Tasks

### Add a new module

1. Create `main/<module>/` with `.h` and `.cpp`
2. Add source to `main/CMakeLists.txt` SRCS
3. Expose init function, call from `main.cpp`
4. Use `esp_event_post()` to emit state changes

### Change pin assignments

Edit `main/app_config.h`. All hardware pins are `constexpr int` there. Cross-ref with `docs/hardware.md`.

### Add an LVGL screen

Add to `main/ui/`. Each screen is a function that builds an `lv_obj_t*`. Register in the UI state machine in `ui.cpp`.

### Modify Sonos commands

See `docs/sonos-upnp.md` for SOAP envelope templates. Implementation in `main/sonos/sonos.cpp`.

## Simulator (no hardware needed)

Runs the LVGL UI natively on macOS/Linux via SDL2. Requires `sdl2` and `cmake`.

```
brew install sdl2                    # macOS, one-time
cmake -B build -S sim               # configure
cmake --build build -j$(nproc)      # build
pkill -f "./build/sim" 2>/dev/null; sleep 0.3; ./build/sim &   # run
```

**Important:** Always launch from the project root (`radio/`). The simulator resolves asset paths (`A:assets/logos/...`) relative to the working directory. Using `open` or launching from `build/` will break image loading.

| Key         | Action              |
| ----------- | ------------------- |
| Up/Down     | Volume +/-          |
| Enter/Space | Toggle screen       |
| Mouse click | Simulates touch tap |
| Scroll      | Simulates encoder   |
| Q / Esc     | Quit                |

Simulator code lives in `sim/`. UI screens are duplicated from `main/ui/` — keep them in sync. The `sim/shims/` directory provides no-op ESP-IDF headers (`esp_event.h`, `esp_log.h`) so `app_config.h` compiles on desktop.

## Build & Flash (hardware)

Use `flash.sh` — it handles everything automatically. Raw `idf.py` commands are listed at the bottom as a fallback.

### flash.sh

`flash.sh` is a build/flash/monitor helper that:

1. **Checks prerequisites** — verifies ESP-IDF, the Rust ESP toolchain, and `ldproxy` are installed
2. **Sources ESP-IDF** — runs `export.sh` automatically if `$IDF_PATH` isn't already set
3. **Detects the correct USB port** — probes connected serial ports with `esptool.py` to find the ESP32-S3
4. **Builds** — runs `idf.py build` (sets target to `esp32s3` on first run)
5. **Flashes** — runs `idf.py flash` on the detected port
6. **Monitors** (optional) — opens the serial monitor via `idf.py monitor`

#### First-time setup

GitHub does not preserve file permissions. After cloning, make the script executable:

```
chmod +x flash.sh
```

#### Usage

```
./flash.sh                        # build + flash
./flash.sh --monitor              # build + flash + open serial monitor
./flash.sh -m                     # same as --monitor
./flash.sh --build-only           # build only, no flash
./flash.sh --log serial.log       # build + flash + monitor, save output to file (implies --monitor)
./flash.sh --help                 # show usage
```

#### Exiting the monitor

Press **Ctrl+T** then **Ctrl+X**.

#### USB-C cable orientation quirk

This board has a single USB-C port connected to a CH445P analog switch. The cable orientation determines which chip is exposed:

- **One way** → ESP32-S3 (main MCU — correct for flashing)
- **Flipped** → ESP32 co-processor (wrong chip)

`flash.sh` detects this automatically. If it finds the co-processor instead of the ESP32-S3, it will print a warning and tell you to flip the cable.

### Raw idf.py commands (fallback)

```
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Phase 1 Scope (current)

WiFi → discover/control Sonos via UPnP → LVGL UI with station list + now playing + volume → rotary encoder input.

## Phase 2 (future, not yet)

Mic capture → local backend service → OpenAI voice → TTS audio hosted as URL → Sonos plays it → resume radio. Device stays a thin client.
