/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_element.h"
#include "esp_muxer.h"
#include "esp_gmf_info.h"
#include "esp_gmf_audio_helper.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Muxer output type
 */
typedef enum {
    ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING = 0,  /*!< Streaming output mode, data is output through databus */
    ESP_GMF_AUDIO_MUXER_OUTPUT_FILE      = 1,  /*!< File output mode, data is written to file using file writer */
} esp_gmf_audio_muxer_output_type_t;

/**
 * @brief  Callback function type for getting codec specific information from audio encoder
 *
 * @param[in]   ctx        Context for the get_codec_spec_info_cb callback
 * @param[out]  spec_info  Pointer to store the specific information
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid context or output pointers
 *       - ESP_GMF_ERR_FAIL         No codec specific information available or encoder not opened
 */
typedef esp_gmf_err_t (*audio_muxer_codec_spec_info_query)(void *ctx, esp_gmf_audio_helper_spec_info_t *spec_info);

/**
 * @brief  Muxer configuration for GMF audio element
 */
typedef struct {
    esp_muxer_type_t                   muxer_type;               /*!< Muxer type */
    esp_muxer_audio_codec_t            codec;                    /*!< Audio codec type */
    esp_gmf_audio_muxer_output_type_t  output_type;              /*!< Output type: streaming, file, or both */
    uint32_t                           slice_duration;           /*!< Muxer file segment duration, unit millisecond. Should set when output_type is ESP_GMF_AUDIO_MUXER_OUTPUT_FILE */
    muxer_url_pattern_ex               url_pattern;              /*!< Muxer file path pattern callback for each segment. Should set when output_type is ESP_GMF_AUDIO_MUXER_OUTPUT_FILE */
    void                              *url_ctx;                  /*!< Muxer file path pattern callback context. Should set when output_type is ESP_GMF_AUDIO_MUXER_OUTPUT_FILE */
    audio_muxer_codec_spec_info_query  get_codec_spec_info_cb;   /*!< Callback function for getting codec specific information from audio encoder. Should set when audio codec have specific information to configure */
    void                              *get_codec_spec_info_ctx;  /*!< Context for the get_codec_spec_info_cb callback. Should set when audio codec have specific information to configure */
} esp_gmf_audio_muxer_cfg_t;

/**
 * @brief  Initializes the GMF muxer element with the provided configuration
 *
 * @param[in]   config  Pointer to the muxer configuration
 * @param[out]  handle  Pointer to the muxer element handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_audio_muxer_init(esp_gmf_audio_muxer_cfg_t *config, esp_gmf_element_handle_t *handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
