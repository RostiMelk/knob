#pragma once

/// Initialize the Sanity API module. Call after WiFi is connected.
void sanity_api_init();

/// Fetch the leaderboard (people + pots/cups) and post
/// APP_EVENT_KAFFI_PEOPLE_UPDATE with the result. Returns true on success.
bool sanity_api_fetch_people();

/// Create a coffeeEvent referencing a directory person by id (person_name is
/// denormalized for readability). `kind` is a KaffiKind (cup/brew).
bool sanity_api_log(const char *person_id, const char *person_name, int kind,
                    int quantity);

/// Fetch a JPEG avatar from `url` and decode it to an RGB565 buffer.
/// On success, *out_pixels is a PSRAM buffer the caller must free.
bool sanity_api_fetch_avatar(const char *url, unsigned char **out_pixels,
                             int *w, int *h);
