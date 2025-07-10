/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_gmf_element.h"
#include "esp_ae_alc.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define DEFAULT_ESP_GMF_ALC_CONFIG() {  \
    .sample_rate     = 48000,           \
    .channel         = 2,               \
    .bits_per_sample = 16,              \
}

/**
 * @brief  Initializes the GMF ALC with the provided configuration
 *
 * @param[in]   config  Pointer to the ALC configuration
 * @param[out]  handle  Pointer to the ALC handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_alc_init(esp_ae_alc_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set specific channel gain of the ALC handle.
 *         Positive gain indicates an increase in volume,
 *         negative gain indicates a decrease in volume.
 *         0 gain indicates the volume level remains unchanged.
 *
 * @param[in]  handle  The ALC handle
 * @param[in]  idx     The channel index of the gain to retrieve. eg: 0 refers to the first channel
 * @param[in]  gain    The gain value needs to conform to the following conditions:
 *                     - Supported range [-64, 63]
 *                     - Below -64 will set to mute
 *                     - Higher than 63 not supported
 *                     Unit: dB
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_alc_set_gain(esp_gmf_element_handle_t handle, uint8_t idx, int8_t gain);

/**
 * @brief  Get the gain for a specific channel from the ALC handle
 *
 * @param[in]   handle  The ALC handle
 * @param[in]   ch_idx  The channel index of the gain to retrieve. eg: 0 refers to the first channel
 * @param[out]  gain    Pointer to store the retrieved gain. Unit: dB
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_alc_get_gain(esp_gmf_element_handle_t handle, uint8_t idx, int8_t *gain);

#ifdef __cplusplus
}
#endif /* __cplusplus */
