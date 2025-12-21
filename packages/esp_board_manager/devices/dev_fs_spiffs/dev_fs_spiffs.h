/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_spiffs.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  SPIFFS filesystem configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         a SPIFFS filesystem device, including base path, partition label, and formatting options.
 */
typedef struct {
    const char  *name;                    /*!< Device name */
    const char  *base_path;               /*!< Base path for mounting */
    const char  *partition_label;         /*!< Partition label */
    uint8_t      max_files;               /*!< Maximum number of files */
    bool         format_if_mount_failed;  /*!< Format if mount failed */
} dev_fs_spiffs_config_t;

/**
 * @brief  Initialize SPIFFS filesystem device
 *
 * @param[in]   cfg            Pointer to device configuration
 * @param[in]   cfg_size       Size of configuration structure
 * @param[out]  device_handle  Still NULL to return, due to the SPIFFS filesystem is a virtual filesystem, not a physical device
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_fs_spiffs_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize SPIFFS filesystem device
 *
 * @param[in]  device_handle  Device handle
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_fs_spiffs_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
