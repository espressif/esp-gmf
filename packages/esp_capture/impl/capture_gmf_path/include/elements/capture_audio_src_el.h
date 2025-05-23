/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_types.h"
#include "esp_capture_audio_src_if.h"
#include "esp_gmf_element.h"
#include "esp_capture_sync.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Audio source element configuration structure
 */
typedef struct {
    esp_capture_audio_src_if_t  *asrc_if;  /*!< Audio source interface to use */
} capture_aud_src_el_cfg_t;

/**
 * @brief  Initialize audio capture source element
 *
 * @param[in]   cfg     Audio capture source element configuration (optional)
 *                      If set to NULL, user need call `capture_audio_src_el_set_src_if` before element run
 * @param[out]  handle  Pointer to store GMF element handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_MEMORY_LACK  Not enough memory
 */
esp_gmf_err_t capture_audio_src_el_init(capture_aud_src_el_cfg_t *cfg, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set capture synchronization handle for audio source
 *
 * @note  This function can only be called before the element starts running
 *        The audio source will use this handle to update time and perform synchronization actions
 *
 * @param[in]  handle       Audio capture source element handle
 * @param[in]  sync_handle  Capture synchronization handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t capture_audio_src_el_set_sync_handle(esp_gmf_element_handle_t handle, esp_capture_sync_handle_t sync_handle);

/**
 * @brief  Set audio source interface
 *
 * @note  This function can only be called before the element starts running
 *
 * @param[in]  handle   Audio capture source element handle
 * @param[in]  asrc_if  Audio source interface to set
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t capture_audio_src_el_set_src_if(esp_gmf_element_handle_t handle, esp_capture_audio_src_if_t *asrc_if);

/**
 * @brief  Set input frame audio sample number for audio source
 *
 * @note  This function can only be called before the element starts running
 *
 * @param[in]  handle         Audio capture source element handle
 * @param[in]  frame_samples  Number of audio samples per frame
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t capture_audio_src_el_set_in_frame_samples(esp_gmf_element_handle_t handle, int frame_samples);

/**
 * @brief  Negotiate audio source capabilities
 *
 * @note  This function can only be called before the element starts running
 *        The negotiation process first matches the format, then other parameters
 *        like sample rate and channel count. If the requested codec is not supported
 *        but PCM is supported, the function will report PCM and related information
 *
 * @param[in]   handle     Audio capture source element handle
 * @param[in]   nego_info  Audio information to negotiate
 * @param[out]  res_info   Pointer to store negotiated result
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - Others                   Failed to negotiate audio capabilities
 */
esp_gmf_err_t capture_audio_src_el_negotiate(esp_gmf_element_handle_t handle, esp_capture_audio_info_t *nego_info,
                                             esp_capture_audio_info_t *res_info);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
