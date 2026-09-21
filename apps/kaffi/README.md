# Kaffi

Office coffee tracker that runs on the knob. Turn the encoder to pick a person, tap to log
that they **brewed a pot** or **took a cup**. Every action is an immutable `coffeeEvent` in
Sanity; the knob derives balances, streaks, and a leaderboard from the event log on each
fetch — no stored counters.

## Parts

| Part         | Path                  | What it is                                                     |
| ------------ | --------------------- | -------------------------------------------------------------- |
| **Firmware** | `apps/kaffi/`         | The ESP-IDF app you flash to the knob (this is "the app").     |
| **Studio**   | `apps/kaffi/studio/`  | Sanity Studio — schema, admin, and a leaderboard tool.         |
| **Reports**  | `apps/kaffi/reports/` | Offline PNG generators for coffee stats, rankings, and trends. |

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
shared TypeScript scoring module (`lib/coffee-stats.ts`), used by both the Studio and reports.

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
  lib/coffee-stats.ts         shared TypeScript scoring and standings
  reports/                   PNG renderers, period aggregation, and brand assets
```

## Report images

Three separate **1200 × 800 PNGs** keep each report readable in Slack:

- `renderCoffeeSummary`: totals, participation, and the highest-ranked brewer.
- `renderCoffeeLeaderboard`: top five participants and the last participant by balance, then pots. The final row keeps its actual rank and normal row spacing. A zigzag divider marks skipped participants. Ties share a rank; groups of five or fewer appear once.
- `renderCoffeeTrend`: pots/cups lines with points on one zero-based count axis. Weekdays for a week/month, weeks for a quarter, and months for a year. Weekend activity stays in report totals, rankings, and weekly/monthly buckets. Partial weeks use open points, dashed segments, and an asterisk. These are logged counts, not pot capacity or estimated consumption.

Use a weekly report for the summary and leaderboard, plus a quarter-to-date report
for the trend. Each renderer returns a PNG `Buffer`.
Knut's integration owns fetching, scheduling, uploading, and Slack delivery.
The generators make no network requests and do not write files.

Footers randomly rotate through 16 messages, using all 16 before repeating.
Any three consecutive images have different footers, even across pool refills.
Render each report's images in the same process; the rotation is kept in memory.

```ts
import { buildCoffeeReport } from "./reports/report-data.ts";
import {
  renderCoffeeSummary,
  renderCoffeeLeaderboard,
  renderCoffeeTrend,
} from "./reports/coffee-images.ts";

const options = {
  date: "2026-09-18", // any local calendar date inside the desired period
  through: "2026-09-18", // inclusive cutoff; omit for complete periods
  office: "San Francisco",
  timeZone: "America/Los_Angeles",
  names: new Map(people.map((person) => [person._id, person.name])), // optional
};
// Fetch enough history for both periods; a week can cross a quarter boundary.
const report = buildCoffeeReport(events, { ...options, period: "week" });
const quarter = buildCoffeeReport(events, { ...options, period: "quarter" });

const images = {
  summary: renderCoffeeSummary(report),
  leaderboard: renderCoffeeLeaderboard(report),
  trend: renderCoffeeTrend(quarter),
};
```

Supply each published event once, including `personId`, `personName`, `kind`,
`quantity`, `office`, and `occurredAt`. Fetch all events for the period, including
pagination where needed. Empty buckets mean no supplied activity, so incomplete
queries produce incomplete totals. For an ongoing period, supply `through` to omit
future buckets and label the period "TO DATE". No previous-period comparison is
inferred from missing data.

Weeks run Monday through Sunday. Fiscal years run February through January and
use the ending year: FY27 is February 1, 2026 through January 31, 2027.
Quarters start in February, May, August, and November, so September 2026 is
Q3 FY27. Quarter/year report bounds, buckets, and image labels follow this calendar;
week/month reports use calendar weeks/months.

`from` is inclusive and `to` exclusive; both
are office-local calendar dates, not UTC query timestamps. Event timestamps must
include an offset or `Z`. Office names match exactly; unstamped events belong to
Oslo. Invalid timestamps and non-positive/non-integer quantities throw. Directory
names take precedence, followed by the most recent event's name snapshot.

### Rendering and validation

```bash
cd apps/kaffi/reports
bun install --frozen-lockfile
bun run test
bun run typecheck
bun run format:check
# Optional: save the synthetic test cases as PNGs for visual review.
KAFFI_PREVIEW_DIR=/tmp/kaffi-previews bun run test
```

The server-side TypeScript runs in Bun or a Node TypeScript toolchain. Ship
`reports/assets/` alongside the source and retain the native resvg dependency for
the deployment OS/architecture. The fonts load relative to the module, independent
of the working directory. Do not import the renderers into the Studio/browser.

### Brand sources and rendering choice

Research checked the live [homepage](https://www.sanity.io),
[engineering site](https://www.sanity.io/engineering), and `sanity-io/www-sanity-io`
through GitHub MCP. Source revision: `f462a78cbe4a7ddc0298c70cc80394dbf7eeaf84`.

- [Engineering OG renderer](https://github.com/sanity-io/www-sanity-io/blob/f462a78cbe4a7ddc0298c70cc80394dbf7eeaf84/apps/web-astro/src/lib/engineering-og.ts): Sanity symbol, uppercase headlines, monospace details, and PNG rendering approach.
- [Font assets](https://github.com/sanity-io/www-sanity-io/tree/f462a78cbe4a7ddc0298c70cc80394dbf7eeaf84/apps/web-astro/src/assets/fonts/og): the actual Waldenburg Normal and IBM Plex Mono Regular TTFs. Waldenburg's embedded family name is `KMR Waldenburg`.
- [Texture definitions](https://github.com/sanity-io/www-sanity-io/blob/f462a78cbe4a7ddc0298c70cc80394dbf7eeaf84/apps/web-astro/src/lib/og-textures.ts): the 12 px hex-dot tile, reproduced as an SVG pattern.
- [Engineering palette](https://github.com/sanity-io/www-sanity-io/blob/f462a78cbe4a7ddc0298c70cc80394dbf7eeaf84/apps/web-astro/src/lib/engineering-theme.ts): yellow `#FFFF00`, gray `#D6D6D6`; ink `#0B0B0B`, white, and homepage orange `#FF5500` complete the report palette.

The fonts and symbol remain Sanity brand assets; their inclusion does not grant
font redistribution rights. Keep their existing licensing when moving this code.

Fixed SVG layouts need only `@resvg/resvg-js` 2.6.2, also used by Sanity's OG
renderer. Direct SVG avoids Satori/React layout and a charting library here.
The registry reports a roughly 44 KB JS wrapper and 3.5 MB macOS ARM native
package; binary size differs by platform. Rendering uses bundled fonts with
system-font loading disabled, so output does not depend on the host's fonts.
