# Build Guide

ESP32-S3 Sonos radio controller. Hybrid Rust/C++ on a Waveshare knob display board.

## Prerequisites

**ESP-IDF v5.4+**
```bash
git clone -b v5.4 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf
cd ~/esp/esp-idf && ./install.sh esp32s3
```

**Rust ESP toolchain**
```bash
cargo install espup && espup install
```
Installs the Xtensa Rust fork + `ldproxy`.

**SDL2** (simulator only)
```bash
brew install sdl2   # macOS
```

---

## Build & Flash (Hardware)

```bash
source ~/esp/esp-idf/export.sh    # needed once per terminal session
cd /path/to/radio

idf.py set-target esp32s3          # only needed once after clean
idf.py build                       # ~10-15 min first time, fast after
idf.py -p /dev/tty.usbmodem* flash monitor   # macOS
# or: idf.py -p /dev/ttyACM0 flash monitor   # Linux
```

> **Pro tip:** Add `alias get_idf='source ~/esp/esp-idf/export.sh'` to `~/.zshrc`

---

## Build & Run (Simulator)

```bash
cmake -B build -S sim
cmake --build build -j$(sysctl -n hw.ncpu)   # macOS
# or: cmake --build build -j$(nproc)          # Linux
./build/sim
```

Must be launched from the project root — asset paths are relative.

### Simulator Controls

| Key | Action |
|-----|--------|
| Up / Down | Volume +/- |
| Enter / Space | Toggle screen |
| Mouse click | Touch tap |
| Scroll wheel | Encoder rotation |
| Q / Esc | Quit |

---

## Device Configuration

Create a `.env` file on a micro SD card and insert it before first boot:

```
WIFI_SSID=YourNetwork
WIFI_PASS=YourPassword
VOLUME=33
STATION=0
```

The device reads this on boot and stores the values to flash. The card can be removed after.

**Optional settings:**
- `SPEAKER_IP=192.168.1.100` — skip auto-discovery, connect directly
- `OPENAI_API_KEY=sk-proj-...` — enable voice assistant (activate with double-tap)

---

## Hardware

| Component | Details |
|-----------|---------|
| Board | Waveshare ESP32-S3-Knob-Touch-LCD-1.8 |
| Display | ST77916 360×360 QSPI LCD |
| Touch | CST816 I2C |
| Input | Rotary encoder (no push button) |
| MCU | ESP32-S3R8 — 8MB PSRAM, 16MB flash |

---

## Project Structure

```
radio/
├── main/           # C++ application code (ESP-IDF main component)
├── components/     # C++ components (radio_ui, radio_net, radio_input)
├── src/            # Rust source (hybrid architecture)
├── sim/            # Desktop simulator (SDL2 + LVGL)
├── Cargo.toml      # Rust build config
├── CMakeLists.txt  # ESP-IDF project root
└── sdkconfig.defaults  # Hardware config (flash, PSRAM, WiFi buffers)
```

---

## Troubleshooting

**`$IDF_PATH` is empty**
Run `source ~/esp/esp-idf/export.sh` first. This must be done in every new terminal session.

**First build is very slow**
Normal — it compiles ESP-IDF, downloads LVGL v9, and builds the Rust crate. Expect ~10–15 min. Subsequent builds are fast.

**`fatal: not a git repository`** during `export.sh`
Harmless warning. Ignore it.

**USB device not found**
Find the port first:
```bash
ls /dev/tty.usb*    # macOS
ls /dev/ttyACM*     # Linux
```

**Display blank after flash**
Verify the ST77916 driver is active — check the build log for `ESP_LCD_ST77916`.
