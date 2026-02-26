#pragma once

#include <cstdint>
#include <cstdio>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

typedef const char *esp_event_base_t;

typedef void (*esp_event_handler_t)(void *handler_arg, esp_event_base_t base,
                                    int32_t id, void *event_data);

#define ESP_EVENT_DECLARE_BASE(name) extern esp_event_base_t name
#define ESP_EVENT_DEFINE_BASE(name) esp_event_base_t name = #name

#define ESP_EVENT_ANY_ID -1

inline esp_err_t esp_event_loop_create_default() { return ESP_OK; }

inline esp_err_t esp_event_handler_register(esp_event_base_t, int32_t,
                                            esp_event_handler_t, void *) {
  return ESP_OK;
}

inline esp_err_t esp_event_post(esp_event_base_t base, int32_t id,
                                const void *data, size_t size,
                                uint32_t ticks_to_wait) {
  (void)base;
  (void)id;
  (void)data;
  (void)size;
  (void)ticks_to_wait;
  return ESP_OK;
}
