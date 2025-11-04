/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "driver/sdmmc_host.h"
#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  SD card handle structure
 *         This structure contains the handle for the SD card device, including the card, host, and mount point
 */
typedef struct {
    sdmmc_card_t  *card;         /*!< SD card card handle */
    sdmmc_host_t   host;         /*!< SD card host handle */
    char          *mount_point;  /*!< Mount point path */
} dev_fatfs_sdcard_handle_t;

/**
 * @brief  SD card pin configuration structure
 *
 *         This structure defines the GPIO pins used for SD card communication,
 *         including clock, command, data lines, and optional card detect/write protect pins.
 */
typedef struct {
    int  clk;  /*!< Clock pin number */
    int  cmd;  /*!< Command pin number */
    int  d0;   /*!< Data line 0 pin number */
    int  d1;   /*!< Data line 1 pin number */
    int  d2;   /*!< Data line 2 pin number */
    int  d3;   /*!< Data line 3 pin number */
    int  d4;   /*!< Data line 4 pin number (for 8-bit mode) */
    int  d5;   /*!< Data line 5 pin number (for 8-bit mode) */
    int  d6;   /*!< Data line 6 pin number (for 8-bit mode) */
    int  d7;   /*!< Data line 7 pin number (for 8-bit mode) */
    int  cd;   /*!< Card detect pin number */
    int  wp;   /*!< Write protect pin number */
} dev_sdcard_pins_t;

/**
 * @brief  SD card VFS (Virtual File System) configuration structure
 */
typedef struct {
    bool      format_if_mount_failed;  /*!< Format the card if mount fails */
    uint32_t  max_files;               /*!< Maximum number of files that can be open simultaneously */
    uint32_t  allocation_unit_size;    /*!< Allocation unit size in bytes */
} dev_sdcard_vfs_config_t;

/**
 * @brief  SD card configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         an SD card device, including mount point, VFS settings, bus configuration,
 *         and GPIO pin assignments.
 */
typedef struct {
    const char              *name;         /*!< SD card device name */
    const char              *mount_point;  /*!< Mount point path */
    dev_sdcard_vfs_config_t  vfs_config;   /*!< VFS configuration */
    uint32_t                 frequency;    /*!< SD card clock frequency in Hz */
    uint8_t                  slot;         /*!< SD card slot number */
    uint8_t                  bus_width;    /*!< Bus width (1, 4, or 8 bits) */
    uint16_t                 slot_flags;   /*!< Slot flags (SDMMC_HOST_FLAG_1BIT, SDMMC_HOST_FLAG_4BIT, SDMMC_HOST_FLAG_SPI) */
    dev_sdcard_pins_t        pins;         /*!< GPIO pin configuration */
    int8_t                   ldo_chan_id;  /*!< Set to the appropriate LDO channel ID if using on-chip LDO for SDMMC power.
                                                This is an optional configuration, and whether it needs to be configured depends on your board.
                                                Please check the schematic diagram or other documentation to determine if SDMMC is powered by LDO. */
} dev_fatfs_sdcard_config_t;

/**
 * @brief  Initialize SD card device
 *
 *         This function initializes an SD card device using the provided configuration structure.
 *         It mounts the SD card to the specified mount point and sets up the VFS interface.
 *
 * @param[in]   cfg            Pointer to the SD card configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  device_handle  Pointer to a variable to receive the dev_fatfs_sdcard_handle_t handle
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_fatfs_sdcard_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize SD card device
 *
 *         This function deinitializes the SD card device and unmounts it from the VFS.
 *         It should be called when the device is no longer needed.
 *
 * @param[in]  device_handle  Pointer to the device handle to be deinitialized
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_fatfs_sdcard_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
