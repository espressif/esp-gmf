/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_ae_bit_cvt.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define DEFAULT_ESP_GMF_BIT_CVT_CONFIG() {  \
    .sample_rate = 48000,                   \
    .channel     = 2,                       \
    .src_bits    = 16,                      \
    .dest_bits   = 16,                      \
}

/**
 * @brief  Initializes the GMF bit conversion with the provided configuration
 *
 * @param[in]   config  Pointer to the bit conversion configuration
 * @param[out]  handle  Pointer to the bit conversion handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_bit_cvt_init(esp_ae_bit_cvt_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set destination bits for the bit conversion handle
 *
 * @param[in]  handle     The channel bit handle
 * @param[in]  dest_bits  The destination bits
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 *       - ESP_GMF_ERR_FAIL         Failed to set configuration
 */
esp_gmf_err_t esp_gmf_bit_cvt_set_dest_bits(esp_gmf_element_handle_t handle, uint8_t dest_bits);

#ifdef __cplusplus
}
#endif /* __cplusplus */
