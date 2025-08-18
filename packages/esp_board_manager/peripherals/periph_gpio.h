/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_board_periph.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  GPIO peripheral handle
 *
 *         This structure represents a GPIO number
 */
typedef struct {
    gpio_num_t  gpio_num;  /*!< GPIO number */
} periph_gpio_handle_t;

/**
 * @brief  Peripheral GPIO configuration structure
 *
 *         This structure includes the standard gpio_config_t and a default_level field
 *         to specify the initial output level for GPIO pins configured as outputs.
 */
typedef struct {
    gpio_config_t  gpio_config;    /*!< Standard GPIO configuration */
    int            default_level;  /*!< Default output level (0 or 1) for output pins */
} periph_gpio_config_t;

/**
 * @brief  Initialize a GPIO pin or set of pins
 *
 *         This function initializes one or more GPIO pins using the provided configuration structure.
 *         For output pins, it also sets the default level as specified in the configuration.
 *
 * @param[in]   cfg            Pointer to periph_gpio_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Returns the periph_gpio_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_gpio_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize a GPIO pin or set of pins
 *
 *         This function deinitializes one or more GPIO pins.
 *
 * @param[in]  periph_handle  Not used; pass NULL
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_gpio_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
