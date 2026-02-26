# Sonos UPnP/SOAP Reference

Sonos speakers expose UPnP services on port **1400** over HTTP. No authentication required on LAN.

All requests are `POST` to the service endpoint with `Content-Type: text/xml; charset="utf-8"` and a `SOAPAction` header.

## Endpoints

| Service | Path | Use |
|---------|------|-----|
| AVTransport | `/MediaRenderer/AVTransport/Control` | Play, stop, set URI, get state |
| RenderingControl | `/MediaRenderer/RenderingControl/Control` | Volume |
| DeviceProperties | `/DeviceProperties/Control` | Get speaker name (optional) |

## AVTransport

### SetAVTransportURI

Sets the stream URL. Does **not** start playback — call `Play` after.

```
SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#SetAVTransportURI"
```

```xml
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
  s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:SetAVTransportURI xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
      <InstanceID>0</InstanceID>
      <CurrentURI>{STREAM_URL}</CurrentURI>
      <CurrentURIMetaData></CurrentURIMetaData>
    </u:SetAVTransportURI>
  </s:Body>
</s:Envelope>
```

### Play

```
SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#Play"
```

```xml
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
  s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:Play xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
      <InstanceID>0</InstanceID>
      <Speed>1</Speed>
    </u:Play>
  </s:Body>
</s:Envelope>
```

### Stop

```
SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#Stop"
```

```xml
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
  s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:Stop xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
      <InstanceID>0</InstanceID>
    </u:Stop>
  </s:Body>
</s:Envelope>
```

### GetTransportInfo

Returns current play state. Use for polling.

```
SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#GetTransportInfo"
```

```xml
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
  s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:GetTransportInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
      <InstanceID>0</InstanceID>
    </u:GetTransportInfo>
  </s:Body>
</s:Envelope>
```

**Response** (relevant fields):

```xml
<CurrentTransportState>PLAYING</CurrentTransportState>
<!-- Values: PLAYING, PAUSED_PLAYBACK, STOPPED, TRANSITIONING -->
```

### GetPositionInfo

Returns current URI. Useful to detect what's playing.

```
SOAPAction: "urn:schemas-upnp-org:service:AVTransport:1#GetPositionInfo"
```

```xml
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
  s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:GetPositionInfo xmlns:u="urn:schemas-upnp-org:service:AVTransport:1">
      <InstanceID>0</InstanceID>
    </u:GetPositionInfo>
  </s:Body>
</s:Envelope>
```

**Response** (relevant fields):

```xml
<TrackURI>x-rincon-mp3radio://...</TrackURI>
<TrackMetaData>...</TrackMetaData>
```

## RenderingControl

### GetVolume

```
SOAPAction: "urn:schemas-upnp-org:service:RenderingControl:1#GetVolume"
```

```xml
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
  s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:GetVolume xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1">
      <InstanceID>0</InstanceID>
      <Channel>Master</Channel>
    </u:GetVolume>
  </s:Body>
</s:Envelope>
```

**Response**:

```xml
<CurrentVolume>42</CurrentVolume>
<!-- Range: 0–100 -->
```

### SetVolume

```
SOAPAction: "urn:schemas-upnp-org:service:RenderingControl:1#SetVolume"
```

```xml
<s:Envelope xmlns:s="http://schemas.xmlsoap.org/soap/envelope/"
  s:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
  <s:Body>
    <u:SetVolume xmlns:u="urn:schemas-upnp-org:service:RenderingControl:1">
      <InstanceID>0</InstanceID>
      <Channel>Master</Channel>
      <DesiredVolume>{0-100}</DesiredVolume>
    </u:SetVolume>
  </s:Body>
</s:Envelope>
```

## Implementation Notes

### HTTP Request Template

```
POST {endpoint} HTTP/1.1
Host: {speaker_ip}:1400
Content-Type: text/xml; charset="utf-8"
SOAPAction: "{action}"
Content-Length: {length}

{soap_body}
```

### Response Parsing

Responses are XML. For our needs, simple string search for tag content is sufficient — no need for a full XML parser on device. Example: find `<CurrentVolume>` and `</CurrentVolume>`, extract the substring.

### Error Responses

HTTP 500 with SOAP fault body. Common causes:
- Wrong InstanceID (always use `0`)
- Invalid volume range
- Speaker in a state that doesn't accept the command

### Radio Stream URLs

Sonos accepts standard HTTP audio streams. Prefix with `x-rincon-mp3radio://` for radio stations:

```
x-rincon-mp3radio://stream.example.com/radio.mp3
```

Or use plain HTTP URLs — Sonos will auto-detect format.

### SSDP Discovery (Phase 1+)

To find Sonos speakers without hardcoding IP:

```
M-SEARCH * HTTP/1.1
HOST: 239.255.255.250:1900
MAN: "ssdp:discover"
MX: 3
ST: urn:schemas-upnp-org:device:ZonePlayer:1
```

Send as UDP multicast. Speakers respond with `LOCATION` header pointing to their device description XML. Parse for friendly name and control URLs.

Not in MVP — we hardcode speaker IP in NVS settings.
