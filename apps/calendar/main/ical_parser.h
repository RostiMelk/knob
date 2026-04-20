#pragma once

#include <cstddef>
#include <ctime>

/// A single calendar event parsed from iCal data.
struct CalEvent {
  char summary[128]; // Event title (null-terminated, max 127 chars)
  time_t start;      // Start time (UTC epoch seconds)
  time_t end;        // End time (UTC epoch seconds)
  bool all_day;      // True if DATE-only (no time component)
};

/// Parse iCal text buffer into an array of events.
///
/// @param ical_data  Raw iCal text (not modified).
/// @param data_len   Length of ical_data in bytes.
/// @param events     Output array to fill with parsed events.
/// @param max_events Capacity of the events array.
/// @param after      Only include events whose end time >= this value
///                   (i.e., skip events that are already over).
/// @return Number of events written to the array (0..max_events).
///         Events are sorted by start time ascending.
int ical_parse(const char *ical_data, size_t data_len, CalEvent *events,
               int max_events, time_t after);
