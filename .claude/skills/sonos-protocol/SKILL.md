---
name: sonos-protocol
description: Sonos UPnP/SOAP protocol details and debugging. Use when modifying Sonos control, adding new commands, or debugging playback issues.
---

# Sonos UPnP/SOAP Protocol

## Architecture

Sonos speakers expose UPnP services over HTTP on port 1400.
We use two services:

- **AVTransport** (`/MediaRenderer/AVTransport/Control`) — play, stop, set URI
- **RenderingControl** (`/MediaRenderer/RenderingControl/Control`) — volume

All commands are SOAP XML over HTTP POST.

## URI Format

**IMPORTANT**: Since Sonos firmware v6.4.2+, internet radio streams MUST use
the `x-rincon-mp3radio://` URI scheme instead of `http://` or `https://`.

```cpp
// Code converts automatically:
// https://stream.example.com/radio → x-rincon-mp3radio://stream.example.com/radio
// http://stream.example.com/radio  → x-rincon-mp3radio://stream.example.com/radio
```

## Stereo Pairs

Sonos stereo pairs have an invisible right channel speaker.
The zone group topology XML contains `Invisible='1'` for the right channel.

**Always send commands to the coordinator** (the visible speaker).
Sending to the invisible member causes silent failures or error 1023.

## SOAP Envelope Format

```xml
<?xml version="1.0" encoding="utf-8"?>
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
 s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
<s:Body>
  <u:ActionName xmlns:u="urn:schemas-upnp-org:service:ServiceName:1">
    <InstanceID>0</InstanceID>
    <!-- action-specific params -->
  </u:ActionName>
</s:Body>
</s:Envelope>
```

The `SOAPAction` header must be: `"urn:schemas-upnp-org:service:ServiceName:1#ActionName"`

## Implemented Commands

| Command | Service | Action |
|---------|---------|--------|
| Play URI | AVTransport | SetAVTransportURI + Play |
| Stop | AVTransport | Stop |
| Get state | AVTransport | GetTransportInfo |
| Get volume | RenderingControl | GetVolume |
| Set volume | RenderingControl | SetVolume |

## State Polling

The net task polls Sonos state every `CONFIG_RADIO_SONOS_POLL_INTERVAL_MS` (default 5000ms).
Polling fires HTTP lifecycle events (CONNECT, HEADERS, DATA, FINISH) that can overflow
the system event queue — hence `CONFIG_ESP_SYSTEM_EVENT_TASK_QUEUE_SIZE=64`.

## Debugging

### Use curl to isolate issues

Before debugging firmware, reproduce with curl from your machine:

```bash
curl -X POST http://SPEAKER_IP:1400/MediaRenderer/AVTransport/Control \
  -H 'Content-Type: text/xml; charset="utf-8"' \
  -H 'SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#GetTransportInfo"' \
  -d '<?xml version="1.0"?><s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/" s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/"><s:Body><u:GetTransportInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1"><InstanceID>0</InstanceID></u:GetTransportInfo></s:Body></s:Envelope>'
```

If curl also fails, the bug is in the service/protocol, not our firmware.
This saved hours on the error 1023 debugging.

### Common Errors

- **HTTP 500**: SOAP fault — check the XML body for `<errorCode>` and `<errorDescription>`
- **Error 1023**: Usually means the URI format is wrong or the speaker can't reach the stream
- **Error 701**: Invalid SetAVTransportURI — check URI encoding
- **Connection refused**: Speaker IP wrong or speaker offline
- **Timeout**: `SONOS_HTTP_TIMEOUT_MS` may be too short for slow networks

## Speaker Discovery (SSDP)

On first boot (no saved speaker IP), we run SSDP multicast discovery:
1. Send M-SEARCH to 239.255.255.250:1900
2. Parse responses for Sonos devices
3. Fetch zone group topology to find coordinators
4. Filter out invisible stereo pair members
5. Present speaker picker UI

Saved speaker IP persists in NVS — discovery only runs on first boot or manual trigger.
