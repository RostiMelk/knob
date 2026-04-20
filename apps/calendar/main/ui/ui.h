#pragma once

#include "ical_parser.h"
#include "weather.h"

void ui_init();

// Update the event list displayed in the UI.
// events: array of parsed calendar events (sorted by start time).
// count: number of events in the array.
// Copies data internally — caller may free after return.
void ui_set_events(const CalEvent *events, int count);

void ui_set_wifi_status(bool connected);
void ui_set_weather(const WeatherData *data);

// Called from main on encoder rotation (steps: +CW / -CCW).
void ui_on_encoder_rotate(int32_t steps);
