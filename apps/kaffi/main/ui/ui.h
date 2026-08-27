#pragma once

#include "app_config.h"

/// Initialize the display + UI. Call once at boot.
void ui_init();

/// Show the splash ("kaffi") screen.
void ui_show_splash();

/// Show a status message (connecting, errors, …).
void ui_set_status(const char *msg);

/// Replace the cached leaderboard and refresh the view.
/// Safe to call from any task (locks the display internally).
void ui_update_people(const KaffiState *state);

/// Hand a decoded avatar (RGB565_SWAPPED) to the UI, matched to a person by
/// image URL (stable across reordering). pixels=null marks a failed fetch.
/// The UI takes ownership of `pixels` and frees it. Safe to call from any task.
void ui_set_avatar(const char *url, unsigned char *pixels, int w, int h);
