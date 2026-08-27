#include "prefs.h"
#include "../app_config.h"

#include "nvs.h"

static constexpr const char *NS = "kaffi";
static constexpr const char *KEY_OFFICE = "office";

int kaffi_office_get() {
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK)
    return -1;
  int32_t idx = -1;
  esp_err_t err = nvs_get_i32(h, KEY_OFFICE, &idx);
  nvs_close(h);
  if (err != ESP_OK || idx < 0 || idx >= KAFFI_OFFICE_COUNT)
    return -1;
  return static_cast<int>(idx);
}

void kaffi_office_set(int index) {
  if (index < 0 || index >= KAFFI_OFFICE_COUNT)
    return;
  nvs_handle_t h;
  if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK)
    return;
  nvs_set_i32(h, KEY_OFFICE, index);
  nvs_commit(h);
  nvs_close(h);
}
