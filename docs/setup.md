# Setup Guide

## Prerequisites

- Waveshare ESP32-S3-Knob-Touch-LCD-1.8 (CNC case variant)
- USB-C cable
- FAT-formatted micro SD card
- ESP-IDF v5.4+ installed ([install guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/))
- A Sonos speaker on your local network

## 1. Prepare the SD Card

Copy `.env.template` from the project root to your SD card as `.env`:

```
WIFI_SSID=YourNetwork
WIFI_PASS=YourPassword
VOLUME=20
STATION=0
```

`SPEAKER_IP` is optional — the device discovers Sonos speakers automatically.

Insert the SD card into the board's TF slot.

## 2. Verify Pin Assignments

The pins in `main/app_config.h` were verified against the official Waveshare demo (June 2025). If Waveshare ships a hardware revision, double-check against:

```
ESP32-S3-Knob-Touch-LCD-1.8-Demo/ESP-IDF/08_LVGL_Test/main/user_config.h
```

Download the demo ZIP from: https://files.waveshare.com/wiki/ESP32-S3-Knob-Touch-LCD-1.8/ESP32-S3-Knob-Touch-LCD-1.8-Demo.zip

## 3. USB-C Orientation

This board has **two MCUs** sharing one USB-C port. The port direction selects which MCU is connected:

- **One direction** → ESP32-S3 (ours)
- **Other direction** → ESP32 (co-processor, ignore)

If flashing fails or the serial monitor shows the wrong chip, **flip the USB-C cable**.

You can verify with:

```
esptool.py --port /dev/ttyACM0 chip_id
```

Should report `ESP32-S3`.

## 4. Build and Flash

```
cd radio
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

Replace `/dev/ttyACM0` with your actual port. On macOS it's typically `/dev/cu.usbmodem*`.

First build takes 3–5 minutes. Subsequent builds are incremental (~30s).

### If the device doesn't enter flash mode

Hold the **BOOT** button while plugging in USB, then release after 1 second. This forces download mode.

## 5. First Boot Sequence

Watch the serial monitor. You should see:

```
[I][main] Sonos Radio starting
[I][settings] NVS initialized
[I][settings] Reading .env from SD card
[I][settings] .env: WIFI_SSID=YourNetwork
[I][settings] .env: WIFI_PASS=***
[I][settings] Applied 4 lines from .env
[I][wifi] STA init — SSID: YourNetwork
[I][wifi] Connected — IP: 192.168.1.x
[I][discovery] Scanning for Sonos speakers...
[I][discovery] Found: Living Room at 192.168.1.y:1400
[I][sonos] Speaker changed: 192.168.1.y:1400
[I][sonos] Playing: https://stream-relay-geo.ntslive.net/stream
```

### What you should see on the display

1. Splash / dark screen while WiFi connects
2. WiFi icon turns blue when connected
3. If one speaker found → auto-connects, shows "Now Playing" with station name
4. If multiple speakers → speaker picker screen, tap to select
5. Volume arc appears when you turn the knob

## 6. After First Boot

- The SD card can be removed — WiFi credentials and speaker selection are stored in flash (NVS)
- To change WiFi: update `.env` on the SD card, reinsert, reboot
- To change speaker: clear NVS or add `SPEAKER_IP=x.x.x.x` to `.env`
- Settings persist across reflashes unless you erase flash

### Erase all settings

```
idf.py -p /dev/ttyACM0 erase-flash
idf.py -p /dev/ttyACM0 flash
```

## 7. Display Driver Note

The ST77916 is a QSPI display. The current code uses `esp_lcd_new_panel_st7789` as a placeholder — this may produce a blank or garbled display on the real hardware.

When you first boot and see nothing on screen (but serial logs look correct), you'll need to port the ST77916 init sequence from the Waveshare demo:

```
ESP32-S3-Knob-Touch-LCD-1.8-Demo/ESP-IDF/08_LVGL_Test/
```

Look for the LCD init command table (register writes via SPI). This is the one piece that needs to be adapted from the Waveshare demo code. The rest of the pipeline (LVGL, touch, encoder, WiFi, Sonos) should work as-is.

## Troubleshooting

| Symptom | Likely cause |
|---------|-------------|
| No serial output | USB-C is in wrong orientation — flip it |
| `wifi: Connecting...` loops forever | Wrong SSID/password in `.env` |
| `No .env on SD card` | Card not FAT formatted, or file named wrong (must be exactly `.env`) |
| Display blank, serial logs OK | ST77916 init sequence needs porting (see §7) |
| `discovery: 0 speaker(s) found` | Sonos not on same subnet, or speaker is in a group/bonded pair |
| Touch not responding | Check I2C bus — SDA=11, SCL=12. May need to verify CST816 variant (CST816D vs CST816S) |
| Encoder direction reversed | Swap `PIN_ENC_A` and `PIN_ENC_B` in `app_config.h` |
