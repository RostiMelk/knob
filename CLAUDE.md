# Radio — ESP32-S3 Sonos Controller

ESP32-S3 internet radio controlling Sonos speakers via UPnP/SOAP.
Pure C++20 on ESP-IDF v5.4. LVGL v9 UI on ST77916 360×360 round QSPI LCD.

## Build & Test

```bash
./test.sh              # build firmware + lint (run before every push)
./test.sh --sim        # build SDL2 simulator
./flash.sh -m          # build + flash + serial monitor
./flash.sh --build-only # build only
```

CI runs on every PR (`.github/workflows/build.yml`).

## Architecture

```
main/
  app_config.h        Pin defs, constants, station list, event IDs
  ui/                 LVGL screens, display driver (ST77916 QSPI + CST816S touch)
  sonos/              UPnP/SOAP control + SSDP discovery
  voice/              OpenAI Realtime API via WebSocket
  input/              Rotary switch (GPIO polling, NOT quadrature encoder)
  net/                WiFi STA
  storage/            NVS + SPIFFS + SD card config
  timer/              Countdown timer
sim/                  SDL2 desktop simulator
```

Modules communicate via `esp_event`. No direct cross-module calls.
LVGL calls only from UI task — use `display_lock()`/`display_unlock()` from elsewhere.

## Code Style

- C++20, no exceptions, no RTTI. Use `constexpr`, `std::clamp`, RAII.
- Prefix public symbols with module name: `sonos_play_uri()`, `settings_get_volume()`
- Large buffers in PSRAM via `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)`
- ISRs minimal — post to FreeRTOS queues only
- No barrel files. No unnecessary comments.

## ⚠️ CRITICAL — Read Before Any Hardware/Display Changes

**RGB565 ONLY.** esp_lvgl_port DMA buffers only support RGB565.
RGB666 causes an unrecoverable boot loop. Never attempt without alternative DMA approach.

**Draw buffers: 36 rows max** in internal DMA SRAM (52KB double-buffered).
54+ rows causes OOM. ESP32-S3 SPI DMA cannot read from PSRAM.

**swap_bytes=true** required for correct colors. 180° rotation via mirror_x + mirror_y.

**USB-C cable orientation matters.** CH445P switch routes to different chips.
Flip cable for ESP32-S3 on `/dev/cu.usbmodem2101`. No reset button — power slider only.

**Each flash cycle costs ~30s.** Batch fixes. Never push untested display changes to main.

## Kconfig Gotchas

Choice blocks need: explicit unset + `rm sdkconfig` + `idf.py fullclean` + rebuild.
ALWAYS verify with `grep` on built config — choices silently revert.

## Image Pipeline

PNG → `scripts/convert_images.sh` → .bin on SPIFFS. All 24 images pre-cached at boot (~3s).
After boot: station switches <2ms. Logos: 100×100 ARGB8888. Backgrounds: 360×360 RGB565.

## Common Pitfalls

- C++20 designated initializers must be in declaration order (GCC enforces)
- `display_idf.cpp` uses field-by-field assignment to avoid initializer order issues
- sys_evt stack: 8192 min (default 2048 overflows with HTTP+TLS+XML)
- Sonos needs `x-rincon-mp3radio://` URI prefix since firmware v6.4.2+
- Stereo pairs: right channel is Invisible — send commands to coordinator IP
- Font .c files >50KB cannot be pushed via worker tools — use sandbox git
- After squash merge, always do clean build check (PR #6 left main broken)
- Review full PR diffs for stale changes from old branches (PR #7 lesson)

## Detailed Docs

- `docs/hardware.md` — Pin map, display/touch/encoder/audio specs
- `docs/architecture.md` — Module layout, task model, event system
- `docs/sonos-upnp.md` — SOAP envelope format and commands
- `docs/setup.md` — First-time hardware setup
- `AGENTS.md` — Full project reference (conventions, layout, common tasks)
