#include "spotify/json_parse.h"

#include <cstring>
#include <cstdio>
#include <cstdlib>

esp_err_t on_http_event(esp_http_client_event_t *evt) {
    auto *r = static_cast<Response *>(evt->user_data);
    if (evt->event_id == HTTP_EVENT_ON_DATA && r && evt->data_len > 0) {
        int needed = r->len + evt->data_len;
        if (needed < r->cap) {
            memcpy(r->buf + r->len, evt->data, evt->data_len);
            r->len = needed;
            r->buf[r->len] = '\0';
        }
    }
    return ESP_OK;
}

const char *json_find_key(const char *json, const char *key) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, pattern)) != nullptr) {
        p += strlen(pattern);
        while (*p == ' ' || *p == '\t') p++;
        if (*p == ':') return p + 1;
    }
    return nullptr;
}

bool json_str(const char *json, const char *key, char *out, size_t out_len) {
    const char *p = json_find_key(json, key);
    if (!p) return false;
    while (*p == ' ') p++;
    if (*p != '"') return false;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < out_len - 1) {
        if (*p == '\\' && *(p + 1)) { p++; } // skip escape
        out[i++] = *p++;
    }
    out[i] = '\0';
    return i > 0;
}

bool json_int(const char *json, const char *key, int *out) {
    const char *p = json_find_key(json, key);
    if (!p) return false;
    while (*p == ' ') p++;
    *out = atoi(p);
    return true;
}

bool json_bool(const char *json, const char *key, bool *out) {
    const char *p = json_find_key(json, key);
    if (!p) return false;
    while (*p == ' ') p++;
    *out = (*p == 't'); // "true" vs "false"
    return true;
}

bool json_str_last(const char *json, const char *key,
                   char *out, size_t out_len) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = json;
    const char *last = nullptr;
    while ((p = strstr(p, pattern)) != nullptr) {
        last = p;
        p += strlen(pattern);
    }
    if (!last) return false;
    return json_str(last, key, out, out_len);
}
