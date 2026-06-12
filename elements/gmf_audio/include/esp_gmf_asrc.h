/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_asrc_types.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_ESP_GMF_ASRC_CONFIG()  {    \
    .src_info = {                           \
        .sample_rate     = 44100,           \
        .channel         = 2,               \
        .bits_per_sample = 16,              \
    },                                      \
    .dest_info = {                          \
        .sample_rate     = 48000,           \
        .channel         = 2,               \
        .bits_per_sample = 16,              \
    },                                      \
    .weight     = NULL,                     \
    .weight_len = 0,                        \
    .perf_type  = ESP_ASRC_PERF_TYPE_AUTO,  \
    .complexity = 2,                        \
    .timeout_ms = -1,                       \
}

/**
 * @brief  Initializes the GMF ASRC element with the provided configuration
 *
 * @param[in]   config  Pointer to the ASRC configuration
 * @param[out]  handle  Pointer to the ASRC element handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_asrc_init(esp_asrc_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set destination sample rate in the ASRC element
 *
 * @param[in]  handle     The ASRC element handle
 * @param[in]  dest_rate  The destination sample rate
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_asrc_set_dest_rate(esp_gmf_element_handle_t handle, uint32_t dest_rate);

/**
 * @brief  Set destination channel count in the ASRC element
 *
 * @param[in]  handle   The ASRC element handle
 * @param[in]  dest_ch  The destination channel count
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_asrc_set_dest_ch(esp_gmf_element_handle_t handle, uint8_t dest_ch);

/**
 * @brief  Set destination bit depth in the ASRC element
 *
 * @param[in]  handle     The ASRC element handle
 * @param[in]  dest_bits  The destination bit depth
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_asrc_set_dest_bits(esp_gmf_element_handle_t handle, uint8_t dest_bits);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
