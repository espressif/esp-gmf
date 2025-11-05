/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_ae_fade.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define DEFAULT_ESP_GMF_FADE_CONFIG() {           \
    .mode            = ESP_AE_FADE_MODE_FADE_IN,  \
    .curve           = ESP_AE_FADE_CURVE_LINE,    \
    .transit_time    = 500,                       \
    .sample_rate     = 48000,                     \
    .channel         = 2,                         \
    .bits_per_sample = 16,                        \
}

/**
 * @brief  Initializes the GMF fade with the provided configuration
 *
 * @param[in]   config  Pointer to the fade configuration
 * @param[out]  handle  Pointer to the fade handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_fade_init(esp_ae_fade_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set the fade process mode
 *
 * @param[in]  handle  The fade handle
 * @param[in]  mode    The mode of fade
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_fade_set_mode(esp_gmf_element_handle_t handle, esp_ae_fade_mode_t mode);

/**
 * @brief  Get the fade process mode
 *
 * @param[in]   handle  The fade handle
 * @param[out]  mode    The mode of fade
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_fade_get_mode(esp_gmf_element_handle_t handle, esp_ae_fade_mode_t *mode);

/**
 * @brief  Reset the internal processing state of the fade element while preserving configuration
 *
 * @param[in]  handle  The fade element handle
 *
 * @return
 *       - ESP_GMF_ERR_OK            Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG   Invalid input parameter
 *       - ESP_GMF_ERR_INVALID_STATE Fade element is not opened
 */
esp_gmf_err_t esp_gmf_fade_reset(esp_gmf_element_handle_t handle);

/**
 * @brief  Reset the fade process to the initial configuration state.
 *         This API is kept for backward compatibility. Internally it calls @ref esp_gmf_fade_reset.
 *
 * @param[in]  handle  The fade handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_fade_reset_weight(esp_gmf_element_handle_t handle);

#ifdef __cplusplus
}
#endif /* __cplusplus */
