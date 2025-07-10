/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_ae_eq.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief  Default EQ parameters
 *         If `para` is NULL in the `esp_ae_eq_cfg_t` , the EQ will use `esp_gmf_default_eq_paras` as default
 *         And the `filter_num` will be set to sizeof(esp_gmf_default_eq_paras) / sizeof(esp_ae_eq_filter_para_t)
 */
#define DEFAULT_ESP_GMF_EQ_CONFIG() {  \
    .sample_rate     = 48000,          \
    .channel         = 2,              \
    .bits_per_sample = 16,             \
    .filter_num      = 0,              \
    .para            = NULL,           \
}

/**
 * @brief  Initializes the GMF EQ with the provided configuration
 *
 * @param[in]   config  Pointer to the EQ configuration
 * @param[out]  handle  Pointer to the EQ handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_eq_init(esp_ae_eq_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set the filter parameters for a specific filter identified by 'idx'
 *
 * @param[in]  handle  The EQ handle
 * @param[in]  idx     The index of a specific filter for which the parameters are to be set.
 *                     eg: 0 refers to the first filter
 * @param[in]  para    The filter setup parameter
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_eq_set_para(esp_gmf_element_handle_t handle, uint8_t idx, esp_ae_eq_filter_para_t *para);

/**
 * @brief  Get the filter parameters for a specific filter identified by 'idx'
 *
 * @param[in]   handle  The EQ handle
 * @param[in]   idx     The index of a specific filter for which the parameters are to be retrieved.
 *                      eg: 0 refers to first filter
 * @param[out]  para    The filter setup parameter
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_eq_get_para(esp_gmf_element_handle_t handle, uint8_t idx, esp_ae_eq_filter_para_t *para);

/**
 * @brief  Choose to enable or disable filter processing for a specific filter identified by 'idx' in the equalizer
 *
 * @param[in]  handle     The EQ handle
 * @param[in]  idx        The index of a specific filter to be enabled
 * @param[in]  is_enable  The flag of whether to enable band filter processing
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_eq_enable_filter(esp_gmf_element_handle_t handle, uint8_t idx, bool is_enable);

#ifdef __cplusplus
}
#endif /* __cplusplus */
