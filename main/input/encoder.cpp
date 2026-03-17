#include "encoder.h"
#include "app_config.h"

#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <inttypes.h>

static constexpr const char *TAG = "encoder";

// Waveshare Knob-Touch-LCD-1.8 uses a bidirectional switch encoder,
// NOT a standard quadrature encoder. Pin A fires on CW rotation,
// pin B fires on CCW rotation. Each pin produces a brief pulse (low→high→low)
// per detent in its direction. We detect rising edges with software debounce.
//
// Reference: Waveshare demo bidi_switch_knob.c

static constexpr int POLL_INTERVAL_MS = 3; // Match Waveshare demo (3ms)
static constexpr int DEBOUNCE_TICKS = 2;   // Consecutive readings before accept

static esp_timer_handle_t s_poll_timer;

// Per-channel state
struct ChannelState {
  uint8_t prev_level;
  uint8_t debounce_cnt;
};

static ChannelState s_chan_a;
static ChannelState s_chan_b;

// Process one encoder channel: detect rising edge with debounce
// Returns true if a valid edge was detected
static bool process_channel(gpio_num_t pin, ChannelState &ch) {
  uint8_t level = gpio_get_level(pin);

  if (level == 0) {
    // Pin is low — reset debounce if it changed, otherwise count
    if (level != ch.prev_level)
      ch.debounce_cnt = 0;
    else
      ch.debounce_cnt++;
  } else {
    // Pin is high — check for debounced rising edge
    if (level != ch.prev_level && ++ch.debounce_cnt >= DEBOUNCE_TICKS) {
      ch.debounce_cnt = 0;
      ch.prev_level = level;
      return true; // Valid rising edge
    } else {
      ch.debounce_cnt = 0;
    }
  }

  ch.prev_level = level;
  return false;
}

static void on_poll(void *) {
  bool cw = process_channel(static_cast<gpio_num_t>(PIN_ENC_A), s_chan_a);
  bool ccw = process_channel(static_cast<gpio_num_t>(PIN_ENC_B), s_chan_b);

  if (cw) {
    int32_t steps = 1;
    ESP_LOGD(TAG, "step: CW (+1)");
    esp_event_post(APP_EVENT, APP_EVENT_ENCODER_ROTATE, &steps, sizeof(steps),
                   0);
  }
  if (ccw) {
    int32_t steps = -1;
    ESP_LOGD(TAG, "step: CCW (-1)");
    esp_event_post(APP_EVENT, APP_EVENT_ENCODER_ROTATE, &steps, sizeof(steps),
                   0);
  }
}

static void init_gpio() {
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = (1ULL << PIN_ENC_A) | (1ULL << PIN_ENC_B);
  cfg.mode = GPIO_MODE_INPUT;
  cfg.pull_up_en = GPIO_PULLUP_ENABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&cfg));

  // Initialize channel state with current levels
  s_chan_a.prev_level = gpio_get_level(static_cast<gpio_num_t>(PIN_ENC_A));
  s_chan_b.prev_level = gpio_get_level(static_cast<gpio_num_t>(PIN_ENC_B));
  s_chan_a.debounce_cnt = 0;
  s_chan_b.debounce_cnt = 0;
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
           "Encoder ready (A=%d B=%d, bidi-switch mode, poll=%dms, debounce=%d)",
           PIN_ENC_A, PIN_ENC_B, POLL_INTERVAL_MS, DEBOUNCE_TICKS);
}
