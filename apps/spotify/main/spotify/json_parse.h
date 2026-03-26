#pragma once

#include <cstddef>

/// HTTP response accumulator — used by both auth and api modules.
struct Response {
    char *buf;
    int   len;
    int   cap;
};

/// ESP HTTP client event handler that accumulates response data into a Response.
#include "esp_http_client.h"
esp_err_t on_http_event(esp_http_client_event_t *evt);

/// Find the value position for a JSON key. Returns pointer after the colon,
/// or nullptr if not found.
const char *json_find_key(const char *json, const char *key);

/// Extract a JSON string value. Returns true if found.
bool json_str(const char *json, const char *key, char *out, size_t out_len);

/// Extract a JSON integer value. Returns true if found.
bool json_int(const char *json, const char *key, int *out);

/// Extract a JSON boolean value. Returns true if found.
bool json_bool(const char *json, const char *key, bool *out);

/// Find the LAST occurrence of a string key in a JSON region.
/// Useful for extracting track "name" after nested album/artist names.
bool json_str_last(const char *json, const char *key, char *out, size_t out_len);
