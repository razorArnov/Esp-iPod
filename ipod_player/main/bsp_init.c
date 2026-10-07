#include "bsp_init.h"
#include "esp_log.h"

static const char *TAG = "BSP_INIT";

esp_err_t app_bsp_init(void)
{
    ESP_LOGI(TAG, "===================================================");
    ESP_LOGI(TAG, " Waveshare ESP32-P4 iPod Media Player Initialization ");
    ESP_LOGI(TAG, "===================================================");

    // 1. Initialize I2C Bus (GPIO 7 / GPIO 8)
    esp_err_t ret = bsp_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2C Bus initialized.");

    // 2. Initialize ST7701 MIPI-DSI Display & GT911 Touch Screen (LVGL 9 Adapter)
    bsp_display_cfg_t display_cfg = {
        .lv_adapter_cfg = ESP_LV_ADAPTER_DEFAULT_CONFIG(),
        .rotation = ESP_LV_ADAPTER_ROTATE_0, // Portrait mode 480x800
        .tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL,
        .touch_flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0
        }
    };

    lv_display_t *disp = bsp_display_start_with_config(&display_cfg);
    if (!disp) {
        ESP_LOGE(TAG, "Failed to start MIPI-DSI display with BSP config!");
        return ESP_FAIL;
    }
    bsp_display_backlight_on();
    ESP_LOGI(TAG, "MIPI-DSI Display (480x800) and GT911 Touch initialized.");

    // 3. Mount MicroSD card via high-speed 4-bit SDMMC peripheral
    ret = bsp_sdcard_mount();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SD Card mount warning: %s. Verify FAT32/exFAT card is inserted in SD slot.", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "SD Card successfully mounted at point: %s", BSP_SD_MOUNT_POINT);
    }

    // 4. Initialize I2S Audio Subsystem for ES8311 Codec
    ret = bsp_audio_init(NULL); // Default: Duplex 16-bit 44.1kHz
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize I2S audio driver: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "I2S Master Duplex & Audio Codec subsystem initialized.");

    return ESP_OK;
}
