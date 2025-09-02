/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  LCD touch configuration structure for I2C interface
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         an LCD touch device over I2C, including chip type, I2C name, and panel/touch configs.
 */
typedef struct {
    const char                    *name;                /*!< Device name */
    const char                    *chip;                /*!< Touch chip type */
    const char                    *type;                /*!< Touch type */
    const char                    *i2c_name;            /*!< I2C bus name */
    const uint16_t                 i2c_addr[2];         /*!< Can be support two I2C address, the first is the primary address, the second is the secondary address.
                                                             Most of the time, the secondary address is not set, only used some devices is the address is not easy to confirm */
    esp_lcd_panel_io_i2c_config_t  io_i2c_config;       /*!< I2C panel IO configuration */
    esp_lcd_touch_config_t         touch_config;        /*!< Touch configuration */
} dev_lcd_touch_i2c_config_t;

/**
 * @brief  LCD touch device handles structure
 *
 *         This structure contains the handles for the LCD touch and panel IO
 */
typedef struct {
    esp_lcd_touch_handle_t     touch_handle;  /*!< LCD touch handle */
    esp_lcd_panel_io_handle_t  io_handle;     /*!< LCD panel IO handle */
} dev_lcd_touch_i2c_handles_t;

/**
 * @brief  Initialize the I2C LCD touch device
 *
 *         This function initializes the LCD touch device over I2C using the provided configuration
 *
 * @param[in]   cfg            Pointer to dev_lcd_touch_i2c_config_t configuration array
 * @param[in]   cfg_size       Size of the configuration array
 * @param[out]  device_handle  Pointer to the returned dev_lcd_touch_i2c_handles_t
 *
 * @return
 *       - 0               On success
 *       - Negative value  On failure
 */
int dev_lcd_touch_i2c_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize the I2C LCD touch device
 *
 *         This function deinitializes the LCD touch device and frees the allocated resources
 *
 * @param[in]  device_handle  Pointer to the device handle to be deinitialized
 *
 * @return
 *       - 0               On success
 *       - Negative value  On failure
 */
int dev_lcd_touch_i2c_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
