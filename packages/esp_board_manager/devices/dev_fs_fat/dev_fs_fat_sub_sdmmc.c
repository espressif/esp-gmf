/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "driver/gpio.h"
#include "sdmmc_cmd.h"
#include "esp_vfs.h"
#include "esp_vfs_fat.h"
#include "dev_fs_fat.h"
#include "esp_board_entry.h"
#ifdef SOC_GP_LDO_SUPPORTED
#include "sd_pwr_ctrl_by_on_chip_ldo.h"
#endif  // SOC_GP_LDO_SUPPORTED

static const char *TAG = "DEV_FS_FAT_SUB_SDMMC";

int dev_fs_fat_sub_sdmmc_init(void *cfg, int cfg_size, void **device_handle)
{
    // No need to check parameters here, it will be checked in dev_fs_fat_init
    esp_err_t ret = ESP_FAIL;
    dev_fs_fat_handle_t *handle = (dev_fs_fat_handle_t *)*device_handle;
    const dev_fs_fat_config_t *config = (const dev_fs_fat_config_t *)cfg;
    // Use SDMMC host
    handle->host = (sdmmc_host_t)SDMMC_HOST_DEFAULT();
    handle->host.max_freq_khz = config->frequency;

#ifdef SOC_GP_LDO_SUPPORTED
    if (config->sub_cfg.sdmmc.ldo_chan_id != -1) {
        sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;
        sd_pwr_ctrl_ldo_config_t ldo_config = {
            .ldo_chan_id = config->sub_cfg.sdmmc.ldo_chan_id,
        };
        ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create a new on-chip LDO power control driver");
            return -1;
        }
        handle->host.pwr_ctrl_handle = pwr_ctrl_handle;
    } else {
        ESP_LOGI(TAG, "LDO power control is not used");
    }
#endif  // SOC_GP_LDO_SUPPORTED

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    const dev_fs_fat_sdmmc_sub_config_t *sdmmc_cfg = &config->sub_cfg.sdmmc;
    slot_config.cd = sdmmc_cfg->pins.cd;
    slot_config.wp = sdmmc_cfg->pins.wp;
    slot_config.clk = sdmmc_cfg->pins.clk;
    slot_config.cmd = sdmmc_cfg->pins.cmd;
    slot_config.d0 = sdmmc_cfg->pins.d0;
    slot_config.d1 = sdmmc_cfg->pins.d1;
    slot_config.d2 = sdmmc_cfg->pins.d2;
    slot_config.d3 = sdmmc_cfg->pins.d3;
    slot_config.d4 = sdmmc_cfg->pins.d4;
    slot_config.d5 = sdmmc_cfg->pins.d5;
    slot_config.d6 = sdmmc_cfg->pins.d6;
    slot_config.d7 = sdmmc_cfg->pins.d7;
    slot_config.width = sdmmc_cfg->bus_width;
    slot_config.flags = sdmmc_cfg->slot_flags;

    handle->host.slot = sdmmc_cfg->slot;

    // Mount filesystem
    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = config->vfs_config.format_if_mount_failed,
        .max_files = config->vfs_config.max_files,
        .allocation_unit_size = config->vfs_config.allocation_unit_size};

    ESP_LOGD(TAG, "host: flags=0x%" PRIx32 ", slot=%d, max_freq_khz=%d, io_voltage=%.1f, command_timeout_ms=%d",
             handle->host.flags, handle->host.slot, handle->host.max_freq_khz, handle->host.io_voltage, handle->host.command_timeout_ms);
    ESP_LOGI(TAG, "slot_config: cd=%d, wp=%d, clk=%d, cmd=%d, d0=%d, d1=%d, d2=%d, d3=%d, d4=%d, d5=%d, d6=%d, d7=%d, width=%d, flags=0x%" PRIx32,
             slot_config.cd, slot_config.wp, slot_config.clk, slot_config.cmd, slot_config.d0, slot_config.d1, slot_config.d2, slot_config.d3, slot_config.d4, slot_config.d5, slot_config.d6, slot_config.d7, slot_config.width, slot_config.flags);
    ESP_LOGD(TAG, "mount_config: format_if_mount_failed=%d, max_files=%d, allocation_unit_size=%d",
             mount_config.format_if_mount_failed, mount_config.max_files, mount_config.allocation_unit_size);

    ret = esp_vfs_fat_sdmmc_mount(config->mount_point, &handle->host, &slot_config,
                                  &mount_config, &handle->card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount filesystem");
        return -1;
    }

    return 0;
}

int dev_fs_fat_sub_sdmmc_deinit(void *device_handle)
{
    dev_fs_fat_handle_t *handle = (dev_fs_fat_handle_t *)device_handle;
    if (handle->mount_point && handle->mount_point[0] && handle->card) {
        esp_vfs_fat_sdcard_unmount(handle->mount_point, handle->card);
    } else {
        ESP_LOGW(TAG, "Mount point or card handle is NULL, skipping unmount");
    }
    return 0;
}

ESP_BOARD_ENTRY_IMPLEMENT(sdmmc, dev_fs_fat_sub_sdmmc_init, dev_fs_fat_sub_sdmmc_deinit);
