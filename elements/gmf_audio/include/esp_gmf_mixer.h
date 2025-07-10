/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_ae_mixer.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief  Default mixer source info
 *         If `src_info` is NULL in `esp_ae_mixer_cfg_t`, the mixer will use esp_gmf_default_mixer_src_info as default
 *         And `src_num` will set to `sizeof(esp_gmf_default_mixer_src_info) / sizeof(esp_ae_mixer_info_t)`;
 */
#define DEFAULT_ESP_GMF_MIXER_CONFIG() {  \
    .sample_rate     = 48000,             \
    .channel         = 2,                 \
    .bits_per_sample = 16,                \
    .src_num         = 0,                 \
    .src_info        = NULL,              \
}

/**
 * @brief  Initializes the GMF mixer with the provided configuration
 *
 * @param[in]   config  Pointer to the mixer configuration
 * @param[out]  handle  Pointer to the mixer handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_mixer_init(esp_ae_mixer_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set the transit mode of a certain stream according to src_idx
 *
 * @param[in]  handle   The mixer handle
 * @param[in]  src_idx  The index of a certain source stream which want to set transit mode.
 *                      eg: 0 refer to first source stream
 * @param[in]  mode     The transit mode of source stream
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_mixer_set_mode(esp_gmf_element_handle_t handle, uint8_t src_idx, esp_ae_mixer_mode_t mode);

/**
 * @brief  Set audio information to the mixer handle
 *
 * @param[in]  handle       The mixer handle
 * @param[in]  sample_rate  The audio sample rate
 * @param[in]  bits         The audio bits per sample
 * @param[in]  channel      The audio channel
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 *       - ESP_GMF_ERR_FAIL         Failed to set configuration
 */
esp_gmf_err_t esp_gmf_mixer_set_audio_info(esp_gmf_element_handle_t handle, uint32_t sample_rate,
                                           uint8_t bits, uint8_t channel);

#ifdef __cplusplus
}
#endif /* __cplusplus */
