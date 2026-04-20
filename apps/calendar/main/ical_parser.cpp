#include "ical_parser.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

static constexpr const char *TAG = "ical";

// ---------------------------------------------------------------------------
// Line unfolding
// ---------------------------------------------------------------------------

// Unfold RFC 5545 continuation lines into a new PSRAM buffer.
// Normalises CRLF to LF.  Caller must heap_caps_free() the result.
static char *unfold(const char *data, size_t len, size_t &out_len) {
  char *buf = static_cast<char *>(heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM));
  if (!buf) {
    ESP_LOGE(TAG, "PSRAM alloc failed (%zu bytes)", len + 1);
    return nullptr;
  }

  size_t j = 0;
  for (size_t i = 0; i < len;) {
    const bool crlf = (data[i] == '\r' && i + 1 < len && data[i + 1] == '\n');
    const bool lf = (data[i] == '\n');

    if (crlf || lf) {
      const size_t next = i + (crlf ? 2 : 1);
      if (next < len && (data[next] == ' ' || data[next] == '\t')) {
        // Folded continuation — skip line break + leading whitespace
        i = next + 1;
      } else {
        buf[j++] = '\n';
        i = next;
      }
    } else {
      buf[j++] = data[i++];
    }
  }
  buf[j] = '\0';
  out_len = j;
  return buf;
}

// ---------------------------------------------------------------------------
// Property helpers
// ---------------------------------------------------------------------------

// Return everything after the first ':' (the property value).
static constexpr std::string_view prop_value(std::string_view line) {
  const auto c = line.find(':');
  return (c != std::string_view::npos) ? line.substr(c + 1)
                                       : std::string_view{};
}

// Return the parameter string between the first ';' and the first ':'.
// e.g.  "DTSTART;TZID=Europe/Oslo:20250115T110000"  →  "TZID=Europe/Oslo"
static constexpr std::string_view prop_params(std::string_view line) {
  const auto semi = line.find(';');
  const auto colon = line.find(':');
  if (semi == std::string_view::npos || colon == std::string_view::npos ||
      semi > colon) {
    return {};
  }
  return line.substr(semi + 1, colon - semi - 1);
}

// Check whether a line begins with one of the expected property tokens.
// Matches "NAME:" or "NAME;" to avoid false hits on longer names.
static constexpr bool is_prop(std::string_view line, std::string_view name) {
  if (line.size() <= name.size())
    return false;
  if (!line.starts_with(name))
    return false;
  const char after = line[name.size()];
  return after == ':' || after == ';';
}

// ---------------------------------------------------------------------------
// Summary unescape
// ---------------------------------------------------------------------------

// Copy value → dst, unescaping  \,  \;  \n  \N  \\.
// Strips 3+ byte UTF-8 sequences (emojis, CJK, symbols ≥ U+0800) since
// the display font only covers Latin.  Trims leading whitespace left
// behind by stripped emojis.  Clamps output to dst_len-1 chars + NUL.
static void unescape_summary(std::string_view src, char *dst, size_t dst_len) {
  size_t j = 0;
  const size_t limit = dst_len - 1;

  for (size_t i = 0; i < src.size() && j < limit; ++i) {
    const auto c = static_cast<unsigned char>(src[i]);

    // Skip 3-byte UTF-8 sequences (U+0800..U+FFFF, includes ⏰ U+23F0)
    if ((c & 0xF0) == 0xE0) {
      i += 2;
      continue;
    }
    // Skip 4-byte UTF-8 sequences (U+10000+, includes 🍕🙌 etc.)
    if ((c & 0xF8) == 0xF0) {
      i += 3;
      continue;
    }

    if (src[i] == '\\' && i + 1 < src.size()) {
      const char next = src[i + 1];
      if (next == 'n' || next == 'N') {
        dst[j++] = ' '; // newline → space for single-line display
      } else if (next == ',' || next == ';' || next == '\\') {
        dst[j++] = next;
      } else {
        // Unknown escape — keep literal backslash + char
        dst[j++] = src[i];
        if (j < limit)
          dst[j++] = next;
      }
      ++i; // consumed the character after backslash
    } else {
      dst[j++] = src[i];
    }
  }
  dst[j] = '\0';

  // Trim leading whitespace (left behind when a leading emoji is stripped)
  size_t start = 0;
  while (dst[start] == ' ')
    ++start;
  if (start > 0) {
    memmove(dst, dst + start, j - start + 1);
  }
}

// ---------------------------------------------------------------------------
// Datetime parsing
// ---------------------------------------------------------------------------

// Parse `count` decimal digits starting at s[off].  Returns false on
// out-of-bounds or non-digit characters.
static bool parse_digits(std::string_view s, size_t off, size_t count,
                         int &out) {
  if (off + count > s.size())
    return false;
  int v = 0;
  for (size_t i = 0; i < count; ++i) {
    const char c = s[off + i];
    if (c < '0' || c > '9')
      return false;
    v = v * 10 + (c - '0');
  }
  out = v;
  return true;
}

// Parse an iCal datetime value + params into a time_t.
//   params — the segment between ';' and ':'  (may be empty)
//   value  — the segment after ':'
//
// Formats handled:
//   20250115T100000Z          — UTC datetime
//   20250115T110000           — local datetime (TZID; uses device TZ)
//   20250115                  — all-day date (VALUE=DATE)
static bool parse_datetime(std::string_view params, std::string_view value,
                           time_t &out, bool &all_day) {
  all_day = false;

  // Detect DATE-only via explicit parameter or value length
  bool is_date = false;
  if (params.find("VALUE=DATE") != std::string_view::npos &&
      params.find("VALUE=DATE-TIME") == std::string_view::npos) {
    is_date = true;
  }
  if (value.size() == 8) {
    is_date = true;
  }

  int Y, M, D;
  if (!parse_digits(value, 0, 4, Y))
    return false;
  if (!parse_digits(value, 4, 2, M))
    return false;
  if (!parse_digits(value, 6, 2, D))
    return false;

  if (is_date) {
    all_day = true;
    struct tm t{};
    t.tm_year = Y - 1900;
    t.tm_mon = M - 1;
    t.tm_mday = D;
    t.tm_isdst = -1;
    out = mktime(&t);
    return out != static_cast<time_t>(-1);
  }

  // Expect 'T' separator at index 8 and at least HHMMSS after it
  if (value.size() < 15 || value[8] != 'T')
    return false;

  int h, m, s;
  if (!parse_digits(value, 9, 2, h))
    return false;
  if (!parse_digits(value, 11, 2, m))
    return false;
  if (!parse_digits(value, 13, 2, s))
    return false;

  const bool utc = (value.size() > 15 && value[15] == 'Z');

  struct tm t{};
  t.tm_year = Y - 1900;
  t.tm_mon = M - 1;
  t.tm_mday = D;
  t.tm_hour = h;
  t.tm_min = m;
  t.tm_sec = s;

  if (utc) {
    // No timegm on ESP-IDF newlib. Use mktime (local TZ) and adjust:
    // mktime treats the struct as local time, so we compute the UTC
    // offset and correct for it.
    t.tm_isdst = -1;
    time_t local_interp = mktime(&t);
    if (local_interp == static_cast<time_t>(-1))
      return false;
    struct tm gm{};
    gmtime_r(&local_interp, &gm);
    gm.tm_isdst = -1;
    time_t gm_as_local = mktime(&gm);
    out = local_interp + (local_interp - gm_as_local);
  } else {
    // TZID datetime — interpret in the device's configured local timezone.
    // Not perfect if TZID differs, but sufficient for V1.
    t.tm_isdst = -1;
    out = mktime(&t);
  }
  return out != static_cast<time_t>(-1);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

int ical_parse(const char *ical_data, size_t data_len, CalEvent *events,
               int max_events, time_t after) {
  if (!ical_data || data_len == 0 || !events || max_events <= 0)
    return 0;

  // Unfold continuation lines into a PSRAM work buffer
  size_t buf_len = 0;
  char *buf = unfold(ical_data, data_len, buf_len);
  if (!buf)
    return 0;

  const std::string_view text(buf, buf_len);
  int count = 0;

  // Extract calendar owner email from X-WR-CALNAME (Google puts the
  // owner's email there).  We need this to know which ATTENDEE line
  // is ours when checking PARTSTAT=DECLINED.
  char owner_email[128] = {};
  {
    const auto key = std::string_view("X-WR-CALNAME:");
    auto p = text.find(key);
    if (p != std::string_view::npos) {
      p += key.size();
      auto nl = text.find('\n', p);
      auto val = text.substr(p, nl != std::string_view::npos ? nl - p : 0);
      if (!val.empty() && val.back() == '\r')
        val.remove_suffix(1);
      size_t n = std::min(val.size(), sizeof(owner_email) - 1);
      memcpy(owner_email, val.data(), n);
      owner_email[n] = '\0';
      ESP_LOGI(TAG, "Calendar owner: %s", owner_email);
    }
  }

  // Per-VEVENT state
  bool in_event = false;
  bool has_rrule = false;
  bool has_start = false;
  bool has_end = false;
  bool cancelled = false;
  bool declined = false;
  bool spam_organizer = false;
  CalEvent cur{};

  // Attempt to commit the current event into the output array.
  auto finish_event = [&]() {
    if (!has_start || has_rrule || cancelled || declined || spam_organizer)
      return;

    if (!has_end) {
      // RFC 5545 default: P1D for DATE, otherwise we use 1 hour as
      // specified in the requirements.
      cur.end = cur.start + (cur.all_day ? 86400 : 3600);
    }

    // Filter out events that have already ended
    if (cur.end >= after && count < max_events) {
      events[count++] = cur;
    }
  };

  size_t pos = 0;
  while (pos < text.size()) {
    const size_t eol = text.find('\n', pos);
    const size_t end = (eol != std::string_view::npos) ? eol : text.size();
    std::string_view line = text.substr(pos, end - pos);
    pos = end + 1;

    // Strip stray trailing CR (shouldn't exist after unfold, but be safe)
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    if (line.empty())
      continue;

    // --- Boundaries ---
    if (line == "BEGIN:VEVENT") {
      in_event = true;
      has_rrule = false;
      has_start = false;
      has_end = false;
      cancelled = false;
      declined = false;
      spam_organizer = false;
      cur = CalEvent{};
      continue;
    }
    if (line == "END:VEVENT") {
      if (in_event)
        finish_event();
      in_event = false;
      if (count >= max_events)
        break;
      continue;
    }

    if (!in_event)
      continue;

    // --- Properties inside VEVENT ---
    if (is_prop(line, "RRULE")) {
      has_rrule = true;

    } else if (is_prop(line, "ORGANIZER")) {
      // Filter calendar invite spam from a specific iCloud user ID.
      // All 108 spam events share this encoded iCloud account prefix.
      if (line.find("2_ge4tanjyga4tgmrugaytsmbvh") != std::string_view::npos)
        spam_organizer = true;

    } else if (is_prop(line, "STATUS")) {
      if (prop_value(line) == "CANCELLED")
        cancelled = true;

    } else if (is_prop(line, "ATTENDEE")) {
      // Only mark declined when the calendar owner's ATTENDEE line
      // has PARTSTAT=DECLINED.  The value is typically
      // "mailto:user@example.com" — match against owner_email
      // extracted from X-WR-CALNAME.
      const auto params = prop_params(line);
      if (params.find("PARTSTAT=DECLINED") != std::string_view::npos) {
        if (owner_email[0] == '\0') {
          // No owner info — be conservative, mark declined
          declined = true;
        } else {
          const auto val = prop_value(line);
          if (val.find(owner_email) != std::string_view::npos)
            declined = true;
        }
      }

    } else if (is_prop(line, "SUMMARY")) {
      unescape_summary(prop_value(line), cur.summary, sizeof(cur.summary));

    } else if (is_prop(line, "DTSTART")) {
      bool ad = false;
      if (parse_datetime(prop_params(line), prop_value(line), cur.start, ad)) {
        has_start = true;
        cur.all_day = ad;
      } else {
        ESP_LOGW(TAG, "Bad DTSTART: %.*s", static_cast<int>(line.size()),
                 line.data());
      }

    } else if (is_prop(line, "DTEND")) {
      bool ad = false;
      if (parse_datetime(prop_params(line), prop_value(line), cur.end, ad)) {
        has_end = true;
      } else {
        ESP_LOGW(TAG, "Bad DTEND: %.*s", static_cast<int>(line.size()),
                 line.data());
      }
    }
  }

  heap_caps_free(buf);

  // Sort by start time ascending
  std::sort(events, events + count, [](const CalEvent &a, const CalEvent &b) {
    return a.start < b.start;
  });

  ESP_LOGI(TAG, "Parsed %d event(s) (after filter)", count);
  return count;
}
