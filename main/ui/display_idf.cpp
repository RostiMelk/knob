#include "app_config.h"
#include "display.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_touch_cst816s.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static constexpr const char *TAG = "display";

static esp_lcd_panel_handle_t s_panel;
static esp_lcd_panel_io_handle_t s_panel_io;
static esp_lcd_touch_handle_t s_touch;
static i2c_master_bus_handle_t s_i2c_bus;

static void init_backlight() {
  ledc_timer_config_t timer_cfg = {};
  timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  timer_cfg.duty_resolution = LEDC_TIMER_10_BIT;
  timer_cfg.timer_num = LEDC_TIMER_0;
  timer_cfg.freq_hz = 25000;
  timer_cfg.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

  ledc_channel_config_t ch_cfg = {};
  ch_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
  ch_cfg.channel = static_cast<ledc_channel_t>(LCD_BL_LEDC_CH);
  ch_cfg.timer_sel = LEDC_TIMER_0;
  ch_cfg.gpio_num = PIN_LCD_BL;
  ch_cfg.duty = 0;
  ch_cfg.hpoint = 0;
  ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
}

static void set_backlight(uint8_t percent) {
  uint32_t duty = (1023 * percent) / 100;
  ledc_set_duty(LEDC_LOW_SPEED_MODE,
                static_cast<ledc_channel_t>(LCD_BL_LEDC_CH), duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE,
                   static_cast<ledc_channel_t>(LCD_BL_LEDC_CH));
}

static void init_lcd() {
  init_backlight();

  gpio_config_t rst_cfg = {};
  rst_cfg.pin_bit_mask = 1ULL << PIN_LCD_RST;
  rst_cfg.mode = GPIO_MODE_OUTPUT;
  gpio_config(&rst_cfg);
  gpio_set_level(static_cast<gpio_num_t>(PIN_LCD_RST), 0);
  vTaskDelay(pdMS_TO_TICKS(20));
  gpio_set_level(static_cast<gpio_num_t>(PIN_LCD_RST), 1);
  vTaskDelay(pdMS_TO_TICKS(120));

  spi_bus_config_t bus_cfg = {};
  bus_cfg.sclk_io_num = PIN_LCD_CLK;
  bus_cfg.data0_io_num = PIN_LCD_SIO0;
  bus_cfg.data1_io_num = PIN_LCD_SIO1;
  bus_cfg.data2_io_num = PIN_LCD_SIO2;
  bus_cfg.data3_io_num = PIN_LCD_SIO3;
  bus_cfg.max_transfer_sz = LCD_H_RES * LCD_DRAW_ROWS * sizeof(uint16_t);
  bus_cfg.flags = SPICOMMON_BUSFLAG_QUAD;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO));

  esp_lcd_panel_io_spi_config_t io_cfg = {};
  io_cfg.cs_gpio_num = PIN_LCD_CS;
  io_cfg.dc_gpio_num = -1;
  io_cfg.spi_mode = 0;
  io_cfg.pclk_hz = 40 * 1000 * 1000;
  io_cfg.trans_queue_depth = 10;
  io_cfg.lcd_cmd_bits = 32;
  io_cfg.lcd_param_bits = 8;
  io_cfg.flags.quad_mode = true;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI2_HOST, &io_cfg, &s_panel_io));

  // TODO: Replace with ST77916-specific init sequence from Waveshare demo.
  // Using st7789 as placeholder — display may be blank until this is ported.
  esp_lcd_panel_dev_config_t panel_cfg = {};
  panel_cfg.reset_gpio_num = -1;
  panel_cfg.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  panel_cfg.bits_per_pixel = 16;
  ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(s_panel_io, &panel_cfg, &s_panel));

  ESP_ERROR_CHECK(esp_lcd_panel_reset(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_init(s_panel));
  ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(s_panel, true));

  set_backlight(80);
  ESP_LOGI(TAG, "LCD initialized (%dx%d)", LCD_H_RES, LCD_V_RES);
}

static void init_touch_hw() {
  i2c_master_bus_config_t i2c_bus_cfg = {};
  i2c_bus_cfg.i2c_port = static_cast<i2c_port_num_t>(TOUCH_I2C_NUM);
  i2c_bus_cfg.sda_io_num = static_cast<gpio_num_t>(PIN_TOUCH_SDA);
  i2c_bus_cfg.scl_io_num = static_cast<gpio_num_t>(PIN_TOUCH_SCL);
  i2c_bus_cfg.flags.enable_internal_pullup = true;

  ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &s_i2c_bus));

  esp_lcd_panel_io_i2c_config_t io_cfg = {};
  io_cfg.scl_speed_hz = TOUCH_I2C_FREQ;
  io_cfg.dev_addr = TOUCH_I2C_ADDR;

  esp_lcd_panel_io_handle_t touch_io;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(s_i2c_bus, &io_cfg, &touch_io));

  esp_lcd_touch_config_t touch_cfg = {};
  touch_cfg.x_max = LCD_H_RES;
  touch_cfg.y_max = LCD_V_RES;
  touch_cfg.rst_gpio_num = static_cast<gpio_num_t>(PIN_TOUCH_RST);
  touch_cfg.int_gpio_num = static_cast<gpio_num_t>(PIN_TOUCH_INT);

  ESP_ERROR_CHECK(
      esp_lcd_touch_new_i2c_cst816s(touch_io, &touch_cfg, &s_touch));
  ESP_LOGI(TAG, "Touch initialized");
}

i2c_master_bus_handle_t display_get_i2c_bus() { return s_i2c_bus; }

void display_init(lv_display_t **disp, lv_indev_t **touch) {
  init_lcd();
  init_touch_hw();

  const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
  ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

  const lvgl_port_display_cfg_t disp_cfg = {
      .io_handle = s_panel_io,
      .panel_handle = s_panel,
      .buffer_size =
          static_cast<uint32_t>(LCD_H_RES * LCD_DRAW_ROWS * sizeof(uint16_t)),
      .double_buffer = true,
      .hres = LCD_H_RES,
      .vres = LCD_V_RES,
      .monochrome = false,
      .rotation =
          {
              .swap_xy = false,
              .mirror_x = false,
              .mirror_y = false,
          },
      .flags =
          {
              .buff_dma = true,
              .buff_spiram = false,
              .sw_rotate = false,
              .full_refresh = false,
              .direct_mode = false,
          },
  };
  *disp = lvgl_port_add_disp(&disp_cfg);

  const lvgl_port_touch_cfg_t touch_port_cfg = {
      .disp = *disp,
      .handle = s_touch,
  };
  *touch = lvgl_port_add_touch(&touch_port_cfg);

  ESP_LOGI(TAG, "LVGL port initialized");
}

bool display_lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }

void display_unlock() { lvgl_port_unlock(); }
