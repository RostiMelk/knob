#include "spotify/spotify_auth.h"
#include "spotify/json_parse.h"
#include "app_config.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <cstring>
#include <cstdio>

static constexpr const char *TAG = "spotify_auth";

// From Kconfig
extern "C" {
extern const char *spotify_client_id(void);
extern const char *spotify_refresh_token(void);
}

static char s_access_token[512];
static char s_refresh_token[256]; // may be rotated by Spotify
static int64_t s_expires_at_us = 0; // microseconds since boot

static void save_refresh_token(const char *token) {
    nvs_handle_t h;
    if (nvs_open("spotify", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "refresh_tok", token);
        nvs_commit(h);
        nvs_close(h);
    }
}

static bool load_refresh_token() {
    nvs_handle_t h;
    if (nvs_open("spotify", NVS_READONLY, &h) != ESP_OK) return false;
    size_t len = sizeof(s_refresh_token);
    bool ok = nvs_get_str(h, "refresh_tok", s_refresh_token, &len) == ESP_OK;
    nvs_close(h);
    return ok && s_refresh_token[0] != '\0';
}

bool spotify_auth_refresh() {
    char body[512];
    snprintf(body, sizeof(body),
             "grant_type=refresh_token&refresh_token=%s&client_id=%s",
             s_refresh_token, spotify_client_id());

    char resp_buf[2048] = {};
    Response resp = {resp_buf, 0, (int)sizeof(resp_buf)};

    esp_http_client_config_t cfg = {};
    cfg.url = "https://accounts.spotify.com/api/token";
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = SPOTIFY_HTTP_TIMEOUT;
    cfg.event_handler = on_http_event;
    cfg.user_data = &resp;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    auto *client = esp_http_client_init(&cfg);
    esp_http_client_set_header(client, "Content-Type",
                               "application/x-www-form-urlencoded");
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(TAG, "Token refresh failed: err=%d status=%d", err, status);
        if (resp.len > 0) ESP_LOGE(TAG, "Response: %s", resp.buf);
        return false;
    }

    char token[512] = {};
    int expires_in = 3600;
    json_str(resp.buf, "access_token", token, sizeof(token));
    json_int(resp.buf, "expires_in", &expires_in);

    if (token[0] == '\0') {
        ESP_LOGE(TAG, "No access_token in response");
        return false;
    }

    strncpy(s_access_token, token, sizeof(s_access_token) - 1);
    s_expires_at_us = esp_timer_get_time() +
                      (int64_t)(expires_in - SPOTIFY_TOKEN_MARGIN) * 1000000LL;

    // Spotify may rotate the refresh token — persist the new one
    char new_refresh[256] = {};
    if (json_str(resp.buf, "refresh_token", new_refresh, sizeof(new_refresh))
        && new_refresh[0] != '\0'
        && strcmp(new_refresh, s_refresh_token) != 0) {
        ESP_LOGI(TAG, "Refresh token rotated — saving to NVS");
        strncpy(s_refresh_token, new_refresh, sizeof(s_refresh_token) - 1);
        save_refresh_token(s_refresh_token);
    }

    ESP_LOGI(TAG, "Token refreshed, expires in %ds", expires_in);
    return true;
}

void spotify_auth_init() {
    s_access_token[0] = '\0';
    s_expires_at_us = 0;
    // Load persisted refresh token, fall back to Kconfig default
    if (!load_refresh_token()) {
        strncpy(s_refresh_token, spotify_refresh_token(),
                sizeof(s_refresh_token) - 1);
    }
    ESP_LOGI(TAG, "Auth init (token len=%d)", (int)strlen(s_refresh_token));
}

const char *spotify_auth_get_token() {
    if (s_access_token[0] == '\0' ||
        esp_timer_get_time() >= s_expires_at_us) {
        if (!spotify_auth_refresh()) return nullptr;
    }
    return s_access_token;
}
