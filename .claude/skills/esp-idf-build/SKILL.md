---
name: esp-idf-build
description: ESP-IDF build, flash, and sdkconfig workflow for ESP32-S3. Use when building firmware, flashing devices, modifying sdkconfig, or debugging boot/build issues.
---

# ESP-IDF Build Workflow

## Prerequisites

- ESP-IDF v5.4 installed at `~/esp/esp-idf`
- Source before use: `. ~/esp/esp-idf/export.sh`
- Target: `idf.py set-target esp32s3` (only needed once after clean)

## Build Commands

```bash
./test.sh              # Build + lint (recommended pre-push check)
./flash.sh -m          # Build + flash + monitor
./flash.sh --build-only # Build only
idf.py fullclean       # Nuclear clean (needed after Kconfig choice changes)
```

## sdkconfig Management

Defaults live in `sdkconfig.defaults` and `sdkconfig.defaults.esp32s3`.
The generated `sdkconfig` is gitignored.

### Changing Kconfig Settings

1. Edit `sdkconfig.defaults` (NOT `sdkconfig` directly)
2. For "choice" blocks, add explicit unset:
   ```
   # CONFIG_LV_USE_BUILTIN_MALLOC is not set
   CONFIG_LV_USE_CLIB_MALLOC=y
   ```
3. Delete sdkconfig: `rm sdkconfig`
4. Full clean: `idf.py fullclean`
5. Rebuild: `idf.py build`
6. **VERIFY**: `grep CONFIG_NAME build/config/sdkconfig.h`

Choices silently revert if the unset line is missing. Always verify.

## Partition Table

Custom layout in `partitions.csv`:
- 1.5 MB factory app
- 8.5 MB SPIFFS storage (station logos)

Partition names in code must match CSV exactly.

## Flash Troubleshooting

- **Device not found**: Flip USB-C cable (CH445P switch routes to different chips)
- **Boot loop after display change**: Power cycle, reflash with known-good commit
- **No reset button**: Power slider only. Cycle power to restart.
- **Port on macOS**: `/dev/cu.usbmodem2101`
- **Monitor exit**: Ctrl+T then Ctrl+X

## CI

GitHub Actions (`.github/workflows/build.yml`) runs `idf.py build` on every PR.
Uses `espressif/idf:v5.4` Docker image.
