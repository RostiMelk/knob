# Architecture

## Monorepo Component Architecture

The project is structured as a monorepo with shared components and per-app entry points.

```
components/              Shared ESP-IDF components (reusable across apps)
  knob_hal/              Display, touch, encoder, backlight (hardware abstraction)
  knob_ui/               LVGL core setup, shared UI utilities
  knob_net/              WiFi STA management, reconnect logic
  knob_sonos/            Sonos UPnP/SOAP client + SSDP discovery
  knob_storage/          NVS persistence
  knob_events/           Shared event base and event IDs (knob_events.h)
  knob_voice/            Voice mode (OpenAI Realtime API)

apps/radio/              Radio application
  main/
    main.cpp             App entry point
    app_config.h         Radio-specific config (station list, UI tuning)
    ui/                  Radio-specific LVGL screens and state machine
  CMakeLists.txt         Pulls in required components via REQUIRES
```

Each app declares which components it needs in its `CMakeLists.txt`. Components never depend on apps — dependencies point strictly downward. A new app (e.g., a voice-only controller) can pick a different subset of components without touching the shared code.

Shared configuration lives in component headers:

| Header           | Component   | Contents                                      |
| ---------------- | ----------- | --------------------------------------------- |
| `hal_pins.h`     | knob_hal    | GPIO pin assignments, display/touch/encoder   |
| `knob_events.h`  | knob_events | Event base, event IDs, event data structs     |
| `sonos_config.h` | knob_sonos  | Sonos polling intervals, timeouts             |
| `voice_config.h` | knob_voice  | Voice mode parameters, OpenAI config          |
| `app_config.h`   | apps/radio  | Station list, UI colors, radio-specific knobs |

## Module Dependency Graph

```
apps/radio/main/main.cpp
  ├── knob_storage          NVS persistence (wifi creds, station index, volume, speaker IP)
  ├── knob_net              STA connect, reconnect, event emission
  ├── knob_sonos            UPnP/SOAP HTTP client + SSDP speaker discovery
  ├── knob_hal              Rotary encoder via PCNT, display, touch init
  ├── knob_ui               LVGL core setup and shared UI utilities
  ├── knob_events           Shared event bus (APP_EVENT base + IDs)
  └── apps/radio/main/ui/   Radio-specific screens and state machine
```

Components are peers. They never call each other directly. All cross-component communication goes through `esp_event`, with event IDs defined in `knob_events.h`.

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

Custom event base: `APP_EVENT` declared in `knob_events.h` (knob_events component), shared across all components and apps.

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
app_main()                              (apps/radio/main/main.cpp)
  1. settings_init()                    Load NVS config (knob_storage)
  2. ui_init()                          Init display, touch, LVGL, show splash (knob_hal + knob_ui + radio ui/)
  3. encoder_init()                     Configure PCNT, rotation only (knob_hal)
  4. discovery_init()                   Prepare SSDP (knob_sonos)
  5. sonos_init()                       Create command queue (knob_sonos)
  6. wifi_manager_init()                Start STA connection, async (knob_net)
       ├── on connect + saved speaker  ──► sonos_start() + play
       ├── on connect + no speaker     ──► SSDP scan → picker or auto-select
       └── on fail                     ──► retry with backoff
  7. Scheduler takes over
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

Speaker is discovered via SSDP on first boot. If one speaker found, auto-selected. If multiple, user picks from a list. Selection saved to NVS — subsequent boots reconnect immediately. `CONFIG_RADIO_SONOS_SPEAKER_IP` in `sdkconfig.defaults.local` is an optional override.

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
