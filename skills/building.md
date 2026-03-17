# Building — Build System, Flash, Kconfig & Image Pipeline

## When to read this

You're building firmware, flashing hardware, modifying Kconfig options, working with the image pipeline, or debugging build issues.

---

## Quick Reference

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

CI runs `idf.py build` on every PR (`.github/workflows/build.yml`).

---

## First-Time Setup

```bash
# 1. Copy the secrets template and fill in your WiFi/API credentials:
cp sdkconfig.defaults.local.template sdkconfig.defaults.local
# Edit sdkconfig.defaults.local with your SSID, password, speaker IP, etc.

# 2. Build and flash:
rm -f sdkconfig && idf.py build && idf.py flash
```

`sdkconfig.defaults.local` is gitignored — never commit real credentials. The build system loads config files in order (last wins):

1. `sdkconfig.defaults` — base config (committed)
2. `sdkconfig.defaults.esp32s3` — target Kconfig overrides (committed)
3. `sdkconfig.defaults.local` — local secrets: WiFi, API keys (gitignored)

If WiFi stops connecting after a clean build (`rm -f sdkconfig`), check that `sdkconfig.defaults.local` exists with valid credentials.

---

## Kconfig Gotchas

Kconfig "choice" blocks need special handling. Choices silently revert if you don't follow this exact sequence:

```bash
# To switch a choice (e.g., LVGL malloc):
# 1. Add explicit unset: # CONFIG_LV_USE_BUILTIN_MALLOC is not set
# 2. Add new choice:     CONFIG_LV_USE_CLIB_MALLOC=y
# 3. Delete sdkconfig:   rm sdkconfig
# 4. Full clean:         idf.py fullclean
# 5. Rebuild:            idf.py build
# 6. VERIFY:             grep LV_USE_CLIB_MALLOC build/config/sdkconfig.h
```

**Always verify Kconfig changes actually took effect** — `grep` the built config. Never trust that the change stuck without checking.

Known Kconfig constraints:

- `CONFIG_LV_USE_CLIB_MALLOC=y` requires explicit `# CONFIG_LV_USE_BUILTIN_MALLOC is not set` + sdkconfig delete + fullclean
- `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y` eats ~38 KB internal SRAM — removed, do not re-enable
- `CONFIG_ESP_SYSTEM_EVENT_TASK_QUEUE_SIZE=64` (default 32 overflows from Sonos HTTP polling)
- sys_evt stack: `8192` bytes minimum — default 2048 causes stack overflow with HTTP+TLS+XML handlers

---

## Image Pipeline

```
stations.json
  → bun scripts/gen_stations.ts
    → downloads logos → assets/logos/*.png (120×120)
    → patches STATIONS[] in main/app_config.h

assets/logos/*.png
  → LVGLImage.py (--cf RGB565A8 --compress LZ4 --premultiply --ofmt C)
    → main/ui/images/*.c (compiled into firmware, ~4 KB each compressed)
    → declared in main/ui/images/images.h
    → referenced by index in s_logos[] array in ui.cpp
```

No SPIFFS, no filesystem, no file I/O for images. They are memory-mapped from flash.

- **Logos**: 120×120 RGB565A8 + LZ4 compressed, embedded as C arrays. Decoded on first use into LVGL cache (1 MB PSRAM).
- **Backgrounds**: solid color from `Station::color` in `app_config.h`, rendered by LVGL at runtime. Two-layer opacity crossfade on station confirm (250ms).
- **Premultiplied alpha**: saves one multiply-per-pixel during blending at draw time.
- **Display format**: `LV_COLOR_FORMAT_RGB565_SWAPPED` set after display init — LVGL renders in byte-swapped RGB565 natively, eliminating the software byte-swap in the flush callback.

### Adding a New Station

1. Add entry to `stations.json` with `id`, `name`, `stream_url`, `logo_url`, `color`
2. Run `bun scripts/gen_stations.ts` — downloads logo, patches `app_config.h`
3. Re-generate C arrays: `./scripts/convert_images.sh`
4. Add the new `.c` file to `main/CMakeLists.txt` SRCS
5. Add `LV_IMAGE_DECLARE(station_id);` to `main/ui/images/images.h`
6. Add `&station_id` to the `s_logos[]` array in `main/ui/ui.cpp` (must match STATIONS[] order)
7. Build and flash: `rm -f sdkconfig && idf.py build && idf.py flash`

To re-download all logos: `bun scripts/gen_stations.ts --force`
To skip patching app_config.h: `bun scripts/gen_stations.ts --no-cpp`

---

## Serial Log Capture

`idf.py monitor` requires an interactive TTY. From an agent or script, capture serial output to `serial.log` instead:

```bash
python3 -c "
import serial, time
s = serial.Serial('/dev/cu.usbmodem2101', 115200, timeout=1)
end = time.time() + 10
with open('serial.log', 'w') as f:
    while time.time() < end:
        data = s.read(s.in_waiting or 1)
        if data:
            f.write(data.decode('utf-8', errors='replace'))
s.close()
"
```

Then read `serial.log` to check for boot errors, OOM crashes, or stack traces. **Always do this after flashing changes that touch memory allocation, Kconfig, or display init.**

---

## Flashing Constraints

- **USB-C cable orientation matters**. CH445P analog switch routes to different chips. Flip cable for ESP32-S3 on `/dev/cu.usbmodem2101`.
- **No reset button** — power slider only. Bad firmware = power cycle + reflash.
- **Each flash cycle is expensive** (~30s). Batch fixes aggressively. Never push changes that crash without a known-good fallback.
- Font `.c` files >50 KB cannot be pushed via worker tools — use sandbox git.

---

## Lessons Learned

- After squash merge, always do a clean build check (PR #6 left main broken)
- Review full PR diffs for stale changes from old branches (PR #7 lesson)
- C++20 designated initializers must be in declaration order (GCC enforces)
- `display_idf.cpp` uses field-by-field assignment to avoid initializer order issues with `esp_lvgl_port` structs

---

> **Keep this alive:** If you change the build pipeline, add scripts, or discover new Kconfig gotchas — update this file in the same PR.
