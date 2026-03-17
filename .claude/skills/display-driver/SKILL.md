---
name: display-driver
description: ST77916 QSPI display driver constraints and LVGL configuration. Use when modifying display init, LVGL config, draw buffers, color settings, or touch input.
---

# Display Driver — ST77916 360×360 QSPI

## Hardware

- Panel: ST77916 360×360 round LCD
- Interface: QSPI (4-wire SPI) @ 50 MHz via SPI2
- Touch: CST816S I2C @ 400 kHz (shared I2C bus with DRV2605 haptics)
- Backlight: LEDC PWM 25 kHz, 10-bit, default 80%

## CRITICAL CONSTRAINTS

### RGB565 Only — RGB666 Causes Boot Loop

`esp_lvgl_port` allocates DMA buffers that only support RGB565 (16-bit).
Setting RGB666 or RGB888 causes an immediate unrecoverable boot loop.
This was tested and confirmed — recovery required USB reflash.

**Never change color depth without an alternative DMA buffer approach.**

### Draw Buffer Limits

| Config | Rows | Buffer (×2) | Flushes/frame | Memory |
|--------|------|-------------|---------------|--------|
| Current | 36 | 52 KB | 10 | Internal DMA SRAM |
| Max safe DMA | ~48 | ~69 KB | 8 | Internal DMA SRAM |
| PSRAM option | 120 | 173 KB | 3 | PSRAM (CPU copy, no DMA) |

- 54+ rows with DMA causes OOM (internal SRAM exhausted)
- ESP32-S3 SPI DMA **cannot read from PSRAM** — `buff_spiram=true` falls back to CPU memcpy
- 10 flushes/frame causes visible top-down scanline wipe (known perf issue)

### Required Settings

```cpp
// display_idf.cpp
disp_cfg.flags.swap_bytes = true;    // Required for correct colors
disp_cfg.rotation.mirror_x = true;   // 180° rotation
disp_cfg.rotation.mirror_y = true;   // (not sw_rotate)
disp_cfg.flags.buff_dma = true;      // DMA from internal SRAM
disp_cfg.flags.buff_spiram = false;   // PSRAM DMA not supported
```

### Struct Initialization

Use field-by-field assignment, NOT C++20 designated initializers.
`esp_lvgl_port` struct field order doesn't match declaration order,
causing compile errors with designated initializers.

## LVGL Configuration (sdkconfig.defaults)

```
CONFIG_LV_USE_FS_POSIX=y              # POSIX filesystem for image loading
CONFIG_LV_FS_POSIX_LETTER=65          # 'A' drive letter
CONFIG_LV_USE_CLIB_MALLOC=y           # Routes to PSRAM via malloc
CONFIG_LV_CACHE_DEF_SIZE=4200000      # 4.2MB image cache
CONFIG_LV_IMAGE_HEADER_CACHE_DEF_CNT=24
CONFIG_LV_DEF_REFR_PERIOD=10          # 10ms refresh (100 FPS target)
CONFIG_LV_DRAW_BUF_ALIGN=64           # Cache line alignment
```

## Image Rendering

- Background images (360×360) use `LV_IMAGE_ALIGN_CENTER` (not STRETCH)
- Logo images (100×100) use `LV_IMAGE_ALIGN_CENTER` (not STRETCH)
- STRETCH forces scaling code path even when source == destination size
- All images pre-cached at boot from SPIFFS into LVGL cache

## Touch

- CST816S on shared I2C bus (also DRV2605 haptics)
- Init order: I2C bus → touch → haptics
- Touch coordinates need no transformation (handled by esp_lvgl_port)

## Vendor Init Commands

`display_idf.cpp` contains 170+ ST77916 vendor-specific init commands
(gamma, GOA mapping, mux mapping, source EQ). These are tuned for the
specific Waveshare panel revision. Do not modify without hardware testing.
