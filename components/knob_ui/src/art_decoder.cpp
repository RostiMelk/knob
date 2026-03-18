#include "art_decoder.h"

extern "C" {
#include "tjpgd/tjpgd.h"
}

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <cstdlib>
#include <cstring>

static constexpr const char *TAG = "art_dec";
static constexpr int TJPGD_WORK_SIZE = 32768;

struct MemStream {
  const uint8_t *data;
  int len;
  int pos;
};

struct DecodeCtx {
  uint16_t *out;
  int stride; // pixels per row in output buffer
};

static size_t mem_input(JDEC *jd, uint8_t *buf, size_t ndata) {
  auto *ms = static_cast<MemStream *>(jd->device);
  int avail = ms->len - ms->pos;
  if (static_cast<int>(ndata) > avail)
    ndata = avail;
  if (buf)
    memcpy(buf, ms->data + ms->pos, ndata);
  ms->pos += ndata;
  return ndata;
}

static int rgb565_output(JDEC *jd, void *bitmap, JRECT *rect) {
  auto *ctx = static_cast<DecodeCtx *>(jd->device);
  // TJPGD stashes our device pointer in jd->device during prepare,
  // but during decomp the device pointer we passed is the MemStream.
  // We need a different approach — store DecodeCtx via a static.
  (void)jd;
  (void)ctx;
  // This callback is not used in our design — see below.
  return 1;
}

// We use a combined device struct that has both stream and output info.
struct JpegDevice {
  // Stream input
  const uint8_t *data;
  int len;
  int pos;
  // RGB565 output
  uint16_t *pixels;
  int out_w;
  int out_h;
};

static size_t jpeg_input(JDEC *jd, uint8_t *buf, size_t ndata) {
  auto *dev = static_cast<JpegDevice *>(jd->device);
  int avail = dev->len - dev->pos;
  if (static_cast<int>(ndata) > avail)
    ndata = avail;
  if (buf)
    memcpy(buf, dev->data + dev->pos, ndata);
  dev->pos += ndata;
  return ndata;
}

static int jpeg_output(JDEC *jd, void *bitmap, JRECT *rect) {
  auto *dev = static_cast<JpegDevice *>(jd->device);
  auto *src = static_cast<uint16_t *>(bitmap);

  int w = rect->right - rect->left + 1;
  int stride = dev->out_w;

  for (int y = rect->top; y <= rect->bottom; y++) {
    if (y >= dev->out_h)
      break;
    uint16_t *dst = dev->pixels + y * stride + rect->left;
    int copy_w = w;
    if (rect->left + copy_w > stride)
      copy_w = stride - rect->left;
    for (int i = 0; i < copy_w; i++) {
      uint16_t px = src[i];
      // TJPGD outputs BGR565 (B in high bits, R in low bits) — swap to RGB565
      uint16_t r = px & 0x1F;
      uint16_t g = (px >> 5) & 0x3F;
      uint16_t b = (px >> 11) & 0x1F;
      uint16_t rgb = (r << 11) | (g << 5) | b;
      // Byte-swap for LV_COLOR_FORMAT_RGB565_SWAPPED display
      dst[i] = (rgb >> 8) | (rgb << 8);
    }
    src += w;
  }
  return 1; // Continue decompression
}

bool art_decode_jpeg(const uint8_t *jpeg_data, int jpeg_len, uint8_t **out_buf,
                     int *out_w, int *out_h, int max_dim) {
  *out_buf = nullptr;
  *out_w = 0;
  *out_h = 0;

  if (!jpeg_data || jpeg_len < 4)
    return false;

  // Verify JPEG magic
  if (jpeg_data[0] != 0xFF || jpeg_data[1] != 0xD8)
    return false;

  // Allocate work buffer for TJPGD (large images need big pools — use PSRAM)
  void *work = heap_caps_malloc(TJPGD_WORK_SIZE, MALLOC_CAP_SPIRAM);
  if (!work)
    return false;

  JpegDevice dev = {};
  dev.data = jpeg_data;
  dev.len = jpeg_len;
  dev.pos = 0;
  dev.pixels = nullptr;

  JDEC jd = {};
  JRESULT rc = jd_prepare(&jd, jpeg_input, work, TJPGD_WORK_SIZE, &dev);
  if (rc != JDR_OK) {
    ESP_LOGW(TAG, "jd_prepare failed: %d", rc);
    free(work);
    return false;
  }

  // Pick scale factor so output fits within max_dim x max_dim
  // Scale: 0=1/1, 1=1/2, 2=1/4, 3=1/8
  uint8_t scale = 0;
  int sw = jd.width, sh = jd.height;
  while (scale < 3 && (sw > max_dim || sh > max_dim)) {
    scale++;
    sw = jd.width >> scale;
    sh = jd.height >> scale;
  }

  if (sw <= 0 || sh <= 0) {
    heap_caps_free(work);
    return false;
  }

  // Allocate output pixel buffer in PSRAM
  size_t buf_size = static_cast<size_t>(sw) * sh * sizeof(uint16_t);
  auto *pixels =
      static_cast<uint16_t *>(heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM));
  if (!pixels) {
    ESP_LOGW(TAG, "Failed to allocate %u bytes for decoded art",
             (unsigned)buf_size);
    heap_caps_free(work);
    return false;
  }
  memset(pixels, 0, buf_size);

  dev.pixels = pixels;
  dev.out_w = sw;
  dev.out_h = sh;

  // Reset stream position for decompression — jd_prepare consumed header,
  // but TJPGD tracks its own internal position via the input buffer,
  // so we don't reset dev.pos here.

  rc = jd_decomp(&jd, jpeg_output, scale);
  heap_caps_free(work);

  if (rc != JDR_OK) {
    ESP_LOGW(TAG, "jd_decomp failed: %d (scale=%d, %dx%d→%dx%d)", rc, scale,
             jd.width, jd.height, sw, sh);
    heap_caps_free(pixels);
    return false;
  }

  *out_buf = reinterpret_cast<uint8_t *>(pixels);
  *out_w = sw;
  *out_h = sh;

  ESP_LOGI(TAG, "Decoded %dx%d JPEG → %dx%d RGB565 (%u bytes)", jd.width,
           jd.height, sw, sh, (unsigned)buf_size);
  return true;
}
