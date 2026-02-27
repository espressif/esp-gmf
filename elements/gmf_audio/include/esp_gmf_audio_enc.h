/*
 * SPDX-FileCopyrightText: 2025-2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_gmf_element.h"
#include "encoder/esp_audio_enc.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_ESP_GMF_AUDIO_ENC_CONFIG() {  \
    .type   = ESP_AUDIO_TYPE_UNSUPPORT,       \
    .cfg    = NULL,                           \
    .cfg_sz = 0,                              \
}

/**
 * @brief  Initializes the GMF audio encoder with the provided configuration
 *
 * @param[in]   config  Pointer to the audio encoder configuration
 * @param[out]  handle  Pointer to the audio encoder handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_audio_enc_init(esp_audio_enc_config_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Get the input and output frame size information of the GMF audio encoder
 *
 * @param[in]   handle    Audio encoder handle
 * @param[out]  in_size   Pointer to store input frame size
 * @param[out]  out_size  Pointer to store suggested output frame size
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid handle or size pointers provided
 */
esp_gmf_err_t esp_gmf_audio_enc_get_frame_size(esp_gmf_element_handle_t handle, uint32_t *in_size, uint32_t *out_size);

/**
 * @brief  Set the bitrate for the GMF audio encoder
 *
 * @note  When element is in `ESP_GMF_EVENT_STATE_NONE` or `ESP_GMF_EVENT_STATE_INITIALIZED` state,
 *        user needs to configure the codec first (e.g., via `esp_gmf_audio_enc_reconfig` or `esp_gmf_audio_enc_reconfig_by_sound_info`)
 *        before calling this function
 *
 * @param[in]  handle   Audio encoder handle
 * @param[in]  bitrate  The bitrate of the encoder
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid handle or bitrate value provided
 */
esp_gmf_err_t esp_gmf_audio_enc_set_bitrate(esp_gmf_element_handle_t handle, uint32_t bitrate);

/**
 * @brief  Get the current bitrate of the GMF audio encoder
 *
 * @param[in]   handle   Audio encoder handle
 * @param[out]  bitrate  Pointer to store the current bitrate
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid handle or bitrate pointer provided
 */
esp_gmf_err_t esp_gmf_audio_enc_get_bitrate(esp_gmf_element_handle_t handle, uint32_t *bitrate);

/**
 * @brief  Reconfigure the GMF audio encoder and filled into provided configuration
 *
 * @note  Only allowed when not running, i.e., in the `ESP_GMF_EVENT_STATE_NONE` or `ESP_GMF_EVENT_STATE_INITIALIZED` state
 *
 * @param[in]  handle  Audio encoder handle to be reconfigured
 * @param[in]  config  Pointer to the new audio encoder configuration
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid handle or configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_audio_enc_reconfig(esp_gmf_element_handle_t handle, esp_audio_enc_config_t *config);

/**
 * @brief  Reconfigures the GMF audio encoder with default configuration for the sound information
 *
 * @note  1. Only allowed when not running, i.e., in the `ESP_GMF_EVENT_STATE_NONE` or `ESP_GMF_EVENT_STATE_INITIALIZED` state
 *        2. The actual reconfiguration behavior depends on the relationship between the provided `sound info` and
 *           the current encoder configuration (`cfg`):
 *           - If the `format_id` in `sound info` matches the encoder's current `type`, and the encoder's sub-config (`sub cfg`) is not NULL,
 *             only the basic fields in the sub-config will be updated with information from `sound info` (e.g., sample rate, channel count).
 *           - If the `format_id` differs from the current `type`, or if the sub-config is NULL,
 *             the encoder will be reconfigured using the **default configuration** for the specified `format_id`,
 *             and the current encoder `type` will be updated accordingly.
 *
 * @param[in]  handle  Audio encoder handle to be reconfigured
 * @param[in]  info    Sound information to be configured
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid handle or encoder type provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_audio_enc_reconfig_by_sound_info(esp_gmf_element_handle_t handle, esp_gmf_info_sound_t *info);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
