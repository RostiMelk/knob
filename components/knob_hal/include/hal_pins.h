#pragma once

#include <cstdint>

// ─── Display: ST77916 QSPI 360×360 ─────────────────────────────────────────

constexpr int PIN_LCD_CS = 14;
constexpr int PIN_LCD_CLK = 13;
constexpr int PIN_LCD_SIO0 = 15;
constexpr int PIN_LCD_SIO1 = 16;
constexpr int PIN_LCD_SIO2 = 17;
constexpr int PIN_LCD_SIO3 = 18;
constexpr int PIN_LCD_RST = 21;
constexpr int PIN_LCD_BL = 47;

constexpr int LCD_H_RES = 360;
constexpr int LCD_V_RES = 360;
constexpr int LCD_DRAW_ROWS = 36;
constexpr int LCD_BL_LEDC_CH = 0;

// ─── Touch: CST816D I2C ─────────────────────────────────────────────────────

constexpr int PIN_TOUCH_SDA = 11;
constexpr int PIN_TOUCH_SCL = 12;
constexpr int PIN_TOUCH_INT = 9;
constexpr int PIN_TOUCH_RST = 10;

constexpr int TOUCH_I2C_NUM = 0;
constexpr int TOUCH_I2C_FREQ = 400'000;

// ─── Rotary Encoder ─────────────────────────────────────────────────────────

constexpr int PIN_ENC_A = 8;
constexpr int PIN_ENC_B = 7;

// ─── I2C Bus (shared: touch + DRV2605 haptics) ─────────────────────────────

constexpr uint8_t DRV2605_ADDR = 0x5A;
constexpr uint8_t TOUCH_I2C_ADDR = 0x15;

// ─── Audio ──────────────────────────────────────────────────────────────────

constexpr int PIN_I2S_BCLK = 39;
constexpr int PIN_I2S_WS = 40;
constexpr int PIN_I2S_DOUT = 41;
constexpr int PIN_PDM_CLK = 45;
constexpr int PIN_PDM_DATA = 46;

// ─── SD Card (SDMMC 4-wire) ─────────────────────────────────────────────────

constexpr int PIN_SD_CMD = 3;
constexpr int PIN_SD_CLK = 4;
constexpr int PIN_SD_D0 = 5;
constexpr int PIN_SD_D1 = 6;
constexpr int PIN_SD_D2 = 42;
constexpr int PIN_SD_D3 = 2;
