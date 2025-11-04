/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include "periph_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  GPIO control device configuration structure
 */
typedef struct {
    const char  *name;           /*!< GPIO control device name */
    const char  *type;           /*!< Device type: "gpio_ctrl" */
    const char  *gpio_name;      /*!< GPIO peripheral name */
    uint8_t      active_level;   /*!< Active level (0 or 1) */
    uint8_t      default_level;  /*!< Default level (0 or 1) */
} dev_gpio_ctrl_config_t;

/**
 * @brief  Initialize GPIO control device
 *
 * @param[in]   cfg            Pointer to device configuration
 * @param[in]   cfg_size       Size of configuration structure
 * @param[out]  device_handle  Pointer to store periph_gpio_handle_t handle
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_gpio_ctrl_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize GPIO control device
 *
 * @param[in]  device_handle  Device handle
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_gpio_ctrl_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
