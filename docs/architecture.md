# Architecture

## Module Dependency Graph

```
main.cpp
  ├── storage/settings    NVS persistence (wifi creds, station index, volume, speaker IP)
  ├── net/wifi_manager    STA connect, reconnect, event emission
  ├── sonos/sonos         UPnP/SOAP HTTP client (AVTransport + RenderingControl)
  ├── sonos/discovery     SSDP multicast speaker discovery
  ├── input/encoder       Rotary encoder via PCNT (no button — rotation only)
  └── ui/ui               LVGL screens, state machine, display+touch init
```

Modules are peers. They never call each other directly. All cross-module communication goes through `esp_event`.

## Event Flow

```
[Encoder turn] ──► ESP_EVENT "ENCODER_ROTATE" ──► ui_on_encoder_rotate()
                   Mode-dependent:
                   • VOLUME mode → volume +/- overlay → sonos (SetVolume)
                   • BROWSE mode → cycle through stations, reset 7s timer

[Touch tap]    ──► LVGL click on screen ──► ui mode switch
                   • VOLUME mode → enter BROWSE mode
                   • BROWSE mode → confirm station, start streaming, → VOLUME mode

[WiFi connected]──► ESP_EVENT "WIFI_CONNECTED" ──► discover speakers or reconnect saved
                                                ──► ui (hide "connecting" overlay)

[Sonos state]   ──► ESP_EVENT "SONOS_STATE"    ──► ui (update now-playing text)
```

The encoder has **no push button** (rotation only, 2 pins). All selection is via the touchscreen. Single screen with two modes — no screen transitions.

Custom event base: `APP_EVENT` declared in `main.cpp`, shared via `app_config.h`.

## Task Model

| Task              | Core | Priority | Stack | Purpose                                           |
| ----------------- | ---- | -------- | ----- | ------------------------------------------------- |
| **ui_task**       | 0    | 5        | 8KB   | LVGL timer handler (every 5ms), display flush     |
| **net_task**      | 1    | 4        | 6KB   | Sonos HTTP requests, state polling                |
| **discover**      | 1    | 4        | 6KB   | One-shot SSDP scan on first boot (self-deletes)   |
| **wifi (system)** | —    | —        | —     | ESP-IDF WiFi driver, runs on system task          |
| **event loop**    | —    | —        | —     | Default esp_event loop, dispatches to subscribers |

LVGL is not thread-safe. All LVGL mutation happens in `ui_task`. Other tasks post events; `ui_task` processes them on next tick.

For occasional cross-task LVGL access (e.g., network status update), use:

```c
if (esp_lvgl_port_lock(100)) {
    // safe LVGL calls
    esp_lvgl_port_unlock();
}
```

## Memory Strategy

- **Internal DRAM**: Task stacks, DMA-capable LCD transfer buffers (2× draw buffers, ~25KB each for 360×36 partial rows)
- **PSRAM**: LVGL frame buffer, HTTP response bodies, any allocation >4KB
- **NVS**: WiFi SSID/pass, selected station index, volume level, Sonos speaker IP

LVGL configured with 2 partial draw buffers in internal DMA-capable RAM. Display flush via QSPI DMA. This keeps frame rate high without consuming all internal SRAM.

## Boot Sequence

```
app_main()
  1. settings_init()          Load NVS config
  2. settings_load_config_from_sd()  Read .env from SD card → NVS (if present)
  3. ui_init()                Init display, touch, LVGL, show splash
  4. encoder_init()           Configure PCNT (rotation only, no button)
  5. discovery_init()         Prepare SSDP
  6. sonos_init()             Create command queue
  7. wifi_manager_init()      Start STA connection (async)
       ├── on connect + saved speaker  ──► sonos_start() + play
       ├── on connect + no speaker     ──► SSDP scan → picker or auto-select
       └── on fail                     ──► retry with backoff
  8. Scheduler takes over
```

## Sonos Interaction Model

```
[Station selected]
  ──► sonos_play_uri(station_url)
        HTTP POST to speaker_ip:1400
        SOAP Action: SetAVTransportURI + Play

[Volume changed]
  ──► sonos_set_volume(level)
        HTTP POST to speaker_ip:1400
        SOAP Action: SetVolume

[Polling] (every 5s while connected)
  ──► sonos_get_transport_info()
        Parse current URI, play state
        Post SONOS_STATE event if changed
```

Speaker is discovered via SSDP on first boot. If one speaker found, auto-selected. If multiple, user picks from a list. Selection saved to NVS — subsequent boots reconnect immediately. `SPEAKER_IP` in `.env` is an optional override.

## Single-Screen UX

One screen, two modes. No page transitions.

```
┌──────────────────────────────────────────────────┐
│                    VOLUME MODE                    │
│                   (default)                       │
│                                                   │
│    encoder turn → volume overlay (auto-hides)     │
│    screen tap   → enter BROWSE mode               │
│                                                   │
│    Shows: station name, play state, speaker name  │
└──────────────────┬───────────────────────────────┘
                   │ tap
                   ▼
┌──────────────────────────────────────────────────┐
│                   BROWSE MODE                     │
│                                                   │
│    encoder turn → cycle stations (wraps around)   │
│    screen tap   → SELECT station → start stream   │
│                   → return to VOLUME mode          │
│    7s inactivity → cancel, revert, → VOLUME mode  │
│                                                   │
│    Shows: station name (blue), "tap to play",     │
│           position (e.g. "3 / 5")                 │
└──────────────────────────────────────────────────┘
```

Volume overlay draws on top of the main screen and auto-hides after 1.5s.

Speaker picker is a separate screen shown only on first boot (no saved speaker). Uses encoder to scroll, touch to select.
