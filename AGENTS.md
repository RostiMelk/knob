# AGENTS.md

## Project

ESP32-S3 Sonos internet radio controller. Pure C++20 on ESP-IDF v5.4.
Custom PCB with ST77916 360×360 round QSPI LCD, CST816S touch, bidirectional rotary switch, I2S mic/DAC, internal SD card.

## Read First

1. `docs/hardware.md` — Pin map, display/touch/encoder/audio specs
2. `docs/architecture.md` — Module layout, task model, event system
3. `docs/sonos-upnp.md` — Sonos UPnP SOAP commands
4. `docs/setup.md` — Hardware setup, build, flash, first boot

## Stack

| Component | Detail |
|-----------|--------|
| MCU | ESP32-S3R8 (8 MB PSRAM, 16 MB flash) |
| Framework | ESP-IDF v5.4+ (C++20, `-fno-exceptions -fno-rtti`) |
| UI | LVGL v9 via `esp_lvgl_port` |
| Display | ST77916 360×360 QSPI @ 50 MHz, RGB565, swap_bytes=true |
| Touch | CST816S I2C @ 400 kHz |
| Input | Bidirectional rotary switch (NOT quadrature encoder — pin A=CW, pin B=CCW, GPIO polling with debounce) |
| Audio | I2S DAC (playback), PDM mic (voice capture) |
| Build | `idf.py build` / `./flash.sh` / `./test.sh` |

## Project Layout

```
main/
  main.cpp              Entry point, event registration, task creation
  app_config.h          Pin defs, constants, station list, event IDs
  fonts/                LVGL font .c files (generated via lv_font_conv)
  input/
    encoder.cpp         Rotary switch — GPIO polling, software debounce
    haptic.cpp          DRV2605 haptic feedback via I2C
  net/
    wifi_manager.cpp    STA connect, reconnect, event emission
  sonos/
    sonos.cpp           UPnP/SOAP HTTP client (AVTransport + RenderingControl)
    discovery.cpp       SSDP multicast speaker discovery
  storage/
    settings.cpp        NVS persistence + SPIFFS mount + SD card config load
  timer/
    timer.cpp           Countdown timer with arc animation
  ui/
    ui.cpp              Home screen, mode state machine (Volume/Browse/Voice)
    ui_pages.cpp        Horizontal pager (home ↔ timer, slide animations)
    ui_timer.cpp        Timer page UI
    ui_voice.cpp        Voice mode overlay UI
    display_idf.cpp     ST77916 QSPI + CST816S init, LVGL port setup
  voice/
    voice_task.cpp      WebSocket → OpenAI Realtime API
    voice_audio.cpp     PDM mic → ring buffer → base64 encoding
    voice_tools.cpp     Tool-call handling from voice assistant
docs/                   Hardware specs, architecture, protocol refs
scripts/
  convert_images.sh     PNG → .bin (LVGLImage.py) for station logos
  gen_stations.ts       Generate station list from stations.json
  lint.sh               clang-tidy on all main/ sources
sim/                    SDL2 desktop simulator (macOS/Linux)
assets/                 Station logos (PNG source) and converted .bin files
```

## Conventions

- **Language**: C++20 with ESP-IDF. No exceptions, no RTTI. Use `constexpr`, `std::clamp`, `std::string_view`, RAII.
- **Modules communicate via `esp_event`**. No direct cross-module calls except through public headers.
- **LVGL calls only from the UI task**. Use `display_lock()`/`display_unlock()` if touching LVGL from elsewhere.
- **Large buffers go in PSRAM** via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`.
- **ISRs are minimal** — post to FreeRTOS queues, no direct logic.
- **Naming**: prefix public symbols with module name: `wifi_manager_init()`, `sonos_play_uri()`, `settings_get_volume()`.
- **No barrel files. No unnecessary comments. Self-documenting code.**

## Build & Test

```bash
# Local build check (recommended before every push):
./test.sh              # build firmware + lint
./test.sh --sim        # build SDL2 simulator
./test.sh --all        # build both

# Flash to hardware:
./flash.sh             # build + flash
./flash.sh -m          # build + flash + serial monitor
./flash.sh --build-only  # build only, no flash

# Lint only (requires prior build for compile_commands.json):
./scripts/lint.sh
```

CI runs `idf.py build` on every PR via GitHub Actions (`.github/workflows/build.yml`).

## Simulator

```bash
brew install sdl2                          # macOS
cmake -B build -S sim && cmake --build build
./build/sim                                # MUST launch from project root
```

**Important**: Always launch from the project root (`radio/`). Asset paths are relative to CWD.

## ⚠️ Critical Hardware Constraints

These have caused boot loops and hours of debugging. **Do not violate them.**

### Display

- **RGB565 ONLY**. `esp_lvgl_port` DMA buffers only support RGB565. RGB666 causes a boot loop. Never attempt RGB666 without an alternative DMA approach.
- **Draw buffer max: 36 rows** (52 KB double-buffered in internal DMA SRAM). 54+ rows causes OOM. The 36-row limit means 10 SPI flushes per frame — this is a known performance issue (visible top-down scanline wipe).
- **ESP32-S3 SPI DMA cannot read from PSRAM**. Setting `buff_spiram=true` falls back to CPU memcpy. Larger PSRAM buffers reduce visible banding but may not improve throughput.
- **swap_bytes=true** is required for correct colors on this panel.
- **180° rotation** via `mirror_x=true, mirror_y=true` (not `sw_rotate`).
- **Field-by-field assignment** in `display_idf.cpp` — C++20 designated initializers cause order mismatch with `esp_lvgl_port` structs.

### Memory

- **`CONFIG_LV_USE_CLIB_MALLOC=y`** requires explicit `# CONFIG_LV_USE_BUILTIN_MALLOC is not set` + sdkconfig delete + fullclean. Kconfig "choice" blocks are fragile.
- **`CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y`** eats ~38 KB internal SRAM. Removed — do not re-enable.
- **Image cache**: 4.2 MB for 24 images. Cache misses cause 120ms+ SPIFFS reads.
- **sys_evt stack**: 8192 bytes minimum. Default 2048 causes stack overflow with HTTP+TLS+XML handlers.

### Rotary Input

- **NOT a quadrature encoder**. Pin A pulses on CW rotation, pin B pulses on CCW. Uses GPIO polling with software debounce, not PCNT hardware decoder.

### USB / Flashing

- **USB-C cable orientation matters**. CH445P analog switch routes to different chips. Flip cable for ESP32-S3 on `/dev/cu.usbmodem2101`.
- **No reset button** — power slider only. Bad firmware = power cycle + reflash.
- **Each flash cycle is expensive** (~30s). Batch fixes aggressively. Never push changes that crash without a known-good fallback.

### Sonos

- **`x-rincon-mp3radio://` URI prefix** required since Sonos firmware v6.4.2+.
- **Stereo pairs**: right channel has `Invisible='1'`. Must send commands to coordinator IP.
- **HTTP event queue**: `CONFIG_ESP_SYSTEM_EVENT_TASK_QUEUE_SIZE=64` (default 32 overflows from Sonos HTTP polling).

## Kconfig Gotchas

Kconfig "choice" blocks need special handling:

```bash
# To switch a choice (e.g., LVGL malloc):
# 1. Add explicit unset: # CONFIG_LV_USE_BUILTIN_MALLOC is not set
# 2. Add new choice:     CONFIG_LV_USE_CLIB_MALLOC=y
# 3. Delete sdkconfig:   rm sdkconfig
# 4. Full clean:         idf.py fullclean
# 5. Rebuild:            idf.py build
# 6. VERIFY:             grep LV_USE_CLIB_MALLOC build/config/sdkconfig.h
```

Always verify Kconfig changes actually took effect — `grep` the built config.

## Image Pipeline

```
PNG (assets/logos/) → scripts/convert_images.sh → .bin (SPIFFS partition)
                      (uses LVGLImage.py)
Boot: all 24 images pre-loaded into LVGL cache (~3s)
After boot: station switches are <2ms (cache hit)
```

- Logos: 100×100 ARGB8888 (~40 KB each)
- Backgrounds: 360×360 RGB565 (~259 KB each)
- SPIFFS mounted at `/spiffs/`, logos at `A:/spiffs/*.bin`

## Common Tasks

### Add a new station
1. Add entry to `stations.json`
2. Add logo PNG to `assets/logos/` and background to `assets/logos/bg/`
3. Run `./scripts/convert_images.sh` to generate .bin files
4. Update `STATION_COUNT` and `STATIONS[]` in `main/app_config.h`

### Add a new LVGL screen/page
1. Create `main/ui/ui_mypage.cpp` + `.h`
2. Define a `PageDef` with build/destroy/tick callbacks
3. Register with `pages_add(&def, priority)` — priority determines position relative to home
4. Pages auto-slide via the pager system in `ui_pages.cpp`

### Change pin assignments
1. Edit `main/app_config.h`
2. Verify against `docs/hardware.md`
3. Test on hardware — pin conflicts cause silent failures or boot loops

### Modify Sonos commands
1. See `docs/sonos-upnp.md` for SOAP envelope format
2. Edit `main/sonos/sonos.cpp`
3. Test with `curl` from your machine first to isolate firmware vs service issues
