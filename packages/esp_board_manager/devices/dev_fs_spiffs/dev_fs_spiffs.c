/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "dev_fs_spiffs.h"
#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "DEV_FS_SPIFFS";

int dev_fs_spiffs_init(void *cfg, int cfg_size, void **device_handle)
{
    if (!cfg || !device_handle) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_fs_spiffs_config_t *config = (dev_fs_spiffs_config_t *)cfg;
    esp_vfs_spiffs_conf_t *fs_config = calloc(1, sizeof(esp_vfs_spiffs_conf_t));

    if (!fs_config) {
        ESP_LOGE(TAG, "Failed to allocate memory for filesystem configuration");
        return -1;
    }

    fs_config->base_path = config->base_path;
    fs_config->partition_label = config->partition_label;
    fs_config->max_files = config->max_files;
    fs_config->format_if_mount_failed = config->format_if_mount_failed;
    ESP_LOGI(TAG, "SPIFFS config: name=%s, base_path=%s, partition_label=%s, max_files=%d, format_if_mount_failed=%d",
        config->name, config->base_path, config->partition_label ? config->partition_label : "NULL", config->max_files,
        config->format_if_mount_failed);

    esp_err_t ret = esp_vfs_spiffs_register(fs_config);
    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount or format filesystem");
        } else if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "Failed to find SPIFFS partition");
        } else {
            ESP_LOGE(TAG, "Failed to initialize SPIFFS (%s)", esp_err_to_name(ret));
        }
        free(fs_config);
        return -1;
    }

    // Get filesystem information
    size_t total_size, used_size;
    ret = esp_spiffs_info(config->partition_label, &total_size, &used_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total_size, used_size);
    }

    *device_handle = fs_config;
    return 0;
}

int dev_fs_spiffs_deinit(void *device_handle)
{
    if (!device_handle) {
        ESP_LOGE(TAG, "Invalid device handle");
        return -1;
    }

    esp_vfs_spiffs_conf_t *fs_config = (esp_vfs_spiffs_conf_t *)device_handle;

    // Unregister SPIFFS
    esp_err_t ret = esp_vfs_spiffs_unregister(fs_config->partition_label);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unregister SPIFFS filesystem (%s)", esp_err_to_name(ret));
        return -1;
    }

    // Free allocated memory
    free(fs_config);

    ESP_LOGI(TAG, "SPIFFS filesystem device deinitialized");
    return 0;
}
