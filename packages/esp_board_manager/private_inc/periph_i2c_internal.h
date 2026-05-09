/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Publish the 8-bit I2C address selected by a device.
 *
 *         This is an internal Board Manager helper. Applications should use
 *         esp_board_device_get_i2c_effective_addr() instead.
 */
esp_err_t periph_i2c_set_effective_addr_internal(const char *i2c_name,
                                                 const char *device_name,
                                                 uint16_t addr);

/**
 * @brief  Get the 8-bit I2C address published for a device.
 *
 *         This is an internal Board Manager helper. Applications should use
 *         esp_board_device_get_i2c_effective_addr() instead.
 */
esp_err_t periph_i2c_get_effective_addr_internal(const char *device_name,
                                                 uint16_t *addr);

/**
 * @brief  Clear the 8-bit I2C address published for a device.
 *
 *         This is an internal Board Manager helper.
 */
esp_err_t periph_i2c_clear_effective_addr_internal(const char *device_name);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
