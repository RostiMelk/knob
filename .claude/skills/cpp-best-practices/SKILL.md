---
name: cpp-best-practices
description: Modern C++20/23 best practices for ESP32-S3 embedded development. Use when writing new code, refactoring, or reviewing for DRY violations, readability, and reusability.
---

# C++20/23 Best Practices for ESP32-S3

ESP-IDF v5.4 ships **GCC 14.2.0** defaulting to **`-std=gnu++23`**.
Nearly all C++20 and most C++23 features are available.
No exceptions (`-fno-exceptions`), no RTTI (`-fno-rtti`).

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

## 2. C++ Feature Matrix (GCC 14.2 / gnu++23)

### Use freely

| Feature | Use for | Example |
|---------|---------|--------|
| `constexpr` | Compile-time constants, lookup tables | `constexpr int LCD_H_RES = 360;` |
| `consteval` | Force compile-time evaluation | `consteval uint32_t make_color(uint8_t r, uint8_t g, uint8_t b)` |
| `std::string_view` | Non-owning string references | Function params instead of `const char*` when you need `.size()` |
| `std::span` | Bounds-safe buffer views | `void parse(std::span<const char> data)` instead of `(char* buf, size_t len)` |
| `std::optional` | Nullable returns without pointers | `std::optional<int> parse_volume(const char* xml)` |
| `std::expected` | Error handling without exceptions (C++23) | `std::expected<int, SonosError> get_volume()` |
| `std::array` | Fixed-size arrays with bounds info | `std::array<Station, 12> stations` |
| `std::clamp` | Bounded values | `std::clamp(vol, VOLUME_MIN, VOLUME_MAX)` |
| Structured bindings | Unpacking structs/pairs | `auto [ip, port] = parse_endpoint(url);` |
| Designated initializers | Readable struct init | `Config cfg = { .timeout = 5000, .retries = 3 };` |
| Concepts | Template constraints | `template<EventPayload T> void post(const T& data)` |
| `[[nodiscard]]` | Force callers to check return values | `[[nodiscard]] bool soap_fire(...)` |
| `[[maybe_unused]]` | Suppress warnings on debug-only vars | `[[maybe_unused]] int64_t t0 = esp_timer_get_time();` |
| `enum class` | Type-safe enums | `enum class PlayState : uint8_t { ... };` (already used) |
| `using` aliases | Readable function types | `using PageChangedCb = void(*)(int, const char*);` |

### Use with care

| Feature | Caveat |
|---------|--------|
| Designated initializers | **Must be in declaration order** — GCC enforces strictly. Use field-by-field assignment for third-party structs (e.g., `esp_lvgl_port` types). |
| `std::string_view` | Does NOT own the data. Never return a `string_view` to a local buffer. Not null-terminated — copy to `char[]` before passing to C APIs. |
| `std::optional` | Adds 1 byte overhead + alignment padding. Fine for return values, avoid in hot structs. |
| `std::format` | Available in GCC 14 but may have issues with newlib (ESP-IDF's C library). Test before relying on it. `snprintf` is the safe fallback. |
| Templates | Each instantiation adds flash. Prefer `constexpr` functions. Use `extern template` to limit bloat. |
| `auto` | Use for iterators and complex types. Avoid for simple types where the type aids readability. |
| `std::ranges` | Available but pulls in heavy headers. Verify binary size impact before using broadly. |

### Avoid

| Feature | Why |
|---------|-----|
| `std::string` | Heap allocation for every instance. Use `std::string_view` or `char[]`. |
| `std::map` / `std::unordered_map` | Heavy allocator use. Use sorted `std::array` + binary search for small N. |
| `std::shared_ptr` | Atomic refcount overhead. Use `std::unique_ptr` or raw ownership. |
| `dynamic_cast` | Requires RTTI (disabled). |
| `std::iostream` | +200KB binary size. Use `ESP_LOGx` macros. |
| `std::function` | May heap-allocate. Use function pointers or templates. |
| Exceptions | Disabled (`-fno-exceptions`). Use `std::expected` or return codes. |
| Modules | Not supported by ESP-IDF build system. |

## 3. Error Handling with std::expected (C++23)

The best pattern for this project — replaces both error codes and exceptions:

```cpp
#include <expected>

enum class SonosError : uint8_t {
  Timeout, HttpError, ParseError, NotFound
};

template<typename T>
using Result = std::expected<T, SonosError>;
using VoidResult = std::expected<void, SonosError>;

// Function returns either a value or an error
Result<int> get_volume(const char *speaker_ip) {
  // ... HTTP request ...
  if (err != ESP_OK) return std::unexpected(SonosError::HttpError);
  if (status != 200) return std::unexpected(SonosError::ParseError);
  return parsed_volume;  // success
}

// Caller
if (auto vol = get_volume(ip)) {
  ESP_LOGI(TAG, "Volume: %d", *vol);
} else {
  ESP_LOGE(TAG, "Failed: %d", static_cast<int>(vol.error()));
}

// With value_or for defaults
int vol = get_volume(ip).value_or(50);
```

**Note**: `std::expected` is header-only and should work with newlib, but verify on hardware before using in critical paths.

## 4. RAII — Resource Acquisition Is Initialization

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

## 5. std::span for Buffer Safety

Replace raw `(char* buf, size_t len)` pairs with `std::span`:

```cpp
// Before: easy to pass wrong length
bool xml_extract(const char *xml, const char *tag, char *out, size_t out_len);

// After: bounds carried with the buffer
bool xml_extract(std::string_view xml, std::string_view tag, std::span<char> out);

// Works with any contiguous container:
char buf[256];
xml_extract(response, "CurrentVolume", std::span{buf});

std::array<char, 64> arr;
xml_extract(response, "CurrentVolume", arr);  // implicit conversion
```

## 6. Strong Types

Avoid "primitive obsession" — raw `int` for volume, station index, and pixel coordinates are easy to mix up.

```cpp
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

## 7. Readability Patterns

### Named Constants Over Magic Numbers

```cpp
// Before
lv_obj_set_style_shadow_width(container, 20, LV_PART_MAIN);
lv_obj_align(container, LV_ALIGN_CENTER, 0, -30);

// After
constexpr int LOGO_SHADOW_WIDTH = 20;
constexpr int LOGO_Y_OFFSET = -30;
```

### Builder-Style Widget Setup

```cpp
static lv_obj_t* create_label(lv_obj_t* parent, const lv_font_t* font,
                               lv_color_t color, lv_align_t align,
                               int x_ofs = 0, int y_ofs = 0) {
  auto* lbl = lv_label_create(parent);
  lv_obj_set_style_text_font(lbl, font, LV_PART_MAIN);
  lv_obj_set_style_text_color(lbl, color, LV_PART_MAIN);
  lv_obj_align(lbl, align, x_ofs, y_ofs);
  return lbl;
}

// One line instead of four:
s_lbl_station = create_label(parent, &geist_medium_28, theme::text,
                              LV_ALIGN_CENTER, 0, 68);
```

### Early Return Over Deep Nesting

```cpp
// Before
void ui_set_volume(int level) {
  if (display_lock(50)) {
    s_volume = level;
    if (pages_is_home() && s_vol_arc)
      lv_arc_set_value(s_vol_arc, level);
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

## 8. Memory Management

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
const char* text = PLAY_STATES[static_cast<int>(state)].data();
```

### Avoid Fragmentation

- Pre-allocate buffers at init, reuse them
- Use `static` local buffers for formatting (thread-safe if single-task)
- Prefer `snprintf` into fixed buffers over string concatenation
- FreeRTOS queues copy data — keep queue items small (use indices, not full structs)

## 9. Concurrency (FreeRTOS + C++)

### Type-Safe Queue

```cpp
template<typename T, size_t N>
class Queue {
  static_assert(std::is_trivially_copyable_v<T>,
                "Queue items must be trivially copyable");
  QueueHandle_t handle_;
public:
  Queue() : handle_(xQueueCreate(N, sizeof(T))) {}
  bool send(const T& item, TickType_t wait = pdMS_TO_TICKS(50)) {
    return xQueueSend(handle_, &item, wait) == pdTRUE;
  }
  std::optional<T> receive(TickType_t wait = portMAX_DELAY) {
    T item;
    if (xQueueReceive(handle_, &item, wait) == pdTRUE) return item;
    return std::nullopt;
  }
  void reset() { xQueueReset(handle_); }
};

// Usage:
Queue<Command, 8> cmd_queue;
if (auto cmd = cmd_queue.receive(pdMS_TO_TICKS(200))) {
  handle(*cmd);
}
```

### Task Pinning Rules

- All UI/LVGL work on **Core 1** (matches LVGL task affinity)
- WiFi on **Core 0** (ESP-IDF default)
- Never block the LVGL task with network I/O

## 10. Code Organization Checklist

When writing new code or refactoring, check:

- [ ] **Colors**: using `theme.h` constants, not local `#define COL_*`?
- [ ] **Animations**: using shared `anim_fade()` / `anim_pulse()`, not raw `lv_anim_init` blocks?
- [ ] **Display lock**: using RAII guard, not manual lock/unlock?
- [ ] **Buffers**: using `std::span` for buffer params, not `(ptr, len)` pairs?
- [ ] **Errors**: using `std::expected` or `std::optional`, not sentinel values?
- [ ] **Magic numbers**: named constants for sizes, offsets, durations?
- [ ] **String formatting**: `snprintf` into fixed buffer, not `std::string`?
- [ ] **Error returns**: `[[nodiscard]]` on fallible functions?
- [ ] **Ownership**: clear who frees each allocation? RAII where possible?
- [ ] **Thread safety**: LVGL calls only from UI task? Network calls only from net task?
