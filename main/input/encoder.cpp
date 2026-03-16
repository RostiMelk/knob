#include "encoder.h"
#include "app_config.h"

#include "driver/pulse_cnt.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <inttypes.h>

static constexpr const char *TAG = "encoder";

static constexpr int PCNT_HIGH_LIMIT = 100;
static constexpr int PCNT_LOW_LIMIT = -100;
static constexpr int POLL_INTERVAL_MS = 20;

static pcnt_unit_handle_t s_pcnt_unit;
static esp_timer_handle_t s_poll_timer;
static int s_last_count;

static void on_poll(void *) {
  int count = 0;
  pcnt_unit_get_count(s_pcnt_unit, &count);

  int delta = count - s_last_count;
  if (delta == 0)
    return;

  s_last_count = count;

  // This encoder produces 1 count per detent (confirmed via serial log).
  // Each non-zero delta is one step.
  int32_t steps = (delta > 0) ? 1 : -1;

  ESP_LOGI(TAG, "step: count=%d delta=%d steps=%" PRId32, count, delta, steps);
  esp_event_post(APP_EVENT, APP_EVENT_ENCODER_ROTATE, &steps, sizeof(steps), 0);
}

static void init_pcnt() {
  pcnt_unit_config_t unit_cfg = {};
  unit_cfg.high_limit = PCNT_HIGH_LIMIT;
  unit_cfg.low_limit = PCNT_LOW_LIMIT;
  unit_cfg.flags.accum_count = 1;
  ESP_ERROR_CHECK(pcnt_new_unit(&unit_cfg, &s_pcnt_unit));

  pcnt_glitch_filter_config_t filter_cfg = {};
  filter_cfg.max_glitch_ns = 50'000;
  ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(s_pcnt_unit, &filter_cfg));

  // Channel A: edge on B, level on A (swapped from original — fixes
  // oscillation where each detent produced +1 then -1 instead of accumulating)
  pcnt_chan_config_t chan_a_cfg = {};
  chan_a_cfg.edge_gpio_num = PIN_ENC_B;
  chan_a_cfg.level_gpio_num = PIN_ENC_A;
  pcnt_channel_handle_t chan_a;
  ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt_unit, &chan_a_cfg, &chan_a));
  ESP_ERROR_CHECK(
      pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
                                   PCNT_CHANNEL_EDGE_ACTION_INCREASE));
  ESP_ERROR_CHECK(
      pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                    PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

  // Channel B: edge on A, level on B
  pcnt_chan_config_t chan_b_cfg = {};
  chan_b_cfg.edge_gpio_num = PIN_ENC_A;
  chan_b_cfg.level_gpio_num = PIN_ENC_B;
  pcnt_channel_handle_t chan_b;
  ESP_ERROR_CHECK(pcnt_new_channel(s_pcnt_unit, &chan_b_cfg, &chan_b));
  ESP_ERROR_CHECK(
      pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
                                   PCNT_CHANNEL_EDGE_ACTION_DECREASE));
  ESP_ERROR_CHECK(
      pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
                                    PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

  ESP_ERROR_CHECK(pcnt_unit_enable(s_pcnt_unit));
  ESP_ERROR_CHECK(pcnt_unit_clear_count(s_pcnt_unit));
  ESP_ERROR_CHECK(pcnt_unit_start(s_pcnt_unit));
}

void encoder_init() {
  init_pcnt();

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

  ESP_LOGI(TAG, "Encoder ready (A=%d B=%d swapped, poll=%dms, filter=50us)",
           PIN_ENC_A, PIN_ENC_B, POLL_INTERVAL_MS);
}
