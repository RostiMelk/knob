#include "encoder.h"
#include "hal_pins.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <atomic>
#include <inttypes.h>

static constexpr const char *TAG = "encoder";

// Waveshare Knob-Touch-LCD-1.8 uses a bidirectional switch encoder,
// NOT a standard quadrature encoder. Pin A fires on CW rotation,
// pin B fires on CCW rotation. Each pin produces a brief pulse (low→high→low)
// per detent in its direction. We detect rising edges with software debounce.
//
// Reference: Waveshare demo bidi_switch_knob.c

static constexpr int POLL_INTERVAL_MS = 3; // Match Waveshare demo (3ms)
static constexpr int STABLE_TICKS = 2;     // Consecutive equal readings to
                                           // accept a level as real

static esp_timer_handle_t s_poll_timer;

// Atomic step accumulator — written by the poll timer, read by the UI task
static std::atomic<int32_t> s_steps{0};

// Per-channel state
struct ChannelState {
  uint8_t last_raw;   // last raw sample
  uint8_t stable_cnt; // consecutive samples at last_raw
  bool armed;         // stable low seen — allowed to fire on the next
                      // stable high
};

static ChannelState s_chan_a;
static ChannelState s_chan_b;

// Arm/fire debounce: a step requires a debounced low (rest) followed by a
// debounced high (pulse), and fires exactly once per pulse. Release bounce
// can't double-fire because re-arming needs STABLE_TICKS of solid low.
static bool process_channel(gpio_num_t pin, ChannelState &ch) {
  uint8_t level = gpio_get_level(pin);

  if (level == ch.last_raw) {
    if (ch.stable_cnt < 255)
      ch.stable_cnt++;
  } else {
    ch.last_raw = level;
    ch.stable_cnt = 1;
  }
  if (ch.stable_cnt < STABLE_TICKS)
    return false;

  if (level == 0) {
    ch.armed = true; // resting between detents
    return false;
  }
  if (ch.armed) {
    ch.armed = false;
    return true; // one step per debounced pulse
  }
  return false;
}

static void on_poll(void *) {
  bool cw = process_channel(static_cast<gpio_num_t>(PIN_ENC_A), s_chan_a);
  bool ccw = process_channel(static_cast<gpio_num_t>(PIN_ENC_B), s_chan_b);

  if (cw) {
    s_steps.fetch_add(1, std::memory_order_relaxed);
    ESP_LOGD(TAG, "step: CW (+1)");
  }
  if (ccw) {
    s_steps.fetch_sub(1, std::memory_order_relaxed);
    ESP_LOGD(TAG, "step: CCW (-1)");
  }
}

int32_t encoder_take_steps() {
  return s_steps.exchange(0, std::memory_order_relaxed);
}

static void init_gpio() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&cfg));

  // Initialize channel state with current levels; channels arm themselves
  // after the first debounced low.
  s_chan_a.last_raw = gpio_get_level(static_cast<gpio_num_t>(PIN_ENC_A));
  s_chan_b.last_raw = gpio_get_level(static_cast<gpio_num_t>(PIN_ENC_B));
  s_chan_a.stable_cnt = 0;
  s_chan_b.stable_cnt = 0;
  s_chan_a.armed = false;
  s_chan_b.armed = false;
}

void encoder_init() {
  init_gpio();

  const esp_timer_create_args_t poll_args = {
      .callback = on_poll,
      .arg = nullptr,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "enc_poll",
      .skip_unhandled_events = true,
  };
  ESP_ERROR_CHECK(esp_timer_create(&poll_args, &s_poll_timer));
  ESP_ERROR_CHECK(
      esp_timer_start_periodic(s_poll_timer, POLL_INTERVAL_MS * 1000LL));

  ESP_LOGI(TAG,
           "Encoder ready (A=%d B=%d, bidi-switch mode, poll=%dms, stable=%d)",
           PIN_ENC_A, PIN_ENC_B, POLL_INTERVAL_MS, STABLE_TICKS);
}
