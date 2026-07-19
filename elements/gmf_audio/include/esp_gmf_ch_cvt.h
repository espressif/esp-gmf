/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_ae_ch_cvt.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define DEFAULT_ESP_GMF_CH_CVT_CONFIG() {  \
    .sample_rate     = 48000,              \
    .bits_per_sample = 16,                 \
    .src_ch          = 2,                  \
    .dest_ch         = 2,                  \
    .weight          = NULL,               \
    .weight_len      = 0,                  \
}

/**
 * @brief  Initializes the GMF channel conversion with the provided configuration
 *
 * @param[in]   config  Pointer to the channel conversion configuration
 * @param[out]  handle  Pointer to the channel conversion handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_ch_cvt_init(esp_ae_ch_cvt_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set dest channel in the channel conversion handle
 *
 * @param[in]  handle   The channel conversion handle
 * @param[in]  dest_ch  The dest channel
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 *       - ESP_GMF_ERR_FAIL         Failed to set configuration
 */
esp_gmf_err_t esp_gmf_ch_cvt_set_dest_channel(esp_gmf_element_handle_t handle, uint8_t dest_ch);

/**
 * @brief  Get destination channel count from the channel conversion element
 *
 * @param[in]   handle   Channel conversion element handle
 * @param[out]  dest_ch  Destination channel count
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 *       - ESP_GMF_ERR_FAIL         Configuration is unavailable
 */
esp_gmf_err_t esp_gmf_ch_cvt_get_dest_channel(esp_gmf_element_handle_t handle, uint8_t *dest_ch);

#ifdef __cplusplus
}
#endif /* __cplusplus */
