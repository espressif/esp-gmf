/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "dev_camera.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"

static const char *TAG = "DEV_CAMERA";

int dev_camera_init(void *cfg, int cfg_size, void **device_handle)
{
    if (cfg == NULL || device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    if (cfg_size != sizeof(dev_camera_config_t)) {
        ESP_LOGE(TAG, "Invalid config size");
        return -1;
    }
    esp_err_t ret = ESP_FAIL;
    dev_camera_handle_t *handle = NULL;
    const dev_camera_config_t *config = (const dev_camera_config_t *)cfg;
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

    ESP_LOGI(TAG, "Successfully initialized camera device: %s, sub_type: %s, dev_path: %s",
             config->name, config->sub_type, handle->dev_path);
    *device_handle = handle;
    return 0;
}

int dev_camera_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    dev_camera_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg) {
        const esp_board_entry_desc_t *desc = esp_board_entry_find_desc(cfg->sub_type);
        if (desc && desc->deinit_func) {
            int ret = desc->deinit_func(device_handle);
            if (ret != 0) {
                ESP_LOGE(TAG, "Sub device '%s' deinit failed with error: %d", cfg->sub_type, ret);
                // Continue with cleanup even if deinit failed
            } else {
                ESP_LOGI(TAG, "Sub device '%s' deinitialized successfully", cfg->sub_type);
            }
        } else {
            ESP_LOGW(TAG, "No deinit function found for sub type '%s'", cfg->sub_type);
        }
    }
    device_handle = NULL;
    return 0;
}
