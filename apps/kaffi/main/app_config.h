#pragma once

#include "knob_events.h"

#include <cstdint>

// ─── Kaffi app event IDs (after the shared KNOB_EVENT_LAST sentinel)
enum : int32_t {
  APP_EVENT_KAFFI_PEOPLE_UPDATE = KNOB_EVENT_LAST + 1, // data: KaffiState*
  APP_EVENT_KAFFI_LOG,                                 // data: KaffiLogCmd
  APP_EVENT_KAFFI_REFRESH,        // no data — request re-fetch
  APP_EVENT_KAFFI_AVATAR_REQUEST, // data: AvatarReq  — UI wants an avatar
  APP_EVENT_KAFFI_AVATAR_READY,   // data: AvatarReady — decoded pixels for UI
};

// ─── Limits
static constexpr int KAFFI_MAX_PEOPLE = 48;
static constexpr int KAFFI_ID_LEN = 40; // Sanity doc IDs are short
static constexpr int KAFFI_NAME_LEN = 48;
static constexpr int KAFFI_URL_LEN = 200;

// ─── Coffee action kinds
enum KaffiKind { KAFFI_CUP = 0, KAFFI_BREW = 1 };

// ─── A person plus their derived yearly stats (from the leaderboard query)
struct Person {
  char id[KAFFI_ID_LEN];
  char name[KAFFI_NAME_LEN];
  char image_url[KAFFI_URL_LEN]; // small JPEG URL, or empty
  int pots;
  int cups;
  int credit;        // cups "paid for" by brews (diminishing per-event returns)
  int streak;        // consecutive brew days; quiet weekends don't break it
  bool brewed_today; // brewed at least one pot today (local time)
};

// ─── Snapshot of all people, posted to the UI on each refresh
struct KaffiState {
  Person people[KAFFI_MAX_PEOPLE];
  int count;
};

// ─── A write request from the UI to the command task
struct KaffiLogCmd {
  char person_id[KAFFI_ID_LEN];
  char
      person_name[KAFFI_NAME_LEN]; // denormalized for readability in the studio
  int32_t kind;                    // KaffiKind
  int32_t quantity;                // pots (1-2) or cups (1)
};

// ─── UI asks the command task to fetch+decode an avatar (keyed by URL, which
// is stable across list reordering — unlike a list index).
struct AvatarReq {
  char url[KAFFI_URL_LEN];
};

// ─── Command task hands a decoded avatar back to the UI (UI takes ownership)
struct AvatarReady {
  char url[KAFFI_URL_LEN];
  uint8_t *pixels; // RGB565_SWAPPED, heap_caps_malloc'd; UI must free (null =
                   // failed)
  int w;
  int h;
};

// ─── Stats
// Fairness balance = credit - cups (positive = hero, negative = freeloader).
// The first pot of a brew event is worth 4 cups; extra pots in the same
// event only 2 each — brewing a second pot is barely more hassle once
// you're already at it.
static constexpr int KAFFI_FIRST_POT_CUPS = 4;
static constexpr int KAFFI_EXTRA_POT_CUPS = 2;

inline int kaffi_brew_credit(int pots) {
  if (pots <= 0)
    return 0;
  return KAFFI_FIRST_POT_CUPS + (pots - 1) * KAFFI_EXTRA_POT_CUPS;
}

// ─── Timing
static constexpr int KAFFI_HTTP_TIMEOUT_MS = 8000;
static constexpr int KAFFI_REFRESH_MS = 30000; // periodic leaderboard refresh
static constexpr int KAFFI_ENCODER_POLL_MS = 20;
// Encoder sensitivity, in half-detents per person move (higher = less
// sensitive). 3 = 1.5 detents per person; 2 = 1 detent; 4 = 2 detents.
static constexpr int KAFFI_ENCODER_HALFSTEPS_PER_ITEM = 2;
static constexpr int KAFFI_TOAST_MS = 1200;
static constexpr int KAFFI_LEADERBOARD_TIMEOUT_MS =
    12000; // auto-exit on inactivity

// ─── Backlight (auto-dimming is disabled; the screen stays at this level)
static constexpr int KAFFI_BACKLIGHT_NORMAL = 80;
