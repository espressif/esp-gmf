/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include "esp_log.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_board_manager.h"
#include "test_periphs.h"

// Conditionally include device test headers based on Kconfig
#ifdef CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT
#include "test_dev_audio_codec.h"
#include "test_board_mgr.h"
#endif  /* CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT
#include "esp_lvgl_port.h"
#include "test_dev_lcd_lvgl.h"
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_GPIO_CTRL_SUPPORT
#include "test_dev_pwr_ctrl.h"
#endif  /* CONFIG_ESP_BOARD_DEV_GPIO_CTRL_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_FS_SPIFFS_SUPPORT
#include "test_dev_fs_spiffs.h"
#endif  /* CONFIG_ESP_BOARD_DEV_FS_SPIFFS_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT
#include "test_dev_fatfs_sdcard.h"
#endif  /* CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT
#include "test_dev_ledc.h"
#endif  /* CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT */

static const char *TAG = "MAIN";

#ifdef CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT
static void test_audio(void)
{
    ESP_LOGI(TAG, "Starting audio tests...");

#ifdef CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT
    ESP_LOGI(TAG, "Using SD card audio implementation...");
#else
    ESP_LOGI(TAG, "Using embedded audio implementation...");
#endif
#ifdef CONFIG_ESP_BOARD_DEV_GPIO_CTRL_SUPPORT
    test_dev_pwr_audio_ctrl(true);
#endif  /* CONFIG_ESP_BOARD_DEV_GPIO_CTRL_SUPPORT */

    test_board_mgr_audio_playback_and_record();
}
#endif  /* CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_FS_SPIFFS_SUPPORT
static void test_spiffs_filesystem(void)
{
    ESP_LOGI(TAG, "Starting SPIFFS filesystem tests...");
    test_spiffs();
}
#endif  /* CONFIG_ESP_BOARD_DEV_FS_SPIFFS_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT
static void test_sdcard_filesystem(void)
{
    ESP_LOGI(TAG, "Starting SD card filesystem tests...");
#ifdef CONFIG_ESP_BOARD_DEV_GPIO_CTRL_SUPPORT
    test_dev_pwr_lcd_ctrl(true);
#endif
    test_sdcard();
}
#endif  /* CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT
static void test_ledc_device(void)
{
    ESP_LOGI(TAG, "Starting LEDC device tests...");
    test_dev_ledc_ctrl();
}
#endif  /* CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT */


#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT
static void test_lcd_lvgl(void)
{
    ESP_LOGI(TAG, "Starting LCD LVGL tests...");

#ifdef CONFIG_ESP_BOARD_DEV_GPIO_CTRL_SUPPORT
    test_dev_pwr_lcd_ctrl(true);
#endif  /* CONFIG_ESP_BOARD_DEV_GPIO_CTRL_SUPPORT */

    esp_err_t err = test_dev_lcd_lvgl_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD initialization failed");
        return;
    }

    err = test_dev_lcd_touch_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LCD Touch initialization failed");
        return;
    }

    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "Starting LCD LVGL test...");
    test_dev_lcd_lvgl_show_menu();
}
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT */

void app_main(void)
{
    ESP_LOGI(TAG, "Starting ESP Board Manager Test Application");
    esp_board_manager_print_board_info();
    // Initialize board manager
    int ret = esp_board_manager_init();
    if (ret != 0) {
        ESP_LOGE(TAG, "Board manager initialization failed: %d", ret);
        return;
    }

#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT
    test_lcd_lvgl();
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT
    test_audio();
#endif  /* CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_FS_SPIFFS_SUPPORT
    test_spiffs_filesystem();
#endif  /* CONFIG_ESP_BOARD_DEV_FS_SPIFFS_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT
    test_sdcard_filesystem();
#endif  /* CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT
    test_ledc_device();
#endif  /* CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT */


#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT
    lvgl_port_stop();
    test_dev_lcd_touch_deinit();
    test_dev_lcd_lvgl_deinit();
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT */

    ESP_LOGI(TAG, "Show all devices and peripherals status");
    esp_board_manager_print();

    ESP_LOGI(TAG, "Starting cleanup...");
    ret = esp_board_manager_deinit();
    if (ret != 0) {
        ESP_LOGE(TAG, "Board manager deinitialization failed: %d", ret);
    } else {
        ESP_LOGI(TAG, "Board manager deinitialized successfully");
    }
    ESP_LOGI(TAG, "Test completed");
}
