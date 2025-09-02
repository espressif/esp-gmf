/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_board_periph.h"
#include "driver/ledc.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  LEDC channel handle structure
 *
 *         This structure contains the configuration for a single LEDC channel,
 *         including channel number, GPIO, and speed mode.
 */
typedef struct {
    ledc_channel_t  channel;     /*!< LEDC channel number */
    ledc_mode_t     speed_mode;  /*!< LEDC speed mode */
} periph_ledc_handle_t;

/**
 * @brief  LEDC configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         a LEDC channel, including duty, frequency, timer, and output settings.
 */
typedef struct {
    periph_ledc_handle_t  handle;           /*!< LEDC channel handle */
    int                   gpio_num;         /*!< GPIO number for output */
    uint32_t              duty;             /*!< Duty cycle */
    uint32_t              freq_hz;          /*!< Frequency in Hz */
    ledc_timer_bit_t      duty_resolution;  /*!< Duty resolution */
    ledc_timer_t          timer_sel;        /*!< Timer selection */
    ledc_sleep_mode_t     sleep_mode;       /*!< Sleep mode */
    bool                  output_invert;    /*!< Output invert flag */
} periph_ledc_config_t;

/**
 * @brief  Initialize a LEDC channel
 *
 *         This function initializes a LEDC channel using the provided configuration structure.
 *         It sets up the LEDC hardware with the specified parameters.
 *
 * @param[in]   cfg            Pointer to ledc_channel_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the periph_ledc_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_ledc_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize a LEDC channel
 *
 *         This function deinitializes the LEDC channel and frees the allocated resources.
 *         It should be called when the channel is no longer needed.
 *
 * @param[in]  periph_handle  LEDC channel handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_ledc_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
