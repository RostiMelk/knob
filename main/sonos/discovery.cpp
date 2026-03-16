#include "discovery.h"
#include "app_config.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lwip/igmp.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

static constexpr const char *TAG = "discovery";

static constexpr const char *SSDP_ADDR = "239.255.255.250";
static constexpr uint16_t SSDP_PORT = 1900;

static constexpr const char *MSEARCH =
    "M-SEARCH * HTTP/1.1\r\n"
    "HOST: 239.255.255.250:1900\r\n"
    "MAN: \"ssdp:discover\"\r\n"
    "MX: 2\r\n"
    "ST: urn:schemas-upnp-org:device:ZonePlayer:1\r\n"
    "\r\n";

// ─── Helpers ────────────────────────────────────────────────────────────────────────────────

static bool parse_location(const char *response, char *out, size_t out_len) {
  const char *loc = strcasestr(response, "LOCATION:");
  if (!loc)
    return false;
  loc += 9;
  while (*loc == ' ')
    loc++;

  const char *end = strstr(loc, "\r\n");
  if (!end)
    end = loc + strlen(loc);

  size_t len = std::min(static_cast<size_t>(end - loc), out_len - 1);
  memcpy(out, loc, len);
  out[len] = '\0';
  return true;
}

static bool extract_ip_from_url(const char *url, char *ip, size_t ip_len,
                                uint16_t *port) {
  const char *host = strstr(url, "://");
  if (!host)
    return false;
  host += 3;

  const char *colon = strchr(host, ':');
  const char *slash = strchr(host, '/');

  if (colon && (!slash || colon < slash)) {
    size_t len = std::min(static_cast<size_t>(colon - host), ip_len - 1);
    memcpy(ip, host, len);
    ip[len] = '\0';
    *port = static_cast<uint16_t>(atoi(colon + 1));
  } else if (slash) {
    size_t len = std::min(static_cast<size_t>(slash - host), ip_len - 1);
    memcpy(ip, host, len);
    ip[len] = '\0';
    *port = 1400;
  } else {
    strncpy(ip, host, ip_len - 1);
    ip[ip_len - 1] = '\0';
    *port = 1400;
  }
  return ip[0] != '\0';
}

static bool already_found(const DiscoveryResult *result, const char *ip) {
  for (int i = 0; i < result->count; i++) {
    if (strcmp(result->speakers[i].ip, ip) == 0)
      return true;
  }
  return false;
}

// ─── Fetch Device Description XML ─────────────────────────────────────────────────────────────

struct HttpBuf {
  char *data;
  int len;
  int capacity;
};

static esp_err_t on_http_data(esp_http_client_event_t *evt) {
  auto *buf = static_cast<HttpBuf *>(evt->user_data);
  if (evt->event_id == HTTP_EVENT_ON_DATA && buf && evt->data_len > 0) {
    int needed = buf->len + evt->data_len;
    if (needed < buf->capacity) {
      memcpy(buf->data + buf->len, evt->data, evt->data_len);
      buf->len += evt->data_len;
      buf->data[buf->len] = '\0';
    }
  }
  return ESP_OK;
}

static bool xml_extract_tag(const char *xml, const char *tag, char *out,
                            size_t out_len) {
  char open[64], close[64];
  snprintf(open, sizeof(open), "<%s>", tag);
  snprintf(close, sizeof(close), "</%s>", tag);

  const char *start = strstr(xml, open);
  if (!start)
    return false;
  start += strlen(open);

  const char *end = strstr(start, close);
  if (!end)
    return false;

  size_t len = std::min(static_cast<size_t>(end - start), out_len - 1);
  memcpy(out, start, len);
  out[len] = '\0';
  return true;
}

static bool fetch_speaker_name(const char *location_url, char *name,
                               size_t name_len) {
  static char resp_buf[2048];
  HttpBuf buf = {resp_buf, 0, static_cast<int>(sizeof(resp_buf))};

  esp_http_client_config_t cfg = {};
  cfg.url = location_url;
  cfg.timeout_ms = 2000;
  cfg.event_handler = on_http_data;
  cfg.user_data = &buf;

  auto *client = esp_http_client_init(&cfg);
  if (!client)
    return false;

  esp_err_t err = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || status != 200)
    return false;

  if (xml_extract_tag(resp_buf, "roomName", name, name_len))
    return true;
  if (xml_extract_tag(resp_buf, "friendlyName", name, name_len))
    return true;

  return false;
}

// ─── Zone Group State (coordinator resolution) ──────────────────────────────

static constexpr const char *ZONE_GROUP_TOPOLOGY_PATH =
    "/ZoneGroupTopology/Control";
static constexpr const char *ZONE_GROUP_NS =
    "urn:schemas-upnp-org:service:ZoneGroupTopology:1";
static constexpr const char *GET_ZONE_GROUP_STATE_BODY =
    "<u:GetZoneGroupState xmlns:u=\"urn:schemas-upnp-org:service:ZoneGroupTopology:1\">"
    "<InstanceID>0</InstanceID>"
    "</u:GetZoneGroupState>";

// Extract attribute value from an XML tag string
// e.g. extract_attr(tag, "UUID") from 'UUID="RINCON_xxx"'
static bool extract_attr(const char *xml, const char *attr_name,
                         char *out, size_t out_len) {
  char needle[64];
  snprintf(needle, sizeof(needle), "%s=\"", attr_name);
  const char *start = strstr(xml, needle);
  if (!start) return false;
  start += strlen(needle);
  const char *end = strchr(start, '"');
  if (!end) return false;
  size_t len = std::min(static_cast<size_t>(end - start), out_len - 1);
  memcpy(out, start, len);
  out[len] = '\0';
  return true;
}

// Query GetZoneGroupState and build coordinator-only speaker list.
// Returns true if successful (out->count updated with coordinators).
static bool resolve_coordinators(const char *any_speaker_ip, int port,
                                 DiscoveryResult *out) {
  // Build SOAP envelope
  char envelope[512];
  snprintf(envelope, sizeof(envelope),
    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
    "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\""
    " s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
    "<s:Body>%s</s:Body>"
    "</s:Envelope>", GET_ZONE_GROUP_STATE_BODY);

  char url[80];
  snprintf(url, sizeof(url), "http://%s:%d%s", any_speaker_ip, port,
           ZONE_GROUP_TOPOLOGY_PATH);

  // Response buffer — ZoneGroupState can be large
  static char resp_buf[8192];
  HttpBuf buf = {resp_buf, 0, static_cast<int>(sizeof(resp_buf))};

  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.method = HTTP_METHOD_POST;
  cfg.timeout_ms = 3000;
  cfg.event_handler = on_http_data;
  cfg.user_data = &buf;

  auto *client = esp_http_client_init(&cfg);
  if (!client) return false;

  esp_http_client_set_header(client, "Content-Type",
                             "text/xml; charset=\"utf-8\"");
  char soap_action[128];
  snprintf(soap_action, sizeof(soap_action), "\"%s#GetZoneGroupState\"",
           ZONE_GROUP_NS);
  esp_http_client_set_header(client, "SOAPAction", soap_action);
  esp_http_client_set_post_field(client, envelope,
                                 static_cast<int>(strlen(envelope)));

  esp_err_t err = esp_http_client_perform(client);
  int status = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  if (err != ESP_OK || status != 200) {
    ESP_LOGW(TAG, "GetZoneGroupState failed: err=%s status=%d",
             esp_err_to_name(err), status);
    return false;
  }

  ESP_LOGD(TAG, "ZoneGroupState: %.500s", resp_buf);

  // Parse ZoneGroup entries — each has a Coordinator="RINCON_xxx" attribute
  // Find each ZoneGroup and extract the coordinator's Location and ZoneName
  memset(out, 0, sizeof(*out));

  const char *zg = resp_buf;
  while ((zg = strstr(zg, "ZoneGroup ")) != nullptr &&
         out->count < DISCOVERY_MAX_SPEAKERS) {
    // Extract Coordinator UUID from ZoneGroup tag
    char coord_uuid[64] = {};
    if (!extract_attr(zg, "Coordinator", coord_uuid, sizeof(coord_uuid))) {
      zg++;
      continue;
    }

    // Find the ZoneGroupMember with this UUID (the coordinator)
    // Search within this ZoneGroup (up to next </ZoneGroup>)
    const char *zg_end = strstr(zg, "</ZoneGroup>");
    if (!zg_end) zg_end = resp_buf + buf.len;

    // Look for the member matching the coordinator UUID
    const char *member = zg;
    bool found = false;
    while (member < zg_end &&
           (member = strstr(member, "ZoneGroupMember ")) != nullptr &&
           member < zg_end) {
      char uuid[64] = {};
      if (extract_attr(member, "UUID", uuid, sizeof(uuid)) &&
          strcmp(uuid, coord_uuid) == 0) {
        // This is the coordinator member — extract Location and ZoneName
        char location[256] = {};
        char zone_name[64] = {};

        extract_attr(member, "Location", location, sizeof(location));
        extract_attr(member, "ZoneName", zone_name, sizeof(zone_name));

        // Extract IP from Location URL
        char ip[40] = {};
        uint16_t member_port = 1400;
        if (location[0] && extract_ip_from_url(location, ip, sizeof(ip), &member_port)) {
          auto &speaker = out->speakers[out->count];
          strncpy(speaker.ip, ip, sizeof(speaker.ip) - 1);
          speaker.port = member_port;
          if (zone_name[0]) {
            strncpy(speaker.name, zone_name, sizeof(speaker.name) - 1);
          } else {
            snprintf(speaker.name, sizeof(speaker.name), "Sonos (%s)", ip);
          }
          ESP_LOGI(TAG, "Coordinator: %s at %s:%d (UUID: %s)",
                   speaker.name, speaker.ip, speaker.port, coord_uuid);
          out->count++;
          found = true;
        }
        break;
      }
      member++;
    }

    if (!found) {
      ESP_LOGW(TAG, "Could not resolve coordinator UUID: %s", coord_uuid);
    }

    zg = zg_end;
  }

  return out->count > 0;
}

// ─── SSDP Multicast ─────────────────────────────────────────────────────────────────────

static int send_msearch(int sock) {
  struct sockaddr_in dest = {};
  dest.sin_family = AF_INET;
  dest.sin_port = htons(SSDP_PORT);
  inet_aton(SSDP_ADDR, &dest.sin_addr);

  int sent = sendto(sock, MSEARCH, strlen(MSEARCH), 0,
                    reinterpret_cast<struct sockaddr *>(&dest), sizeof(dest));
  if (sent < 0) {
    ESP_LOGE(TAG, "sendto failed: errno %d", errno);
    return -1;
  }
  return sent;
}

// ─── Public API ─────────────────────────────────────────────────────────────────────

void discovery_init() { ESP_LOGI(TAG, "SSDP discovery ready"); }

int discovery_scan(DiscoveryResult *out, int timeout_ms) {
  memset(out, 0, sizeof(*out));

  int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock < 0) {
    ESP_LOGE(TAG, "socket failed: errno %d", errno);
    return 0;
  }

  int reuse = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  struct timeval tv = {};
  tv.tv_sec = timeout_ms / 1000;
  tv.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  if (send_msearch(sock) < 0) {
    close(sock);
    return 0;
  }

  ESP_LOGI(TAG, "Scanning for Sonos speakers...");

  // Phase 1: SSDP — find at least one speaker on the network
  char recv_buf[1024];
  struct sockaddr_in src_addr;
  socklen_t addr_len = sizeof(src_addr);

  // Temporary list of raw SSDP responses (before coordinator resolution)
  char first_ip[40] = {};
  uint16_t first_port = 1400;

  int64_t start = esp_timer_get_time();
  int64_t deadline = start + static_cast<int64_t>(timeout_ms) * 1000;

  while (esp_timer_get_time() < deadline) {
    int len =
        recvfrom(sock, recv_buf, sizeof(recv_buf) - 1, 0,
                 reinterpret_cast<struct sockaddr *>(&src_addr), &addr_len);
    if (len <= 0)
      break;

    recv_buf[len] = '\0';

    char location[256];
    if (!parse_location(recv_buf, location, sizeof(location)))
      continue;

    char ip[40];
    uint16_t port = 1400;
    if (!extract_ip_from_url(location, ip, sizeof(ip), &port))
      continue;

    if (first_ip[0] == '\0') {
      strncpy(first_ip, ip, sizeof(first_ip) - 1);
      first_port = port;
      ESP_LOGI(TAG, "SSDP: found speaker at %s:%d — querying zone groups", ip, port);
      break; // One speaker is enough to query zone state
    }
  }

  close(sock);

  if (first_ip[0] == '\0') {
    ESP_LOGW(TAG, "No speakers found via SSDP");
    return 0;
  }

  // Phase 2: Query GetZoneGroupState to find all coordinators
  if (resolve_coordinators(first_ip, first_port, out)) {
    ESP_LOGI(TAG, "Zone group resolution: %d coordinator(s) found", out->count);
    return out->count;
  }

  // Fallback: if zone group query fails, use SSDP result with device name
  ESP_LOGW(TAG, "Zone group query failed — falling back to SSDP result");
  auto &speaker = out->speakers[0];
  strncpy(speaker.ip, first_ip, sizeof(speaker.ip) - 1);
  speaker.port = first_port;

  char fallback_url[256];
  snprintf(fallback_url, sizeof(fallback_url), "http://%s:%d/xml/device_description.xml",
           first_ip, first_port);
  if (!fetch_speaker_name(fallback_url, speaker.name, sizeof(speaker.name))) {
    snprintf(speaker.name, sizeof(speaker.name), "Sonos (%s)", first_ip);
  }

  out->count = 1;
  ESP_LOGI(TAG, "Fallback: %s at %s:%d", speaker.name, speaker.ip, speaker.port);
  return out->count;
}
