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

static const char *TAG = "DEV_CUSTOM";

// Helper function to find custom device descriptor
static const custom_device_desc_t *find_custom_device_desc(const char *device_name)
{
    extern const custom_device_desc_t _custom_devices_array_start;
    extern const custom_device_desc_t _custom_devices_array_end;
    for (const custom_device_desc_t *desc = &_custom_devices_array_start;
         desc != &_custom_devices_array_end; desc++) {
        if (strcmp(desc->device_name, device_name) == 0) {
            return desc;
        }
    }
    return NULL;
}

int dev_custom_init(void *cfg, int cfg_size, void **device_handle)
{
    if (!cfg || !device_handle || cfg_size <= 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    // Cast to base config to get device name
    dev_custom_base_config_t *base_cfg = (dev_custom_base_config_t *)cfg;
    const custom_device_desc_t *desc = find_custom_device_desc(base_cfg->name);
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
    const custom_device_desc_t *desc = find_custom_device_desc(board_device->name);
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
