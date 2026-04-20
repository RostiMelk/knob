#!/usr/bin/env python3
"""Parse a downloaded iCal file and show the next 7 days of events.

Usage:
    curl -sL "https://calendar.google.com/calendar/ical/..." -o /tmp/cal.ics
    python3 peek_events.py /tmp/cal.ics
"""

import re
import sys
from datetime import datetime, timedelta, timezone

OWNER_EMAIL = "rosti@sanity.io"
DAYS_AHEAD = 7


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else "/tmp/cal.ics"
    with open(path, "r", errors="replace") as f:
        raw = f.read()

    # Unfold continuation lines (RFC 5545)
    raw = re.sub(r"\r\n[ \t]", "", raw)
    raw = re.sub(r"\n[ \t]", "", raw)

    now = datetime.now(timezone.utc)
    end_window = now + timedelta(days=DAYS_AHEAD)

    events = []
    for match in re.finditer(r"BEGIN:VEVENT\n(.*?)END:VEVENT", raw, re.DOTALL):
        block = match.group(1)

        # Skip recurring events
        if "RRULE:" in block:
            continue

        # Skip cancelled
        status_m = re.search(r"^STATUS:(.+)$", block, re.MULTILINE)
        if status_m and status_m.group(1).strip() == "CANCELLED":
            continue

        # Skip declined by owner
        declined = False
        for att_m in re.finditer(r"^ATTENDEE(.*?):(.+)$", block, re.MULTILINE):
            params, val = att_m.group(1), att_m.group(2).strip()
            if "PARTSTAT=DECLINED" in params and OWNER_EMAIL in val:
                declined = True
                break
        if declined:
            continue

        # Parse DTSTART
        dt_m = re.search(r"^DTSTART[^:]*:(\S+)$", block, re.MULTILINE)
        if not dt_m:
            continue
        dtval = dt_m.group(1).strip()

        all_day = False
        if len(dtval) == 8:
            dt = datetime.strptime(dtval, "%Y%m%d").replace(tzinfo=timezone.utc)
            all_day = True
        elif dtval.endswith("Z"):
            dt = datetime.strptime(dtval, "%Y%m%dT%H%M%SZ").replace(tzinfo=timezone.utc)
        else:
            # Local time with TZID — assume CEST (UTC+2) as rough approx
            dt = datetime.strptime(dtval[:15], "%Y%m%dT%H%M%S").replace(
                tzinfo=timezone.utc
            ) - timedelta(hours=2)

        if dt < now or dt > end_window:
            continue

        # Summary
        summary_m = re.search(r"^SUMMARY:(.+)$", block, re.MULTILINE)
        summary = summary_m.group(1).strip() if summary_m else "(no title)"
        summary = summary.replace("\\,", ",").replace("\\;", ";").replace("\\n", " ")

        # Parse DTEND for display
        end_str = ""
        dte_m = re.search(r"^DTEND[^:]*:(\S+)$", block, re.MULTILINE)
        if dte_m:
            etval = dte_m.group(1).strip()
            if len(etval) == 8:
                end_str = "all day"
            elif etval.endswith("Z"):
                edt = datetime.strptime(etval, "%Y%m%dT%H%M%SZ").replace(
                    tzinfo=timezone.utc
                )
                end_str = edt.strftime("%H:%M UTC")
            else:
                edt = datetime.strptime(etval[:15], "%Y%m%dT%H%M%S")
                end_str = edt.strftime("%H:%M")

        events.append((dt, summary, all_day, end_str))

    events.sort(key=lambda e: e[0])

    print(
        f"Found {len(events)} events in the next {DAYS_AHEAD} days "
        f"(filtered: no declined, cancelled, or recurring)\n"
    )

    current_day = None
    for dt, title, all_day, end_str in events:
        day_str = dt.strftime("%a %d %b")
        if day_str != current_day:
            current_day = day_str
            print(f"── {day_str} ──")
        if all_day:
            print(f"  ALL DAY     {title}")
        else:
            print(f"  {dt.strftime('%H:%M')} UTC → {end_str:>9s}   {title}")

    if not events:
        print("  (no events)")


if __name__ == "__main__":
    main()
