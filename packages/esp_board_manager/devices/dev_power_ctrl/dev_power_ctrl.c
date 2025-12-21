/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "dev_power_ctrl.h"
#include "esp_board_entry.h"
#include "esp_board_device.h"

static const char *TAG = "DEV_POWER_CTRL";

int dev_power_ctrl_init(void *cfg, int cfg_size, void **device_handle)
{
    if (cfg == NULL || device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    if (cfg_size != sizeof(dev_power_ctrl_config_t)) {
        ESP_LOGE(TAG, "Invalid config size");
        return -1;
    }

    esp_err_t ret = ESP_FAIL;
    dev_power_ctrl_handle_t *handle = NULL;
    const dev_power_ctrl_config_t *config = (const dev_power_ctrl_config_t *)cfg;

    // Find the peripheral entry description based on sub_type
    const esp_board_entry_desc_t *entry_desc = esp_board_entry_find_desc(config->sub_type);
    if (entry_desc == NULL) {
        ESP_LOGE(TAG, "Failed to find sub device: %s", config->sub_type);
        return -1;
    }

    ret = entry_desc->init_func((void *)config, cfg_size, (void **)&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sub device: %s", config->sub_type);
        return -1;
    }

    ESP_LOGI(TAG, "Power control device initialized successfully, sub_type: %s", config->sub_type);
    *device_handle = handle;
    return 0;
}

int dev_power_ctrl_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    // Get configuration to find the peripheral entry
    dev_power_ctrl_config_t *config = NULL;
    esp_err_t ret = esp_board_device_get_config_by_handle(device_handle, (void **)&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get device configuration");
        return -1;
    }

    const esp_board_entry_desc_t *entry_desc = esp_board_entry_find_desc(config->sub_type);
    if (entry_desc == NULL) {
        ESP_LOGE(TAG, "Failed to find sub device: %s", config->sub_type);
        return -1;
    }

    ret = entry_desc->deinit_func(device_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize sub device: %s", config->sub_type);
        return -1;
    }

    ESP_LOGI(TAG, "Power control device deinitialized successfully, sub_type: %s", config->sub_type);
    return 0;
}
