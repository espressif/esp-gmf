/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "dev_fs_fat.h"
#include "esp_board_periph.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"
#include "periph_spi.h"

static const char *TAG = "DEV_FS_FAT_SUB_SPI";

int dev_fs_fat_sub_spi_init(void *cfg, int cfg_size, void **device_handle)
{
    // No need to check parameters here, it will be checked in dev_fs_fat_init
    const dev_fs_fat_config_t *config = (const dev_fs_fat_config_t *)cfg;
    const dev_fs_fat_spi_sub_config_t *spi_cfg = &config->sub_cfg.spi;
    periph_spi_handle_t *spi_handle = NULL;
    if (spi_cfg->spi_bus_name && spi_cfg->spi_bus_name[0]) {
        // TODO Use the reference SPI peripheral handle
        int ret = esp_board_periph_get_handle(spi_cfg->spi_bus_name, (void **)&spi_handle);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to get SPI peripheral handle: %d", ret);
            return -1;
        }
    } else {
        ESP_LOGE(TAG, "Invalid SPI bus name");
        return -1;
    }

    esp_err_t ret = 0;
    dev_fs_fat_handle_t *handle = (dev_fs_fat_handle_t *)*device_handle;
    // Use SDSPI host
    handle->host = (sdmmc_host_t)SDSPI_HOST_DEFAULT();
    handle->host.max_freq_khz = config->frequency;
    handle->host.slot = spi_handle->spi_port;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = spi_cfg->cs_gpio_num;
    slot_config.host_id = handle->host.slot;
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = config->vfs_config.format_if_mount_failed,
        .max_files = config->vfs_config.max_files,
        .allocation_unit_size = config->vfs_config.allocation_unit_size,
    };

    ESP_LOGD(TAG, "host: flags=0x%" PRIx32 ", slot=%d, max_freq_khz=%d, io_voltage=%.1f, command_timeout_ms=%d",
             handle->host.flags, handle->host.slot, handle->host.max_freq_khz, handle->host.io_voltage, handle->host.command_timeout_ms);
    ESP_LOGI(TAG, "slot_config: host_id=%d, gpio_cs=%d", slot_config.host_id, slot_config.gpio_cs);
    ESP_LOGD(TAG, "mount_config: format_if_mount_failed=%d, max_files=%d, allocation_unit_size=%d",
             mount_config.format_if_mount_failed, mount_config.max_files, mount_config.allocation_unit_size);

    ESP_LOGI(TAG, "Mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(config->mount_point, &handle->host, &slot_config, &mount_config, &handle->card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem");
        return -1;
    }
    return 0;
}

int dev_fs_fat_sub_spi_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    dev_fs_fat_handle_t *handle = (dev_fs_fat_handle_t *)device_handle;
    if (handle->mount_point && handle->mount_point[0] && handle->card) {
        esp_vfs_fat_sdcard_unmount(handle->mount_point, handle->card);
    } else {
        ESP_LOGW(TAG, "Mount point or card handle is NULL, skipping unmount");
    }
    dev_fs_fat_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg) {
        esp_err_t ret = esp_board_periph_unref_handle(cfg->sub_cfg.spi.spi_bus_name);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to unref SPI peripheral handle: %s", esp_err_to_name(ret));
        }
    }
    return 0;
}

ESP_BOARD_ENTRY_IMPLEMENT(spi, dev_fs_fat_sub_spi_init, dev_fs_fat_sub_spi_deinit);
