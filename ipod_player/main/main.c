#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "bsp_init.h"
#include "audio_player.h"
#include "ui_main.h"
#include "media_scanner.h"
#include "video_player.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting iPod Media Player Firmware on ESP32-P4...");

    // 1. Initialize NVS (Non-Volatile Storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. Initialize Waveshare ESP32-P4 Hardware (MIPI-DSI Display, GT911 Touch, SDMMC, ES8311 I2S)
    ESP_ERROR_CHECK(app_bsp_init());

    // 3. Initialize Audio Pipeline & Spawn Audio Decode Task on Core 1
    ESP_ERROR_CHECK(audio_player_init());

    // 4. Initialize LVGL 9 User Interface on Core 0
    /* Scan SD card for all media (songs, photos, videos) */
    media_scanner_scan("/sdcard");

    /* Initialize Video Player subsystem */
    video_player_init();

    ui_init();

    ESP_LOGI(TAG, "System startup complete. Core 0 handling UI loop, Core 1 handling Audio Pipeline.");

    // Periodic state synchronization loop
    while (1) {
        ui_update_loop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
