/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_audio_simple_dec.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG() {         \
    .dec_type      = ESP_AUDIO_SIMPLE_DEC_TYPE_NONE, \
    .dec_cfg       = NULL,                           \
    .cfg_size      = 0,                              \
    .use_frame_dec = false,                          \
}

/**
 * @brief  Initializes the GMF audio decoder with the provided configuration
 *
 * @param[in]   config  Pointer to the audio decoder configuration
 * @param[out]  handle  Pointer to the audio decoder handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_audio_dec_init(esp_audio_simple_dec_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Reconfigure the GMF audio decoder and filled into provided configuration
 *
 * @note  Only allowed when not running, i.e., in the `ESP_GMF_EVENT_STATE_NONE` or `ESP_GMF_EVENT_STATE_INITIALIZED` state
 *
 * @param[in]  handle  Audio decoder handle to be reconfigured
 * @param[in]  config  Pointer to the new simple decoder configuration
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid handle or configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_audio_dec_reconfig(esp_gmf_element_handle_t handle, esp_audio_simple_dec_cfg_t *config);

/**
 * @brief  Reconfigures the GMF audio decoder with default configuration based on provided sound information
 *
 * @note  1. Only allowed when not running, i.e., in the `ESP_GMF_EVENT_STATE_NONE` or `ESP_GMF_EVENT_STATE_INITIALIZED` state
 *        2. The actual reconfiguration behavior depends on the relationship between the provided `sound info` and
 *           the current decoder configuration (`cfg`):
 *             - If the `format_id` in `sound info` matches the decoder's current `type`, and the decoder's sub-config (`sub cfg`) is not NULL,
 *               only the basic fields in the sub-config will be updated with information from `sound info` (e.g., sample rate, channel count).
 *             - If the `format_id` differs from the current `type`, or if the sub-config is NULL,
 *               the decoder will be reconfigured using the **default configuration** for the specified `format_id`,
 *               and the current decoder `type` will be updated accordingly.
 *
 * @param[in]  handle   Audio decoder handle to be reconfigured
 * @param[in]  info     Sound information to be configured
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid handle or decoder type provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_audio_dec_reconfig_by_sound_info(esp_gmf_element_handle_t handle, esp_gmf_info_sound_t *info);

#ifdef __cplusplus
}
#endif /* __cplusplus */
