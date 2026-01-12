#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "dev_ledc_ctrl.h"
#include "esp_board_device.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"

static const char *TAG = "TEST_LEDC";

void test_dev_ledc_ctrl(void)
{
    ESP_LOGI(TAG, "=== LEDC Device Test ===");

    /* Get the LCD brightness device */
    periph_ledc_handle_t *ledc_handle = NULL;
    esp_err_t ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS, (void**)&ledc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LCD brightness device");
        return;
    }

    /* Test different brightness levels */
    const int brightness_levels[] = {0, 25, 50, 75, 100};
    const int num_levels = sizeof(brightness_levels) / sizeof(brightness_levels[0]);
    dev_ledc_ctrl_config_t *dev_ledc_cfg = {0};
    esp_err_t config_ret = esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS, (void*)&dev_ledc_cfg);
    if (config_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LEDC peripheral config '%s': %s", "lcd_brightness", esp_err_to_name(config_ret));
        return;
    }
    periph_ledc_config_t *ledc_config = NULL;
    esp_board_periph_get_config(dev_ledc_cfg->ledc_name, (void**)&ledc_config);

    ESP_LOGI(TAG, "Testing LCD brightness control...");
    for (int i = 0; i < num_levels; i++) {
        int brightness = brightness_levels[i];
        // Calculate duty cycle (0-100% to 0-1023 for 10-bit resolution)
        uint32_t duty = (brightness * ((1 << (uint32_t)ledc_config->duty_resolution) - 1)) / 100;

        ESP_LOGI(TAG, "Setting brightness to %d%% (duty: %lu)", brightness, duty);

        // Set LEDC duty cycle using the handle
        esp_err_t ret = ledc_set_duty(ledc_handle->speed_mode, ledc_handle->channel, duty);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set duty cycle: %s", esp_err_to_name(ret));
            continue;
        }

        // Update the duty cycle
        ret = ledc_update_duty(ledc_handle->speed_mode, ledc_handle->channel);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update duty cycle: %s", esp_err_to_name(ret));
            continue;
        }

        ESP_LOGI(TAG, "Brightness set to %d%% successfully", brightness);

        // Wait for 1 second before next level
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Test smooth brightness transition
    ESP_LOGI(TAG, "Testing smooth brightness transition...");

    for (int brightness = 0; brightness <= 100; brightness += 5) {
        uint32_t duty = (brightness * ((1 << (uint32_t)ledc_config->duty_resolution) - 1)) / 100;

        esp_err_t ret = ledc_set_duty(ledc_handle->speed_mode, ledc_handle->channel, duty);
        if (ret == ESP_OK) {
            ledc_update_duty(ledc_handle->speed_mode, ledc_handle->channel);
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay for smooth transition
    }

    // Fade back to 0
    for (int brightness = 100; brightness >= 0; brightness -= 5) {
        uint32_t duty = (brightness * ((1 << (uint32_t)ledc_config->duty_resolution) - 1)) / 100;

        esp_err_t ret = ledc_set_duty(ledc_handle->speed_mode, ledc_handle->channel, duty);
        if (ret == ESP_OK) {
            ledc_update_duty(ledc_handle->speed_mode, ledc_handle->channel);
        }

        vTaskDelay(pdMS_TO_TICKS(100)); // 100ms delay for smooth transition
    }

    ESP_LOGI(TAG, "LEDC device test completed successfully!");
}
