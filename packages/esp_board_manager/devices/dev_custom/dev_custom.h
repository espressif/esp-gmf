/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_board_manager_err.h"
#include "esp_board_entry.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Maximum number of peripherals supported by custom device
 */
#define MAX_PERIPHERALS 4

/**
 * @brief  Generic custom device configuration structure
 *
 *         This is a base structure that will be extended by dynamically generated
 *         structures based on the YAML configuration.
 */
typedef struct {
    const char  *name;                               /*!< Custom device name */
    const char  *type;                               /*!< Device type: "custom" */
    const char  *chip;                               /*!< Chip name */
    uint8_t      peripheral_count;                   /*!< Number of peripherals */
    const char  *peripheral_names[MAX_PERIPHERALS];  /*!< Peripheral names array */
} dev_custom_base_config_t;

/**
 * @brief  Initialize custom device
 *
 * @param[in]   cfg            Pointer to device configuration
 * @param[in]   cfg_size       Size of configuration structure
 * @param[out]  device_handle  Pointer to store device handle
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_custom_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize custom device
 *
 * @param[in]  device_handle  Device handle
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_custom_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
