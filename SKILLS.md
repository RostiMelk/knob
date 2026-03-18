# SKILLS.md

Hard-won knowledge, patterns, and gotchas learned while building this project. Read this before making changes.

> **Monorepo note:** This project uses `components/` for shared code and `apps/` for per-knob firmware. Paths below reference the new structure. See [README.md](README.md) for the full layout.

## Image Pipeline

### Adding or updating station logos

1. Edit `apps/radio/stations.json` with the new station entry
2. Run `bun scripts/gen_stations.ts` to download the logo and patch `apps/radio/main/app_config.h`
3. Re-generate C arrays from the downloaded PNGs:
   ```
   python3 apps/radio/managed_components/lvgl__lvgl/scripts/LVGLImage.py \
     --cf RGB565A8 --compress LZ4 --premultiply --ofmt C \
     -o apps/radio/main/ui/images apps/radio/assets/logos/
   rm -f apps/radio/main/ui/images/*_bg*
   ```
4. Add the new `.c` file to `apps/radio/main/CMakeLists.txt` SRCS
5. Add `LV_IMAGE_DECLARE(station_id);` to `apps/radio/main/ui/images/images.h`
6. Add `&station_id` to the `s_logos[]` array in `apps/radio/main/ui/ui.cpp` — order must match `STATIONS[]`
7. Build and flash: `./test.sh radio && ./flash.sh radio -m`

### Why C arrays instead of SPIFFS

Images are compiled into firmware as `const` data placed in DROM (flash read-only segment). The ESP32-S3 MMU memory-maps this region, so reads are effectively RAM-speed (~100 MB/s cached). This eliminates:

- SPIFFS mount at boot (~50ms)
- File I/O per image (~5-10ms per 40KB read from SPIFFS)
- The 3-second boot pre-warm loop that loaded all images into cache
- 9MB SPIFFS partition (flash time went from ~60s to ~15s)

### Image format choices

- **RGB565A8 + LZ4**: 12 logos compress to ~48KB total (vs 480KB uncompressed ARGB8888). The display is RGB565, so ARGB8888 gets converted at draw time anyway — RGB565A8 skips that conversion.
- **Premultiplied alpha** (`--premultiply`): pre-calculates `R*A, G*A, B*A` at build time, saving a multiply per pixel during alpha blending.
- **LZ4 decompression**: ~500 MB/s on ESP32-S3. A 4KB compressed logo decompresses in microseconds. Requires `CONFIG_LV_BIN_DECODER_RAM_LOAD=y` and `CONFIG_LV_USE_LZ4=y` in sdkconfig.

### Backgrounds are solid colors, not images

Station backgrounds are rendered as a solid `lv_obj_t` with `bg_color` set to `Station::color`. A black overlay (`s_bg_dim`) provides edge darkening. Color transitions animate smoothly between stations using a custom LVGL animation. This replaced per-station 360x360 RGB565 background images (~259KB each, ~2.85MB total).

### Generated C files need LV_LVGL_H_INCLUDE_SIMPLE

LVGLImage.py generates files with `#ifdef LV_LVGL_H_INCLUDE_SIMPLE` to pick the right include path. ESP-IDF's LVGL port expects `#include "lvgl.h"` (simple form), so each app's `main/CMakeLists.txt` and `components/knob_ui/CMakeLists.txt` define `LV_LVGL_H_INCLUDE_SIMPLE`.

## Kconfig

### Choice blocks are fragile

Kconfig "choice" blocks (like LVGL malloc provider) need explicit handling:

1. Add explicit unset: `# CONFIG_LV_USE_BUILTIN_MALLOC is not set`
2. Add new choice: `CONFIG_LV_USE_CLIB_MALLOC=y`
3. Delete sdkconfig: `rm sdkconfig`
4. Full clean: `idf.py fullclean`
5. Rebuild and **verify**: `grep LV_USE_CLIB_MALLOC build/config/sdkconfig.h`

Duplicate critical Kconfig choices in `sdkconfig.defaults.esp32s3` — the target-specific file merges on top and survives component manager resets.

### Always verify Kconfig changes took effect

```
grep YOUR_CONFIG_KEY apps/<app>/build/config/sdkconfig.h
```

The sdkconfig file is generated, not authoritative. Only `apps/<app>/build/config/sdkconfig.h` shows what the compiler actually sees.

## Display & LVGL

Display driver lives in `components/knob_hal/src/display_idf.cpp`. Pin definitions in `components/knob_hal/include/hal_pins.h`.

### RGB565 only — RGB666 causes boot loop

`esp_lvgl_port` DMA buffers only support RGB565. Never attempt RGB666.

### Draw buffer: 36 rows max

52KB double-buffered in internal DMA SRAM. 54+ rows causes OOM. This means 10 SPI flushes per frame (visible top-down scanline wipe on fast animations).

### ESP32-S3 SPI DMA cannot read from PSRAM

`buff_spiram=true` falls back to CPU memcpy. Larger PSRAM buffers reduce visible banding but don't improve DMA throughput.

### LVGL image cache is byte-based (not entry count)

`CONFIG_LV_CACHE_DEF_SIZE` is the max bytes of decoded pixel buffers in cache. 12 logos at ~30KB RGB565A8 = ~360KB. Cache set to 512KB with headroom. The separate `LV_IMAGE_HEADER_CACHE_DEF_CNT` caches just 12-byte headers.

## Rotary Input

**Not a quadrature encoder.** Pin A pulses on CW rotation, pin B pulses on CCW. Uses GPIO polling with software debounce, not PCNT hardware decoder.

## Sonos

### URI prefix required

`x-rincon-mp3radio://` prefix required since Sonos firmware v6.4.2+.

### Stereo pairs

Right channel has `Invisible='1'`. Must send commands to coordinator IP.

## Flashing

### USB-C cable orientation matters

CH445P analog switch routes to different chips depending on cable orientation. Flip cable for ESP32-S3 on `/dev/cu.usbmodem2101`.

### No reset button

Power slider only. Bad firmware = power cycle + reflash. Each flash cycle is ~15s. Batch fixes aggressively.

### Clean build after sdkconfig changes

```
cd apps/<app> && rm -f sdkconfig && idf.py fullclean && idf.py build
```

Incremental builds after sdkconfig.defaults changes will silently use stale config.

### Reading serial logs (no interactive terminal needed)

The `idf.py monitor` command requires an interactive TTY. When running from an agent or non-interactive shell, capture serial output to a file instead:

```
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

Then read `serial.log` to check for boot errors, crashes, or stack traces. Always do this after flashing changes that touch memory allocation, Kconfig, or display init.

## DMA Buffer Sizing

### The 36-row limit is hard

DMA draw buffers MUST be in internal SRAM (ESP32-S3 SPI DMA cannot read from PSRAM). The double-buffered 36-row config uses ~52KB of internal SRAM. This is the maximum that reliably fits alongside WiFi buffers (~40-50KB), task stacks, and heap.

**Tested and failed:**

- 45 rows (65KB) — OOM on buf2 allocation
- 60 rows (86KB) — OOM on buf2 allocation

The `esp_psram` driver reserves 32KB of internal memory at boot for DMA/internal allocations, further reducing available SRAM. The `esp_lvgl_port` error message is: `Not enough memory for LVGL buffer (buf2) allocation!`

### Soft shadows are not possible with LVGL 9 on this hardware

Tried every approach — none produced a soft iOS-style shadow:

- **`lv_obj_set_style_shadow_width`**: LVGL's built-in box shadow renders as a solid rounded rect with falloff, not a Gaussian blur. Looks like a thick stroke with opacity, not a soft shadow. Tested with various `shadow_width` (20-48), `shadow_spread` (-2 to 2), and `shadow_opa` (30-50%) values.
- **Pre-rendered shadow image (ARGB8888)**: PNG with Gaussian blur baked in by ImageMagick. Alpha channel was ignored — rendered as a grey/white opaque rectangle. The `RGB565_SWAPPED` display format likely breaks ARGB8888 alpha blending.
- **Pre-rendered shadow image (RGB565A8)**: Same format as logos (which DO alpha-blend correctly). Shadow still rendered as a visible lighter rectangle instead of a dark shadow. The issue may be that the image is mostly-transparent black pixels, which the blend path handles differently than the logos' mostly-opaque colored pixels.
- **Pre-rendered shadow image (A8 + image_recolor)**: Alpha-only mask with `image_recolor` set to black. Rendered as a solid black square — no alpha gradient.
- **Oversized rounded rect behind logo**: Hard edges, no blur — just a bigger square.

The logos work perfectly with RGB565A8 alpha (rounded transparent corners). The difference is that logos are mostly opaque with small transparent regions, while the shadow is mostly transparent with small semi-opaque regions. LVGL's renderer may optimize away mostly-transparent pixels or handle them differently.

On a 1.8" round display with dark station-color backgrounds, the logo already has natural contrast and depth. No shadow needed.

### Half-res bg images with LVGL scaling is SLOWER

Tried 180×180 bg images with `LV_IMAGE_ALIGN_STRETCH` to scale to 360×360, expecting 4× less blend work. Result: dropped to 3 FPS — worse than full-res. LVGL's software image scaling runs per-pixel bilinear interpolation every frame, which costs far more than the simple alpha blend it replaces. Always use native-resolution images for animated layers.

### PSRAM bounce buffers don't work for SPI displays

The `bounce_buffer_size_px` feature only exists on RGB parallel panel interfaces, not SPI/QSPI. Setting `buff_spiram=true` on SPI displays falls back to CPU memcpy (no DMA), which is slower.

### Dual-core LVGL rendering eats internal SRAM

`CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=2` requires `CONFIG_LV_OS_FREERTOS=y`, which adds FreeRTOS thread stacks and synchronization structures in internal SRAM. This pushed the system past the OOM threshold for DMA buffer allocation. Not viable with the current memory layout.

## Font Files

### Generated fonts can be truncated by tools

Font `.c` files (generated by `lv_font_conv`) are large (50-100KB). Some tools truncate files around 50KB. If a font file fails to compile with "unterminated #if" or "invalid suffix", check if it was truncated — compare `wc -c` against git history. The complete versions are in the git log.

## Maintaining This File

**Keep SKILLS.md up to date.** When you discover a new gotcha, fix a hard-to-debug issue, or learn something that would save future time — add it here. When a skill becomes obsolete because the code changed, remove it. Stale knowledge is dangerous.

Also keep `AGENTS.md` in sync — it covers project structure and conventions, while this file covers hard-won debugging knowledge and gotchas. Both go stale fast. The `skills/` directory has task-specific guides (building, hardware, UI, Sonos, voice) that complement this file.
