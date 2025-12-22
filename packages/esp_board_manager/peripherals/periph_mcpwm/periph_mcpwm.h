/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_idf_version.h"
#include "driver/mcpwm_prelude.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    #warning "periph_mcpwm is not supported in ESP-IDF v6.0.0 and above yet."
#endif

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  MCPWM peripheral handle
 *
 *         This structure represents a MCPWM channel handle including timer, operator,
 *         comparators and generator components.
 */
typedef struct {
    mcpwm_timer_handle_t  timer;                                            /*!< MCPWM timer handle */
    mcpwm_oper_handle_t   operator;                                         /*!< MCPWM operator handle */
    int                   num_comparators;                                  /*!< Number of comparators actually created */
    mcpwm_cmpr_handle_t   comparators[SOC_MCPWM_COMPARATORS_PER_OPERATOR];  /*!< MCPWM comparator handles */
    mcpwm_gen_handle_t    generator;                                        /*!< MCPWM generator handle */
} periph_mcpwm_handle_t;

/**
 * @brief  MCPWM peripheral configuration structure
 *
 *         This structure includes all necessary configuration for MCPWM peripheral
 *         including timer, operator, comparators and generator settings.
 */
typedef struct {
    mcpwm_timer_config_t       timer_config;                                         /*!< MCPWM timer configuration */
    mcpwm_operator_config_t    operator_config;                                      /*!< MCPWM operator configuration */
    int                        num_comparators;                                      /*!< Number of comparators to create (0-2) */
    mcpwm_comparator_config_t  comparator_cfgs[SOC_MCPWM_COMPARATORS_PER_OPERATOR];  /*!< Comparator configurations */
    mcpwm_generator_config_t   generator_config;                                     /*!< MCPWM generator configuration */
} periph_mcpwm_config_t;

/**
 * @brief  Initialize the MCPWM peripheral
 *
 *         This function initializes a MCPWM peripheral using the provided configuration structure.
 *         It creates timer, operator, comparator and generator components and sets up the
 *         PWM waveform generation.
 *
 * @param[in]   cfg            Pointer to periph_mcpwm_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the returned periph_mcpwm_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_mcpwm_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize the MCPWM peripheral
 *
 *         This function deinitializes the MCPWM peripheral and frees the allocated resources.
 *
 * @param[in]  periph_handle  Handle returned by periph_mcpwm_init
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_mcpwm_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
