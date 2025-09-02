/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include "driver/ledc.h"
#include "periph_ledc.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  LEDC control device configuration structure
 */
typedef struct {
    const char *name;             /*!< LEDC control device name */
    const char *type;             /*!< Device type: "ledc_ctrl" */
    const char *ledc_name;        /*!< LEDC peripheral name */
    uint32_t    default_percent;  /*!< Default brightness percentage (0-100) */
} dev_ledc_ctrl_config_t;

/**
 * @brief  Initialize LEDC control device
 *
 * @param[in]   cfg            Pointer to device configuration
 * @param[in]   cfg_size       Size of configuration structure
 * @param[out]  device_handle  Pointer to store periph_ledc_handle_t handle
 *
 * @return
 *       - 0               On success
 *       - Negative value  On failure
 */
int dev_ledc_ctrl_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize LEDC control device
 *
 * @param[in]  device_handle  Device handle
 *
 * @return
 *       - 0               On success
 *       - Negative value  On failure
 */
int dev_ledc_ctrl_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
