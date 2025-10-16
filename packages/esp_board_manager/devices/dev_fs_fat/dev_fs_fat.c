/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "esp_board_periph.h"
#include "dev_fs_fat.h"
#include "esp_board_entry.h"
#include "esp_board_device.h"

static const char *TAG = "DEV_FS_FAT";

int dev_fs_fat_init(void *cfg, int cfg_size, void **device_handle)
{
    if (cfg == NULL || device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    if (cfg_size != sizeof(dev_fs_fat_config_t)) {
        ESP_LOGE(TAG, "Invalid config size");
        return -1;
    }
    esp_err_t ret = ESP_FAIL;
    dev_fs_fat_handle_t *handle = calloc(1, sizeof(dev_fs_fat_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return -1;
    }

    const dev_fs_fat_config_t *config = (const dev_fs_fat_config_t *)cfg;
    const esp_board_entry_desc_t *entry_desc = esp_board_entry_find_desc(config->sub_type);
    if (entry_desc == NULL) {
        ESP_LOGE(TAG, "Failed to find sub device: %s", config->sub_type);
        goto cleanup;
    }
    ret = entry_desc->init_func((void *)config, cfg_size, (void **)&handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize sub device: %s", config->sub_type);
        goto cleanup;
    }
    // Save mount point
    handle->mount_point = strdup(config->mount_point);
    if (handle->mount_point == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mount point");
        esp_vfs_fat_sdcard_unmount(config->mount_point, handle->card);
        goto cleanup;
    }

    ESP_LOGI(TAG, "Filesystem mounted, base path: %s", config->mount_point);
    *device_handle = handle;
    return 0;
cleanup:
    free(handle);
    return -1;
}

int dev_fs_fat_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    dev_fs_fat_config_t *cfg = NULL;
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
            ESP_LOGW(TAG, "No custom deinit function found for sub type '%s'", cfg->sub_type);
        }
    }
    dev_fs_fat_handle_t *handle = (dev_fs_fat_handle_t *)device_handle;
    free((char *)handle->mount_point);
    free(handle);
    return 0;
}
