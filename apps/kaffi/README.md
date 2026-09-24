# Kaffi

Office coffee tracker that runs on the knob. Turn the encoder to pick a person, tap to log
that they **brewed a pot** or **took a cup**. Every action is an immutable `coffeeEvent` in
Sanity; the knob derives balances, streaks, and a leaderboard from the event log on each
fetch — no stored counters.

## Two parts

| Part         | Path                 | What it is                                                 |
| ------------ | -------------------- | ---------------------------------------------------------- |
| **Firmware** | `apps/kaffi/`        | The ESP-IDF app you flash to the knob (this is "the app"). |
| **Studio**   | `apps/kaffi/studio/` | Sanity Studio — schema, admin, and a leaderboard tool.     |

## Two Sanity projects

- **People** come live from the company directory (Sanity Home, project
  `r3dzy7he`), filtered by office location — nobody maintains a people list by hand.
  Needs a read token (`CONFIG_KAFFI_DIR_TOKEN`).
- **Events** live in kaffi's own project (`j7764jbe`) as `coffeeEvent` documents with a
  `personId` string snapshot of the directory `_id` (plus a denormalized `personName` for
  Studio readability). Needs a write token (`CONFIG_KAFFI_SANITY_TOKEN`).

Events for the current year are cursor-paginated and aggregated on-device.

## Multi-office

One binary serves every office. On first boot the knob asks which office it lives in
(stored in NVS); that choice drives the directory location filter (GROQ `match`, so
free-text locations like full addresses still match), the `office` stamp on every event,
and the timezone for streaks. Each office gets its own balances, streaks, and
leaderboard. Offices are defined in `main/app_config.h` (`KAFFI_OFFICES`) — adding one
is a table row + a release. Events without an `office` predate multi-office and count as
Oslo.

## Scoring

```
balance = brew credit − cups taken
```

The first pot of a brew is worth **4** cups, a second pot in the same log **+2** (barely
more hassle once you're standing there). Taking a cup is **−1**. Positive = hero,
negative = freeloader. Constants live in `main/app_config.h` and are mirrored by the
Studio's leaderboard tool.

**Streaks** count consecutive weekdays with at least one brew — quiet weekends don't
break them, and only brewing counts (it rewards making coffee, not drinking it). The
avatar gets a flame pill with the day count; an unsaved streak on a weekday shows an
inverted "SAVE DAY N" state.

## On-device UX

```
LIST         avatar + name + "POTS n  CUPS m" + signed balance + streak pill
             encoder → change person (wraps) · tap → ACTION · long-press → LEADERBOARD
ACTION       [ Took a cup ]  [ Brewed a pot ]  [ Back ]
POTS         [ 1 pot ]  [ 2 pots ]  [ Back ]
LEADERBOARD  yearly, balance desc, medals top 3, freeloader row red,
             zero-entry people hidden, auto-exits after 12 s
```

Writes buzz, toast ("Fresh pot!", "Day 5 streak!"), and reflect optimistically before the
refetch lands. Avatars are fetched once, decoded JPEG → RGB565, and cached in PSRAM.

WiFi blips don't take over the screen once people are loaded — the knob keeps working
from memory and refetches quietly on reconnect.

## Setting up a new knob

Flash any kaffi binary (USB once, OTA thereafter). On first boot the knob:

1. Asks which office it's in (tap on screen)
2. Shows a QR code — scan it with a phone to join the `kaffi` hotspot, a captive
   portal opens, pick the office WiFi and enter its password

That's it. Credentials live in NVS, so a generic CI-built binary works anywhere —
flash it in Oslo, mail it to SF.

### Dev builds

```bash
cp apps/kaffi/sdkconfig.defaults.local.template apps/kaffi/sdkconfig.defaults.local
# fill in both Sanity tokens (and optionally WiFi to skip the portal)

./test.sh kaffi          # build + lint (needs ESP-IDF sourced)
./flash.sh kaffi -m      # build, flash, monitor
```

Tokens are compiled into firmware; on-device exposure is accepted for this internal
device.

## OTA releases

Tag a release and CI does the rest:

```bash
git tag kaffi-v0.2.0 && git push --tags
```

The `release-kaffi` workflow builds the firmware (version from the tag), uploads
`kaffi.bin` as a Sanity asset, and publishes a `firmwareRelease` document. Every knob
polls it hourly (and after each boot), updates when the published version is newer, and
reboots into the new image. A fresh image must complete a successful people fetch to
mark itself valid — otherwise the bootloader rolls back to the previous partition, so a
bad release can't brick a device an ocean away.

Requires `KAFFI_SANITY_TOKEN` and `KAFFI_DIR_TOKEN` as GitHub Actions secrets.

## Studio

Hosted at <https://kaffi.sanity.studio/>, bun-managed:

```bash
cd apps/kaffi/studio
bun install
bunx sanity dev      # http://localhost:3333
bunx sanity deploy
```

- `coffeeEvent` is liveEdit (the device writes published docs directly)
- **Leaderboard** tool mirrors the device's scoring, with names/avatars resolved live
  from the directory (snapshot fallback)
- `personId` fields render with a custom input that resolves the person against the
  directory and flags name drift

The directory client reuses the Studio session token; the Studio origin must be a CORS
origin on the directory project.

## Layout

```
apps/kaffi/
  version.txt                firmware version (CI overwrites from the release tag)
  main/
    main.cpp                 boot, event loop, WiFi wiring, cmd/avatar tasks
    app_config.h             event IDs, offices, Person/KaffiState, scoring constants
    kaffi/sanity_api.*       HTTPS to both projects, JSON parse, aggregation, streaks
    kaffi/prefs.*            NVS-backed office choice
    kaffi/ota.*              firmwareRelease poll + esp_https_ota + rollback guard
    ui/ui.*                  LVGL state machine (setup/list/action/pots/leaderboard)
    ui/images/               generated LVGL assets (flame icon)
  studio/                    Sanity Studio (schema, leaderboard tool, directory client)
```
