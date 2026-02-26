#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

constexpr int DISCOVERY_MAX_SPEAKERS = 16;
constexpr int DISCOVERY_TIMEOUT_MS = 3000;

struct SonosSpeaker {
  char ip[40];
  char name[64];
  uint16_t port;
};

struct DiscoveryResult {
  SonosSpeaker speakers[DISCOVERY_MAX_SPEAKERS];
  int count;
};

inline void discovery_init() {}

inline int discovery_scan(DiscoveryResult *out, int = DISCOVERY_TIMEOUT_MS) {
  memset(out, 0, sizeof(*out));

  strcpy(out->speakers[0].ip, "192.168.1.10");
  strcpy(out->speakers[0].name, "Living Room");
  out->speakers[0].port = 1400;

  strcpy(out->speakers[1].ip, "192.168.1.11");
  strcpy(out->speakers[1].name, "Kitchen");
  out->speakers[1].port = 1400;

  out->count = 2;
  printf("[sim] discovery_scan() → %d fake speakers\n", out->count);
  return out->count;
}
