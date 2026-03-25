#pragma once

/// Initialize auth module. Call once after NVS + WiFi.
void spotify_auth_init();

/// Get a valid access token. Refreshes automatically if expired.
/// Returns pointer to internal static buffer, or nullptr on failure.
const char *spotify_auth_get_token();

/// Force a token refresh (e.g. on 401 response).
bool spotify_auth_refresh();
