/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Set frame destination sample rate for audio element
 *
 * @param[in]  handle     Audio element handle
 * @param[in]  dest_rate  Destination sample rate
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_audio_param_set_dest_rate(esp_gmf_element_handle_t self, uint32_t dest_rate);

/**
 * @brief  Set frame destination bits for audio element
 *
 * @param[in]  handle     Audio element handle
 * @param[in]  dest_bits  Destination bits
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_audio_param_set_dest_bits(esp_gmf_element_handle_t self, uint8_t dest_bits);

/**
 * @brief  Set frame destination channel for audio element
 *
 * @param[in]  handle   Audio element handle
 * @param[in]  dest_ch  Destination channel
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_audio_param_set_dest_ch(esp_gmf_element_handle_t self, uint8_t dest_ch);

/**
 * @brief  Set speed for audio element
 *
 * @param[in]  handle  Audio element handle
 * @param[in]  speed   Audio playback speed
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_audio_param_set_speed(esp_gmf_element_handle_t self, float speed);

/**
 * @brief  Set pitch for audio element
 *
 * @param[in]  handle  Audio element handle
 * @param[in]  pitch   Audio pitch to set
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_audio_param_set_pitch(esp_gmf_element_handle_t self, float pitch);

/**
 * @brief  Set ALC certain channel gain for audio element
 *
 * @param[in]  handle   Audio element handle
 * @param[in]  ch_idx   Channel index (0: Left channel 1: Right Channel 0xFF: All channels)
 * @param[in]  gain_db  ALC gain (unit decibel)
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_audio_param_set_alc_channel_gain(esp_gmf_element_handle_t self, uint8_t ch_idx, float gain_db);

/**
 * @brief  Set ALC gain for all channels
 */
#define esp_gmf_audio_param_set_alc_gain(self, gain_db) esp_gmf_audio_param_set_alc_channel_gain(self, 0xFF, gain_db)

/**
 * @brief  Set fade in/out direction for audio element
 *
 * @param[in]  handle      Audio element handle
 * @param[in]  is_fade_in  Whether fade in or out
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_audio_param_set_fade(esp_gmf_element_handle_t self, bool is_fade_in);

/**
 * @brief  Helper wrapper for fade in and fade out setting
 */
#define esp_gmf_audio_param_set_fade_in(self)  esp_gmf_audio_param_set_fade(self, true);
#define esp_gmf_audio_param_set_fade_out(self) esp_gmf_audio_param_set_fade(self, false);

#ifdef __cplusplus
}
#endif
