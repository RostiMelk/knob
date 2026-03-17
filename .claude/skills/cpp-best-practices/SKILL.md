---
name: cpp-best-practices
description: Modern C++20 best practices for ESP32-S3 embedded development. Use when writing new code, refactoring, or reviewing for DRY violations, readability, and reusability.
---

# C++20 Best Practices for ESP32-S3

This project uses C++20 on ESP-IDF v5.4 (GCC 13.2 Xtensa). No exceptions, no RTTI.
These guidelines prioritize readability, DRY, and safe abstractions within embedded constraints.

## 1. DRY — Don't Repeat Yourself

### Shared Theme/Palette

Colors are currently duplicated across `ui.cpp`, `ui_timer.cpp`, `ui_voice.cpp`, and `ui_pages.cpp` with different prefixes (`COL_`, `COL_T_`, `COL_V_`). Centralize:

```cpp
// ui/theme.h — single source of truth
#pragma once
#include "lvgl.h"

namespace theme {
  constexpr lv_color_t bg         = lv_color_hex(0x000000);
  constexpr lv_color_t text       = lv_color_hex(0xFFFFFF);
  constexpr lv_color_t text_sec   = lv_color_hex(0x8E8E93);
  constexpr lv_color_t accent     = lv_color_hex(0x0A84FF);
  constexpr lv_color_t green      = lv_color_hex(0x30D158);
  constexpr lv_color_t orange     = lv_color_hex(0xFF9F0A);
  constexpr lv_color_t red        = lv_color_hex(0xFF453A);
  constexpr lv_color_t arc_bg     = lv_color_hex(0x1C1C1E);
  constexpr lv_color_t dim        = lv_color_hex(0x48484A);
  constexpr lv_color_t indigo     = lv_color_hex(0x5E5CE6);
  constexpr lv_color_t purple     = lv_color_hex(0xBF5AF2);
}
```

### Shared Animation Helpers

`ui.cpp` has `anim_fade()` but `ui_voice.cpp` (24 lv_anim_init blocks) and `ui_timer.cpp` don't use it. Extract to a shared header:

```cpp
// ui/anim_helpers.h
#pragma once
#include "lvgl.h"

// Simple fade: animates a single property from start to end
void anim_fade(lv_obj_t *obj, lv_anim_exec_xcb_t exec_cb,
               int32_t start, int32_t end, int duration_ms,
               lv_anim_completed_cb_t done_cb = nullptr);

// Pulse: oscillates between lo and hi (for breathing effects)
void anim_pulse(lv_obj_t *obj, lv_anim_exec_xcb_t exec_cb,
                int32_t lo, int32_t hi, int period_ms,
                int repeat_count = LV_ANIM_REPEAT_INFINITE);

// Cancel all animations on an object for a given callback
inline void anim_cancel(lv_obj_t *obj, lv_anim_exec_xcb_t cb) {
  lv_anim_delete(obj, cb);
}
```

### SOAP Helpers

The SOAP envelope wrapping in `sonos.cpp` is repeated for every command. Extract:

```cpp
// Before: repeated in every command
char envelope[1024];
snprintf(envelope, sizeof(envelope), SOAP_ENVELOPE_FMT, body);

// After: helper that builds and sends
bool soap_fire(const char *path, const char *action, const char *ns,
               const char *body_xml);
bool soap_request(const char *path, const char *action, const char *ns,
                  const char *body_xml, Response *resp);
```

This is already partially done — keep pushing toward fewer raw `snprintf` calls.

## 2. Modern C++20 Features (Safe on ESP-IDF v5.4)

### Use freely

| Feature | Use for | Example |
|---------|---------|--------|
| `constexpr` | Compile-time constants, lookup tables | `constexpr int LCD_H_RES = 360;` |
| `std::string_view` | Non-owning string references | Function params instead of `const char*` when you need `.size()` |
| `std::clamp` | Bounded values | `std::clamp(vol, VOLUME_MIN, VOLUME_MAX)` |
| `std::optional` | Nullable returns without pointers | `std::optional<int> parse_volume(const char* xml)` |
| `std::array` | Fixed-size arrays with bounds info | `std::array<Station, 12> stations` |
| Structured bindings | Unpacking structs/pairs | `auto [ip, port] = parse_endpoint(url);` |
| `[[nodiscard]]` | Force callers to check return values | `[[nodiscard]] bool soap_fire(...)` |
| `[[maybe_unused]]` | Suppress warnings on debug-only vars | `[[maybe_unused]] int64_t t0 = esp_timer_get_time();` |
| Designated initializers | Readable struct init | `Config cfg = { .timeout = 5000, .retries = 3 };` |
| `enum class` | Type-safe enums | `enum class PlayState : uint8_t { ... };` (already used) |
| `using` aliases | Readable function types | `using PageChangedCb = void(*)(int, const char*);` |

### Use with care

| Feature | Caveat |
|---------|--------|
| Designated initializers | **Must be in declaration order** — GCC enforces strictly. Use field-by-field assignment for third-party structs (e.g., `esp_lvgl_port` types). |
| `std::string_view` | Does NOT own the data. Never return a `string_view` to a local buffer. |
| `std::optional` | Adds 1 byte overhead + alignment padding. Fine for return values, avoid in hot structs. |
| Templates | Use sparingly — each instantiation adds flash. Prefer `constexpr` functions. |
| `auto` | Use for iterators and complex types. Avoid for simple types where the type aids readability. |

### Avoid

| Feature | Why |
|---------|-----|
| `std::string` | Heap allocation for every instance. Use `char[]` or `std::string_view`. |
| `std::map` / `std::unordered_map` | Heavy allocator use. Use sorted `std::array` + binary search. |
| `std::vector` (in hot paths) | Heap allocation + reallocation. Pre-size or use `std::array`. |
| `std::shared_ptr` | Atomic refcount overhead. Use `std::unique_ptr` or raw ownership. |
| `dynamic_cast` | Requires RTTI (disabled). |
| `std::iostream` | Massive binary size. Use `ESP_LOGx` macros. |
| `std::format` | Not available in GCC 13.2 libstdc++ for Xtensa. Use `snprintf`. |
| `std::ranges` | Partial support, pulls in heavy headers. Use raw loops. |
| Exceptions | Disabled (`-fno-exceptions`). Use return codes or `std::optional`. |

## 3. RAII — Resource Acquisition Is Initialization

### Display Lock Guard

```cpp
// Before: manual lock/unlock (easy to forget unlock on early return)
if (display_lock(50)) {
  lv_label_set_text(label, "hello");
  if (error) return;  // BUG: display_unlock() never called!
  display_unlock();
}

// After: RAII guard
class DisplayLock {
  bool locked_;
public:
  explicit DisplayLock(int timeout_ms = 50)
    : locked_(display_lock(timeout_ms)) {}
  ~DisplayLock() { if (locked_) display_unlock(); }
  explicit operator bool() const { return locked_; }
  DisplayLock(const DisplayLock&) = delete;
  DisplayLock& operator=(const DisplayLock&) = delete;
};

// Usage:
if (DisplayLock lock{50}) {
  lv_label_set_text(label, "hello");
  if (error) return;  // Safe: destructor calls display_unlock()
}
```

### HTTP Client RAII

```cpp
// RAII wrapper for esp_http_client
class HttpClient {
  esp_http_client_handle_t handle_;
public:
  explicit HttpClient(const esp_http_client_config_t& cfg)
    : handle_(esp_http_client_init(&cfg)) {}
  ~HttpClient() { if (handle_) esp_http_client_cleanup(handle_); }
  esp_http_client_handle_t get() { return handle_; }
  explicit operator bool() const { return handle_ != nullptr; }
  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;
};
```

### PSRAM Buffer RAII

```cpp
// Typed PSRAM allocation with automatic cleanup
template<typename T>
class PsramBuffer {
  T* ptr_;
  size_t count_;
public:
  explicit PsramBuffer(size_t count)
    : ptr_(static_cast<T*>(heap_caps_malloc(count * sizeof(T), MALLOC_CAP_SPIRAM)))
    , count_(count) {}
  ~PsramBuffer() { if (ptr_) heap_caps_free(ptr_); }
  T* data() { return ptr_; }
  size_t size() const { return count_; }
  explicit operator bool() const { return ptr_ != nullptr; }
  T& operator[](size_t i) { return ptr_[i]; }
  PsramBuffer(const PsramBuffer&) = delete;
  PsramBuffer& operator=(const PsramBuffer&) = delete;
};
```

## 4. Strong Types

Avoid "primitive obsession" — raw `int` for volume, station index, and pixel coordinates are easy to mix up.

```cpp
// Lightweight strong type (zero overhead with constexpr)
struct Volume {
  int value;
  constexpr Volume clamp() const {
    return {std::clamp(value, VOLUME_MIN, VOLUME_MAX)};
  }
};

struct StationIndex {
  int value;
  constexpr StationIndex wrap(int count) const {
    return {((value % count) + count) % count};
  }
};

// Prevents: sonos_set_volume(station_index) — type error!
void sonos_set_volume(Volume vol);
void sonos_play_station(StationIndex idx);
```

Apply selectively — strong types for API boundaries, raw types for internal math.

## 5. Readability Patterns

### Named Constants Over Magic Numbers

```cpp
// Before
lv_obj_set_style_shadow_width(container, 20, LV_PART_MAIN);
lv_obj_set_style_shadow_spread(container, 2, LV_PART_MAIN);
lv_obj_align(container, LV_ALIGN_CENTER, 0, -30);

// After
constexpr int LOGO_SHADOW_WIDTH = 20;
constexpr int LOGO_SHADOW_SPREAD = 2;
constexpr int LOGO_Y_OFFSET = -30;
```

### Builder-Style Widget Setup

For complex LVGL widget initialization, group related properties:

```cpp
// Group by concern, not by API call order
static lv_obj_t* create_label(lv_obj_t* parent, const lv_font_t* font,
                               lv_color_t color, lv_align_t align,
                               int x_ofs = 0, int y_ofs = 0) {
  auto* lbl = lv_label_create(parent);
  lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
  lv_obj_align(lbl, align, x_ofs, y_ofs);
  return lbl;
}

// Usage: one line instead of four
s_lbl_station = create_label(parent, &geist_medium_28, theme::text,
                              LV_ALIGN_CENTER, 0, 68);
```

### Early Return Over Deep Nesting

```cpp
// Before
void ui_set_volume(int level) {
  if (display_lock(50)) {
    s_volume = level;
    if (pages_is_home() && s_vol_arc) {
      lv_arc_set_value(s_vol_arc, level);
    }
    display_unlock();
  }
}

// After
void ui_set_volume(int level) {
  DisplayLock lock{50};
  if (!lock) return;

  s_volume = level;
  if (pages_is_home() && s_vol_arc)
    lv_arc_set_value(s_vol_arc, level);
}
```

## 6. Memory Management

### Stack vs Heap vs PSRAM

| Size | Where | Example |
|------|-------|---------|
| < 4 KB | Stack | Local buffers, small structs |
| 4–16 KB | Heap (internal SRAM) | Default `malloc` for small allocs |
| > 16 KB | PSRAM | `heap_caps_malloc(size, MALLOC_CAP_SPIRAM)` |
| DMA buffers | Internal SRAM | `heap_caps_malloc(size, MALLOC_CAP_DMA)` |

`CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384` means `malloc` auto-routes >16KB to PSRAM.

### Constexpr String Tables

```cpp
// Prefer constexpr arrays over runtime-built strings
constexpr std::string_view PLAY_STATES[] = {
  "Playing", "Paused", "Stopped", "Loading...", ""
};

// Instead of switch/case returning string literals
const char* text = PLAY_STATES[static_cast<int>(state)].data();
```

### Avoid Fragmentation

- Pre-allocate buffers at init, reuse them
- Use `static` local buffers for formatting (thread-safe if single-task)
- Prefer `snprintf` into fixed buffers over string concatenation
- FreeRTOS queues copy data — keep queue items small (use indices, not full structs)

## 7. Concurrency (FreeRTOS + C++)

### Type-Safe Queue

```cpp
template<typename T, size_t N>
class Queue {
  QueueHandle_t handle_;
public:
  Queue() : handle_(xQueueCreate(N, sizeof(T))) {}
  bool send(const T& item, TickType_t wait = pdMS_TO_TICKS(50)) {
    return xQueueSend(handle_, &item, wait) == pdTRUE;
  }
  bool receive(T& item, TickType_t wait = portMAX_DELAY) {
    return xQueueReceive(handle_, &item, wait) == pdTRUE;
  }
  void reset() { xQueueReset(handle_); }
};

// Usage:
Queue<Command, 8> cmd_queue;  // type-safe, size checked at compile time
```

### Task Pinning

All UI work on Core 1 (matches LVGL task affinity).
WiFi on Core 0 (ESP-IDF default).
Never block the LVGL task with network I/O.

## 8. Error Handling Without Exceptions

```cpp
// Option 1: std::optional for "might not have a value"
std::optional<int> xml_extract_int(const char* xml, const char* tag) {
  char buf[32];
  if (!xml_extract(xml, tag, buf, sizeof(buf))) return std::nullopt;
  return atoi(buf);
}

// Option 2: ESP-IDF error codes for operations that can fail
[[nodiscard]] esp_err_t wifi_connect(const char* ssid, const char* pass);

// Option 3: bool + out parameter for simple cases
bool soap_request(const char* path, ..., Response* out_resp);
```

Always use `[[nodiscard]]` on functions where ignoring the return value is a bug.

## 9. Code Organization Checklist

When writing new code or refactoring, check:

- [ ] **Colors**: using `theme.h` constants, not local `#define COL_*`?
- [ ] **Animations**: using shared `anim_fade()` / `anim_pulse()`, not raw `lv_anim_init` blocks?
- [ ] **Display lock**: using RAII guard, not manual lock/unlock?
- [ ] **Magic numbers**: named constants for sizes, offsets, durations?
- [ ] **String formatting**: `snprintf` into fixed buffer, not `std::string`?
- [ ] **Error returns**: `[[nodiscard]]` on fallible functions?
- [ ] **Ownership**: clear who frees each allocation? RAII where possible?
- [ ] **Thread safety**: LVGL calls only from UI task? Network calls only from net task?
