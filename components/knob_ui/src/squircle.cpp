#include "squircle.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <cmath>
#include <cstring>

static constexpr const char *TAG = "squircle";

static uint8_t *s_mask_buf;
static lv_image_dsc_t s_mask_dsc;
static bool s_ready;

void squircle_mask_init(int size, float n) {
  if (s_ready)
    return;

  auto *buf =
      static_cast<uint8_t *>(heap_caps_malloc(size * size, MALLOC_CAP_SPIRAM));
  if (!buf) {
    ESP_LOGW(TAG, "Failed to allocate %dx%d mask", size, size);
    return;
  }

  float half = static_cast<float>(size) * 0.5f;
  // Inset by 1px so the opaque mask fully covers logo corner pixels
  float r = half - 1.0f;
  float inv_r = 1.0f / r;
  // Nearly binary edge — 0.5px AA eliminates color bleed from logo backgrounds
  float aa = 0.5f / r;

  for (int y = 0; y < size; y++) {
    float ny = std::fabs((static_cast<float>(y) - half + 0.5f) * inv_r);
    float ny_n = std::pow(ny, n);
    for (int x = 0; x < size; x++) {
      float nx = std::fabs((static_cast<float>(x) - half + 0.5f) * inv_r);
      // Superellipse: |x/r|^n + |y/r|^n = 1
      float d = std::pow(nx, n) + ny_n - 1.0f;
      // d < 0 inside, d > 0 outside
      float alpha;
      if (d <= -aa)
        alpha = 0.0f;
      else if (d >= aa)
        alpha = 1.0f;
      else
        alpha = (d + aa) / (2.0f * aa);

      buf[y * size + x] = static_cast<uint8_t>(alpha * 255.0f + 0.5f);
    }
  }

  s_mask_buf = buf;

  std::memset(&s_mask_dsc, 0, sizeof(s_mask_dsc));
  s_mask_dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  s_mask_dsc.header.cf = LV_COLOR_FORMAT_A8;
  s_mask_dsc.header.w = static_cast<uint32_t>(size);
  s_mask_dsc.header.h = static_cast<uint32_t>(size);
  s_mask_dsc.header.stride = static_cast<uint32_t>(size);
  s_mask_dsc.data_size = static_cast<uint32_t>(size * size);
  s_mask_dsc.data = s_mask_buf;

  s_ready = true;
  ESP_LOGI(TAG, "Squircle mask ready: %dx%d, n=%.1f (%d bytes)", size, size, n,
           size * size);
}

const lv_image_dsc_t *squircle_mask_get() {
  return s_ready ? &s_mask_dsc : nullptr;
}
