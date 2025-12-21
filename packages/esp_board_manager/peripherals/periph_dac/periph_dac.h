/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once
#include "driver/dac_oneshot.h"
#include "driver/dac_continuous.h"
#include "driver/dac_cosine.h"
#include "esp_board_manager_defs.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  DAC peripheral handle structure
 *
 *         This structure uses union to support oneshot, continuous and cosine handles
 */
typedef union {
    dac_oneshot_handle_t     oneshot;     /*!< Oneshot mode handle */
    dac_continuous_handle_t  continuous;  /*!< Continuous mode handle */
    dac_cosine_handle_t      cosine;      /*!< Cosine wave mode handle */
} periph_dac_handle_t;

/**
 * @brief  DAC peripheral configuration structure
 *
 *         This structure uses union to support oneshot, continuous and cosine modes
 */
typedef struct {
    esp_board_periph_role_t  role;  /*!< DAC role type */
    union {
        dac_oneshot_config_t     oneshot_cfg;     /*!< Oneshot mode configuration */
        dac_continuous_config_t  continuous_cfg;  /*!< Continuous mode configuration */
        dac_cosine_config_t      cosine_cfg;      /*!< Cosine wave mode configuration */
    };
} periph_dac_config_t;

/**
 * @brief  Initialize a DAC peripheral
 *
 *         This function initializes a DAC peripheral using the provided configuration structure.
 *         It supports both oneshot and continuous modes based on the role field.
 *
 * @param[in]   cfg            Pointer to periph_dac_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Returns the periph_dac_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_dac_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize a DAC peripheral
 *
 *         This function deinitializes a DAC peripheral and frees the allocated resources.
 *
 * @param[in]  periph_handle  Handle returned by periph_dac_init
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_dac_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
