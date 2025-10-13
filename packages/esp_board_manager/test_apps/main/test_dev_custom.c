/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include "esp_log.h"
#include "dev_custom.h"
#include "esp_board_device.h"
#include "test_dev_custom.h"
#include "gen_board_device_custom.h" // Include generated custom device config header

static const char *TAG = "TEST_DEV_CUSTOM";

void test_dev_custom(void)
{
    ESP_LOGI(TAG, "=== Custom Device Test ===");
    /* Get the custom device handle */
    void *custom_handle = NULL;
    esp_err_t ret = esp_board_device_get_handle("my_custom_sensor", (void**)&custom_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get my_custom_sensor device handle: %s", esp_err_to_name(ret));
        return;
    }
    /* Get the device configuration */
    dev_custom_my_custom_sensor_config_t *custom_config = NULL;
    ret = esp_board_device_get_config("my_custom_sensor", (void**)&custom_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get my_custom_sensor device config: %s", esp_err_to_name(ret));
        return;
    }
    ESP_LOGI(TAG, "✅ Custom device handle obtained successfully");
    ESP_LOGI(TAG, "Device info:");
    ESP_LOGI(TAG, "  Name: %s", custom_config->name);
    ESP_LOGI(TAG, "  Chip: %s", custom_config->chip);
    ESP_LOGI(TAG, "  Type: %s", custom_config->type);
    ESP_LOGI(TAG, "  Sensor ID: 0x%02X", custom_config->sensor_id);
    ESP_LOGI(TAG, "  Sample Rate: %d Hz", custom_config->sample_rate);
    ESP_LOGI(TAG, "  Enable Filter: %s", custom_config->enable_filter ? "true" : "false");
    ESP_LOGI(TAG, "  Filter Cutoff: %.1f", custom_config->filter_cutoff);
    ESP_LOGI(TAG, "  Timeout: %d ms", custom_config->timeout_ms);
    ESP_LOGI(TAG, "  Debug Mode: %s", custom_config->debug_mode ? "true" : "false");
    ESP_LOGI(TAG, "  Peripheral Count: %d", custom_config->peripheral_count);
    if (custom_config->peripheral_count > 0) {
        for (int i = 0; i < custom_config->peripheral_count; i++) {
            ESP_LOGI(TAG, "    Peripheral %d: %s", i, custom_config->peripheral_names[i]);
        }
    }
    // Test custom device functionality
    if (custom_handle) {
        ESP_LOGI(TAG, "Custom device functionality test:");
        ESP_LOGI(TAG, "  Device handle obtained: %p", custom_handle);

        uint32_t *user_handle = (uint32_t *)custom_handle;
        if (*user_handle !=  0x99887766) {
            ESP_LOGE(TAG, "User handle is not specified value 0x99887766, it's %p", user_handle);
        }

        // Test configuration access
        ESP_LOGI(TAG, "  Configuration test:");
        ESP_LOGI(TAG, "    Sensor ID from config: 0x%02X", custom_config->sensor_id);
        ESP_LOGI(TAG, "    Sample rate from config: %d Hz", custom_config->sample_rate);
    }
    ESP_LOGI(TAG, "Custom device test completed successfully!");
}
