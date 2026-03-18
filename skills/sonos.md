# Sonos — UPnP/SOAP Control & Speaker Discovery

## When to read this

You're modifying Sonos playback, volume control, speaker discovery, or the polling loop. Read `docs/sonos-upnp.md` alongside this for full SOAP envelope templates.

---

## Architecture

Sonos functionality lives in the **`knob_sonos`** shared component (`components/knob_sonos/`). It is app-agnostic — any app in the monorepo can link against it. The radio app registers its station list at init via `sonos_set_stations()`.

---

## Key Files

| File                                           | Purpose                                                                            |
| ---------------------------------------------- | ---------------------------------------------------------------------------------- |
| `components/knob_sonos/src/sonos.cpp`          | UPnP/SOAP HTTP client (AVTransport + RenderingControl)                             |
| `components/knob_sonos/include/sonos.h`        | Public API: `sonos_play_uri()`, `sonos_set_volume()`, `sonos_set_stations()`, etc. |
| `components/knob_sonos/src/discovery.cpp`      | SSDP multicast speaker discovery                                                   |
| `components/knob_sonos/include/sonos_config.h` | Constants (`SONOS_PORT`, etc.) and event IDs (`APP_EVENT_STATION_CHANGED`, etc.)   |
| `apps/radio/stations.json`                     | Station URLs and metadata for the radio app                                        |
| `docs/sonos-upnp.md`                           | Complete SOAP envelope templates for every command                                 |

### What moved

| Old path                   | New path                                  |
| -------------------------- | ----------------------------------------- |
| `main/sonos/sonos.cpp`     | `components/knob_sonos/src/sonos.cpp`     |
| `main/sonos/sonos.h`       | `components/knob_sonos/include/sonos.h`   |
| `main/sonos/discovery.cpp` | `components/knob_sonos/src/discovery.cpp` |
| `stations.json`            | `apps/radio/stations.json`                |

### What moved between headers

- Sonos event IDs (`APP_EVENT_STATION_CHANGED`, etc.) now live in `sonos_config.h`, **not** `app_config.h`.
- Sonos constants (`SONOS_PORT`, etc.) also in `sonos_config.h`.

---

## Using knob_sonos from an App

Apps depend on `knob_sonos` like any ESP-IDF component. At init, the app registers its station list:

```c
#include "sonos.h"

// App startup
sonos_set_stations(my_stations, my_station_count);
sonos_init();
```

`sonos_set_stations()` must be called before `sonos_init()`. The component does not own or embed any station data — the app provides it.

---

## Interaction Model

```
[Station selected]
  -> sonos_play_uri(station_url)
       HTTP POST to speaker_ip:1400
       SOAP Action: SetAVTransportURI + Play

[Volume changed]
  -> sonos_set_volume(level)
       HTTP POST to speaker_ip:1400
       SOAP Action: SetVolume

[Polling] (every 5s while connected)
  -> sonos_get_transport_info()
       Parse current URI, play state
       Post SONOS_STATE event if changed
```

Commands are HTTP POST requests with SOAP XML bodies to port 1400 on the speaker IP. Response parsing uses string search — no XML parser on the device.

---

## Constraints

### URI prefix required

Since Sonos firmware v6.4.2+, internet radio streams must use the `x-rincon-mp3radio://` URI prefix. Without it, Sonos silently refuses to play.

Stream URLs in `apps/radio/stations.json` use `https://`. The firmware prepends the prefix before sending to Sonos.

### Stereo pairs

In a stereo pair, the right channel has `Invisible='1'` in SSDP responses. All commands must be sent to the **coordinator** (left channel) IP. Sending to the right channel is silently ignored.

### HTTP event queue

`CONFIG_ESP_SYSTEM_EVENT_TASK_QUEUE_SIZE=64` (default 32 overflows from Sonos HTTP polling combined with WiFi events). If you see events being dropped, check this config value. The `sdkconfig` lives per-app under `apps/radio/`.

---

## Speaker Discovery

SSDP multicast scan runs once on first boot. Behavior:

| Scenario                                                      | Action                                                |
| ------------------------------------------------------------- | ----------------------------------------------------- |
| One speaker found                                             | Auto-selected, saved to NVS                           |
| Multiple speakers                                             | User picks from a list (encoder scroll, touch select) |
| No speakers                                                   | Retry with backoff                                    |
| `CONFIG_RADIO_SONOS_SPEAKER_IP` in `sdkconfig.defaults.local` | Skip discovery, use override                          |

The selected speaker IP is persisted to NVS. Subsequent boots reconnect immediately without discovery.

---

## Testing Sonos Changes

Always test SOAP commands with `curl` from your dev machine first to isolate firmware vs. service issues:

```bash
curl -X POST http://SPEAKER_IP:1400/MediaRenderer/AVTransport/Control \
  -H 'Content-Type: text/xml; charset="utf-8"' \
  -H 'SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#GetTransportInfo"' \
  -d '<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
  s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:GetTransportInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
      <InstanceID>0</InstanceID>
    </u:GetTransportInfo>
  </s:Body>
</s:Envelope>'
```

See `docs/sonos-upnp.md` for the complete set of SOAP envelopes (SetAVTransportURI, Play, Stop, GetVolume, SetVolume, GetPositionInfo).

---

## Common Issues

**Sonos stops playing after a few minutes**: Check if the stream URL returns a redirect. Sonos doesn't follow HTTP redirects from `x-rincon-mp3radio://` URIs. Use the final URL.

**Volume commands ignored**: You may be sending to the wrong speaker in a stereo pair. Verify you're targeting the coordinator IP (the one without `Invisible='1'`).

**Event queue overflow** (`ESP_ERR_TIMEOUT` from `esp_event_post`): Increase `CONFIG_ESP_SYSTEM_EVENT_TASK_QUEUE_SIZE` in the app's sdkconfig (`apps/radio/sdkconfig`). The Sonos polling loop + WiFi events + UI events can exceed 32 slots.

---

> **Keep this alive:** If you discover undocumented Sonos behavior or new constraints while working, update this file in the same PR.
