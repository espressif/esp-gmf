/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "periph_spi.h"
#include "dev_fatfs_sdcard_spi.h"
#include "esp_board_device.h"

static const char *TAG = "DEV_FATFS_SDCARD_SPI";

int dev_fatfs_sdcard_spi_init(void *cfg, int cfg_size, void **device_handle)
{
    if (cfg == NULL || device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    if (cfg_size != sizeof(dev_fatfs_sdcard_spi_config_t)) {
        ESP_LOGE(TAG, "Invalid config size");
        return -1;
    }

    const dev_fatfs_sdcard_spi_config_t *config = (const dev_fatfs_sdcard_spi_config_t *)cfg;
    periph_spi_handle_t *spi_handle = NULL;
    if (config->spi_bus_name && config->spi_bus_name[0]) {
        int ret = esp_board_periph_ref_handle(config->spi_bus_name, (void **)&spi_handle);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to get SPI peripheral handle: %d", ret);
            return -1;
        }
    } else {
        ESP_LOGE(TAG, "Invalid SPI bus name");
        return -1;
    }

    esp_err_t ret = 0;
    dev_fatfs_sdcard_spi_handle_t *handle = (dev_fatfs_sdcard_spi_handle_t *)calloc(1, sizeof(dev_fatfs_sdcard_spi_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        return -1;
    }

    // Use SDSPI host
    handle->host = (sdmmc_host_t)SDSPI_HOST_DEFAULT();
    handle->host.max_freq_khz = config->frequency;
    handle->host.slot = spi_handle->spi_port;

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs = config->cs_gpio_num;
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
        goto cleanup;
    }

    // Save mount point
    handle->mount_point = strdup(config->mount_point);
    if (handle->mount_point == NULL) {
        ESP_LOGE(TAG, "Failed to allocate mount point");
        esp_vfs_fat_sdcard_unmount(config->mount_point, handle->card);
        goto cleanup;
    }

    sdmmc_card_print_info(stdout, handle->card);
    ESP_LOGI(TAG, "Filesystem mounted, base path: %s", config->mount_point);
    *device_handle = handle;
    return 0;
cleanup:
    free(handle);
    return -1;
}

int dev_fatfs_sdcard_spi_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    dev_fatfs_sdcard_spi_handle_t *handle = (dev_fatfs_sdcard_spi_handle_t *)device_handle;
    if (handle->mount_point && handle->mount_point[0] && handle->card) {
        esp_vfs_fat_sdcard_unmount(handle->mount_point, handle->card);
    } else {
        ESP_LOGW(TAG, "Mount point or card handle is NULL, skipping unmount");
    }

    dev_fatfs_sdcard_spi_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(device_handle, (void **)&cfg);
    if (cfg) {
        esp_board_periph_unref_handle(cfg->spi_bus_name);
    }

    free((char *)handle->mount_point);
    free(handle);
    return 0;
}
