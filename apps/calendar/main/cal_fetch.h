#pragma once

#include <cstddef>

// Initialize the fetcher (call once after WiFi is connected).
void cal_fetch_init();

// Fetch the iCal feed synchronously. Returns a PSRAM-allocated buffer
// with the response body, or nullptr on failure. Caller must free with
// heap_caps_free(). out_len receives the body length.
// The URL comes from Kconfig: CONFIG_CALENDAR_ICAL_URL
char *cal_fetch_ical(size_t *out_len);
