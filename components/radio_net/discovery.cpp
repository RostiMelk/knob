#include "discovery.h"
#include "app_config.h"

#include "esp_http_client.h"
#include "esp_log.h"

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

// ─── Helpers ────────────────────────────────────────────────────────────────

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

// ─── Fetch Device Description XML ───────────────────────────────────────────

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

// ─── SSDP Multicast ─────────────────────────────────────────────────────────

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

// ─── Public API ─────────────────────────────────────────────────────────────

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

  char recv_buf[1024];
  struct sockaddr_in src_addr;
  socklen_t addr_len = sizeof(src_addr);

  int64_t start = esp_timer_get_time();
  int64_t deadline = start + static_cast<int64_t>(timeout_ms) * 1000;

  while (esp_timer_get_time() < deadline &&
         out->count < DISCOVERY_MAX_SPEAKERS) {
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

    if (already_found(out, ip))
      continue;

    auto &speaker = out->speakers[out->count];
    strncpy(speaker.ip, ip, sizeof(speaker.ip) - 1);
    speaker.port = port;

    if (!fetch_speaker_name(location, speaker.name, sizeof(speaker.name))) {
      snprintf(speaker.name, sizeof(speaker.name), "Sonos (%s)", ip);
    }

    ESP_LOGI(TAG, "Found: %s at %s:%d", speaker.name, speaker.ip, speaker.port);
    out->count++;
  }

  close(sock);
  ESP_LOGI(TAG, "Scan complete — %d speaker(s) found", out->count);
  return out->count;
}
