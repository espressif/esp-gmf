/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "dev_custom.h"
#include "esp_log.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"

static const char *TAG = "DEV_CUSTOM";

int dev_custom_init(void *cfg, int cfg_size, void **device_handle)
{
    if (!cfg || !device_handle || cfg_size <= 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    // Cast to base config to get device name
    dev_custom_base_config_t *base_cfg = (dev_custom_base_config_t *)cfg;
    const esp_board_entry_desc_t *desc = esp_board_entry_find_desc(base_cfg->name);
    if (desc && desc->init_func) {
        void *user_handle = NULL;
        int ret = desc->init_func(cfg, cfg_size, &user_handle);
        if (ret != 0) {
            ESP_LOGE(TAG, "Custom device '%s' init failed with error: %d", base_cfg->name, ret);
            return ret;
        }
        *device_handle = user_handle;
    } else {
        // No custom function registered, use default behavior
        ESP_LOGW(TAG, "No custom init function registered for device '%s', using default behavior", base_cfg->name);
        *device_handle = NULL;
    }
    return 0;
}

int dev_custom_deinit(void *device_handle)
{
    if (!device_handle) {
        ESP_LOGE(TAG, "Invalid device handle");
        return -1;
    }
    // First, try to find the device handle in g_esp_board_device_handles
    const esp_board_device_handle_t *board_device = esp_board_device_find_by_handle(device_handle);
    if (board_device == NULL) {
        ESP_LOGE(TAG, "Device handle[%p] not found, deinit failed", device_handle);
        return -1;
    }
    const esp_board_entry_desc_t *desc = esp_board_entry_find_desc(board_device->name);
    if (desc && desc->deinit_func) {
        int ret = desc->deinit_func(device_handle);
        if (ret != 0) {
            ESP_LOGE(TAG, "Custom device '%s' deinit failed with error: %d", board_device->name, ret);
            // Continue with cleanup even if deinit failed
        } else {
            ESP_LOGI(TAG, "Custom device '%s' deinitialized successfully", board_device->name);
        }
    } else {
        ESP_LOGW(TAG, "No custom deinit function found for device '%s'", board_device->name);
    }
    return 0;
}
