#pragma once

#include <cstdio>

#define ESP_LOGE(tag, fmt, ...) printf("[E][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("[W][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) printf("[I][%s] " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)

#define ESP_ERROR_CHECK(x)                                                     \
  do {                                                                         \
    int __err = (x);                                                           \
    if (__err) {                                                               \
      printf("[FATAL] ESP_ERROR_CHECK failed: %d at %s:%d\n", __err, __FILE__, \
             __LINE__);                                                        \
    }                                                                          \
  } while (0)
