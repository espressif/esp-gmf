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
#include "esp_lcd_panel_dev.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  LCD display configuration structure for SPI interface
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         an LCD display device over SPI, including chip type, panel configuration,
 *         SPI name, and panel IO configuration.
 */
typedef struct {
    const char                    *name;           /*!< Device name */
    const char                    *chip;           /*!< LCD chip type */
    const char                    *type;           /*!< Display type */
    esp_lcd_panel_dev_config_t     panel_config;   /*!< LCD panel device configuration */
    const char                    *spi_name;       /*!< SPI bus name */
    esp_lcd_panel_io_spi_config_t  io_spi_config;  /*!< SPI panel IO configuration */
    uint8_t                        swap_xy  : 1;   /*!< Swap X and Y coordinates */
    uint8_t                        mirror_x : 1;   /*!< Mirror X coordinates */
    uint8_t                        mirror_y : 1;   /*!< Mirror Y coordinates */
    uint16_t                       x_max;          /*!< X coordinates max  */
    uint16_t                       y_max;          /*!< Y coordinates max */
    bool                           invert_color;   /*!< Invert color flag */
} dev_display_lcd_spi_config_t;

/**
 * @brief  LCD display device handles structure
 *
 *         This structure contains the handles for the LCD panel IO and panel device.
 */
typedef struct {
    esp_lcd_panel_io_handle_t  io_handle;     /*!< LCD panel IO handle */
    esp_lcd_panel_handle_t     panel_handle;  /*!< LCD panel device handle */
} dev_display_lcd_spi_handles_t;

/**
 * @brief  Initialize the LCD display device with the given configuration
 *
 *         This function initializes an LCD display device using the provided configuration structure.
 *         It sets up the necessary hardware interfaces (SPI, GPIO, etc.) and allocates resources
 *         for the display. The resulting device handle can be used for further display operations.
 *
 * @param[in]   cfg            Pointer to the LCD display configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  device_handle  Pointer to a variable to receive the dev_display_lcd_spi_handles_t handle
 *
 * @return
 *       - 0               On success
 *       - Negative value  On failure
 */
int dev_display_lcd_spi_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize the LCD display device
 *
 *         This function deinitializes the LCD display device and frees the allocated resources.
 *         It should be called when the device is no longer needed to prevent memory leaks.
 *
 * @param[in]  device_handle  Pointer to the device handle to be deinitialized
 *
 * @return
 *       - 0               On success
 *       - Negative value  On failure
 */
int dev_display_lcd_spi_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
