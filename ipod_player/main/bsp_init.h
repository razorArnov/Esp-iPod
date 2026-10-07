#include "esp_lv_adapter.h"
#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize all hardware peripherals for Waveshare ESP32-P4 Touch LCD 4.3
 * 
 * Includes:
 *  - I2C Bus setup
 *  - ST7701 MIPI-DSI Display & GT911 Touch Controller (LVGL 9 adapter)
 *  - SDMMC Card Mount at /sdcard
 *  - ES8311 I2S Audio Codec Driver
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t app_bsp_init(void);

#ifdef __cplusplus
}
#endif
