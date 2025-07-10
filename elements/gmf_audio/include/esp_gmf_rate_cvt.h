/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_ae_rate_cvt.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define DEFAULT_ESP_GMF_RATE_CVT_CONFIG() {              \
    .src_rate        = 44100,                            \
    .dest_rate       = 48000,                            \
    .channel         = 2,                                \
    .bits_per_sample = 16,                               \
    .complexity      = 2,                                \
    .perf_type       = ESP_AE_RATE_CVT_PERF_TYPE_SPEED,  \
}

/**
 * @brief  Initializes the GMF rate conversion with the provided configuration
 *
 * @param[in]   config  Pointer to the rate conversion configuration
 * @param[out]  handle  Pointer to the rate conversion handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_rate_cvt_init(esp_ae_rate_cvt_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set dest rate in the rate conversion handle
 *
 * @param[in]  handle   The rate conversion handle
 * @param[in]  dest_ch  The dest rate
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 *       - ESP_GMF_ERR_FAIL         Failed to set configuration
 */
esp_gmf_err_t esp_gmf_rate_cvt_set_dest_rate(esp_gmf_element_handle_t handle, uint32_t dest_rate);

#ifdef __cplusplus
}
#endif /* __cplusplus */
