# Hardware Reference

**Board**: Waveshare ESP32-S3-Knob-Touch-LCD-1.8 (CNC case, no battery variant)
**Wiki**: https://www.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8
**Schematic**: https://files.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8/ESP32-S3-Knob-Touch-LCD-1.8-schematic.zip
**Demo code**: https://files.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8/ESP32-S3-Knob-Touch-LCD-1.8-Demo.zip

## Dual-MCU Architecture

| MCU             | Role                                              | Flash           | PSRAM       |
| --------------- | ------------------------------------------------- | --------------- | ----------- |
| **ESP32-S3R8**  | Main (display, WiFi, touch, app logic)            | 16MB (external) | 8MB (octal) |
| **ESP32-U4WDH** | Co-processor (Bluetooth audio, secondary encoder) | 4MB (internal)  | —           |

USB-C port direction selects which MCU is connected. We target the **ESP32-S3** only.

## Display — ST77916 QSPI 360×360

All pins verified from `ESP-IDF/08_LVGL_Test/main/user_config.h`.

| Signal       | GPIO   | Notes            |
| ------------ | ------ | ---------------- |
| CS           | **14** |                  |
| CLK (PCLK)   | **13** |                  |
| DATA0 (SIO0) | **15** | QSPI data line 0 |
| DATA1 (SIO1) | **16** | QSPI data line 1 |
| DATA2 (SIO2) | **17** | QSPI data line 2 |
| DATA3 (SIO3) | **18** | QSPI data line 3 |
| RST          | **21** |                  |
| Backlight    | **47** | PWM via LEDC     |

- Color depth: RGB565
- SPI host: `SPI2_HOST`

## Touch — CST816D I2C

Pins verified from `Arduino/examples/08_LVGL_Test/lcd_config.h` and `ESP-IDF/08_LVGL_Test/main/user_config.h`.

| Signal | GPIO   | Notes          |
| ------ | ------ | -------------- |
| SDA    | **11** | Shared I2C bus |
| SCL    | **12** | Shared I2C bus |
| INT    | **9**  | Active low     |
| RST    | **10** |                |

- I2C address: `0x15`
- I2C port: `I2C_NUM_0`
- Datasheet: https://files.waveshare.com/wiki/common/CST816D_datasheet_En_V1.3.pdf

## Rotary Encoder

Pins verified from `ESP-IDF/04_Encoder_Test/main/user_config.h`. **No push button** — the encoder is rotation-only. Screen selection uses the touchscreen.

| Signal  | GPIO  |
| ------- | ----- |
| A (ECA) | **8** |
| B (ECB) | **7** |

Waveshare demo uses a software-based knob library (`bidi_switch_knob`). We use the hardware PCNT peripheral instead for zero CPU overhead.

## Audio — PCM5100A DAC + PDM Microphone

Pins verified from `ESP-IDF/07_Audio_Test/main/user_config.h`. Not used in Phase 1.

**I2S output (to DAC → 3.5mm jack)**:

| Signal    | GPIO   |
| --------- | ------ |
| BCLK      | **39** |
| WS (LRCK) | **40** |
| DOUT      | **41** |

**PDM microphone input**:

| Signal | GPIO   |
| ------ | ------ |
| CLK    | **45** |
| DATA   | **46** |

## I2C Bus (shared)

Single I2C bus on `I2C_NUM_0` at 400 kHz. Shared by:

| Device                | Address |
| --------------------- | ------- |
| CST816D touch         | `0x15`  |
| DRV2605 haptic driver | `0x5A`  |

Bus pins: SDA = GPIO 11, SCL = GPIO 12.

## SD Card (SDMMC 4-wire)

Pins verified from `ESP-IDF/02_SD_Card/components/sdcard_bsp/sdcard_bsp.c`.

| Signal | GPIO   |
| ------ | ------ |
| CMD    | **3**  |
| CLK    | **4**  |
| D0     | **5**  |
| D1     | **6**  |
| D2     | **42** |
| D3     | **2**  |

Mounted at `/sdcard` via `esp_vfs_fat_sdmmc`. FatFS formatted.

### .env Config File

On boot, the device reads `/sdcard/.env` if present. Standard `.env` format — `KEY=value`, one per line. Lines starting with `#` are comments.

Copy `.env.template` from the project root to your SD card as `.env` and fill in your values:

```
WIFI_SSID=MyNetwork
WIFI_PASS=MyPassword
SPEAKER_IP=192.168.1.100
VOLUME=25
STATION=0
```

Values are written to NVS. The SD card can be removed after first boot — settings persist.

## Other Peripherals

| Peripheral           | Interface        | Notes                               |
| -------------------- | ---------------- | ----------------------------------- |
| DRV2605 haptic motor | I2C `0x5A`       | Vibration feedback                  |
| CH445P analog switch | —                | USB-C MCU selector                  |
| TF card slot         | SDMMC 4-wire     | FatFS formatted                     |
| Battery charger      | MX1.25 connector | Not present on our CNC-case variant |

## Memory Map

| Region        | Size  | Use                                                        |
| ------------- | ----- | ---------------------------------------------------------- |
| Internal SRAM | 512KB | Stack, DRAM, DMA-capable LCD transfer buffers              |
| PSRAM (octal) | 8MB   | LVGL draw buffers, HTTP response bodies, large allocations |
| Flash         | 16MB  | Firmware (2× OTA slots), NVS, SPIFFS                       |
