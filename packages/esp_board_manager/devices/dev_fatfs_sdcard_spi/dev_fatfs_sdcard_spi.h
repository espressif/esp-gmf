/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "sdmmc_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  SPI SD card handle structure
 *         This structure contains the handle for the SPI SD card device, including the card, host, and mount point
 */
typedef struct {
    sdmmc_card_t  *card;         /*!< SPI SD card card handle */
    sdmmc_host_t   host;         /*!< SPI SD card host handle */
    const char    *mount_point;  /*!< Mount point path */
} dev_fatfs_sdcard_spi_handle_t;

/**
 * @brief  SPI SD card VFS (Virtual File System) configuration structure
 */
typedef struct {
    bool      format_if_mount_failed;  /*!< Format the card if mount fails */
    uint16_t  max_files;               /*!< Maximum number of files that can be open simultaneously */
    uint16_t  allocation_unit_size;    /*!< Allocation unit size in bytes */
} dev_sdcard_spi_vfs_config_t;

/**
 * @brief  SPI SD card configuration structure
 *
 *         This structure contains the configuration parameters needed to initialize
 *         an SPI SD card device, including the name, mount point, frequency, VFS configuration,
 *         and sub-type-specific configuration.
 */
typedef struct {
    const char                  *name;
    const char                  *mount_point;   /*!< Mount point path */
    uint32_t                     frequency;     /*!< SPI SD card clock frequency in Hz */
    dev_sdcard_spi_vfs_config_t  vfs_config;    /*!< VFS configuration */
    int                          cs_gpio_num;   /*!< Chip select GPIO number */
    const char                  *spi_bus_name;  /*!< SPI bus name */
} dev_fatfs_sdcard_spi_config_t;

/**
 * @brief  Initialize SPI SD card device
 *
 *         This function initializes an SPI SD card device using the provided configuration structure.
 *         It mounts the SPI SD card to the specified mount point and sets up the VFS interface.
 *
 * @param[in]   cfg            Pointer to the SPI SD card configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  device_handle  Pointer to a variable to receive the dev_fatfs_sdcard_spi_handle_t handle
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_fatfs_sdcard_spi_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize SPI SD card device
 *
 *         This function deinitializes the SPI SD card device and unmounts it from the VFS.
 *         It should be called when the device is no longer needed.
 *
 * @param[in]  device_handle  Pointer to the device handle to be deinitialized
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_fatfs_sdcard_spi_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
