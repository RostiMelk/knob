#pragma once

#include <cstdint>

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

void discovery_init();
int discovery_scan(DiscoveryResult *out, int timeout_ms = DISCOVERY_TIMEOUT_MS);
