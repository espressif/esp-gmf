/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_ae_delay.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_ESP_GMF_DELAY_CONFIG()  {  \
    .sample_rate     = 48000,              \
    .channel         = 2,                  \
    .bits_per_sample = 16,                 \
    .max_delay_ms    = 500,                \
    .delay_para      = {                   \
        .delay_time_ms = 250,              \
        .feedback      = 0.35f,            \
        .mix           = 0.35f,            \
    },                                     \
}

/**
 * @brief  Initializes the GMF delay element with the provided configuration
 *
 * @param[in]   config  Pointer to the delay configuration. May be NULL for defaults.
 * @param[out]  handle  Pointer to the element handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_delay_init(esp_ae_delay_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set the delay time
 *
 * @param[in]  handle         The delay element handle
 * @param[in]  delay_time_ms  Delay time in milliseconds
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_delay_set_delay_time(esp_gmf_element_handle_t handle, uint16_t delay_time_ms);

/**
 * @brief  Get the delay time
 *
 * @param[in]   handle         The delay element handle
 * @param[out]  delay_time_ms  Delay time in milliseconds
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_delay_get_delay_time(esp_gmf_element_handle_t handle, uint16_t *delay_time_ms);

/**
 * @brief  Set the feedback coefficient
 *
 * @param[in]  handle    The delay element handle
 * @param[in]  feedback  Feedback coefficient, range: [0.0, 0.95]
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_delay_set_feedback(esp_gmf_element_handle_t handle, float feedback);

/**
 * @brief  Get the feedback coefficient
 *
 * @param[in]   handle    The delay element handle
 * @param[out]  feedback  Feedback coefficient
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_delay_get_feedback(esp_gmf_element_handle_t handle, float *feedback);

/**
 * @brief  Set the wet/dry mix ratio
 *
 * @param[in]  handle     The delay element handle
 * @param[in]  mix_ratio  Mix ratio, range: [0.0, 1.0]
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_delay_set_mix_ratio(esp_gmf_element_handle_t handle, float mix_ratio);

/**
 * @brief  Get the wet/dry mix ratio
 *
 * @param[in]   handle     The delay element handle
 * @param[out]  mix_ratio  Mix ratio
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_delay_get_mix_ratio(esp_gmf_element_handle_t handle, float *mix_ratio);

/**
 * @brief  Reset the internal processing state of the delay element
 *
 * @param[in]  handle  The delay element handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_delay_reset(esp_gmf_element_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
