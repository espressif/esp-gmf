/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once
#include "driver/i2c_master.h"
#include "esp_board_periph.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize the I2C master bus
 *
 *         This function initializes the I2C master bus using the provided configuration structure.
 *
 * @param[in]   cfg            Pointer to i2c_master_bus_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the returned i2c_master_bus_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_i2c_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize the I2C master bus
 *
 *         This function deinitializes the I2C master bus and frees the allocated resources.
 *         It should be called when the bus is no longer needed.
 *
 * @param[in]  periph_handle  Handle returned by periph_i2c_init
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_i2c_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
