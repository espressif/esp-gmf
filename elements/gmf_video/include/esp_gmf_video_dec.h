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
 * @brief  Video decoder configuration (optional)
 */
typedef struct {
    uint32_t  codec_cc;  /*!< FourCC code to force a specific decoder implementation
                              See `esp_video_codec_desc_t` in `esp_video_codec_types.h` for details
                              If zero, the system will select a suitable decoder (HW preferred) */
} esp_gmf_video_dec_cfg_t;

/**
 * @brief  Initializes the GMF Video decoder with the provided configuration
 *
 * @param[in]   cfg     Pointer to the Video decoder configuration (optional)
 * @param[out]  handle  Pointer to the Video decoder handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_video_dec_init(esp_gmf_video_dec_cfg_t *cfg, esp_gmf_element_handle_t *handle);

/**
 * @brief  Get Video decoder output frame size
 *
 * @note  This API should only called after element running or `esp_gmf_video_dec_set_dst_format` is called
 *
 * @param[in]   handle      Video decoder element handle
 * @param[out]  frame_size  Output frame size to store
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 */
esp_gmf_err_t esp_gmf_video_dec_get_out_size(esp_gmf_element_handle_t handle, uint32_t *frame_size);

/**
 * @brief  Setting for video decoder destination format (e.g., YUV420P, RGB565LE)
 *
 * @note  This API should only called before element running
 *
 * @param[in]  handle   Video decoder element handle
 * @param[in]  dst_fmt  Destination format (GMF FourCC representation of video codec)
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 */
esp_gmf_err_t esp_gmf_video_dec_set_dst_format(esp_gmf_element_handle_t handle, uint32_t dst_fmt);

/**
 * @brief  Get currently configured decoder output pixel format
 *
 * @param[in]   handle   Video decoder element handle
 * @param[out]  dst_fmt  Destination format (GMF FourCC)
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_video_dec_get_dst_format(esp_gmf_element_handle_t handle, uint32_t *dst_fmt);

/**
 * @brief  Get supported output formats for a given video decoder type
 *         This function retrieves the list of output formats (e.g., YUV420P, RGB565LE) that the decoder can produce
 *         when decoding from the specified source codec
 *
 * @param[in]   handle        Video decoder element handle
 * @param[in]   src_codec     Decode type (GMF FourCC representation of video codec)
 * @param[out]  dst_fmts      Output video formats to store (GMF FourCC representation of video format)
 * @param[out]  dst_fmts_num  Output video format number to store
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 */
esp_gmf_err_t esp_gmf_video_dec_get_dst_formats(esp_gmf_element_handle_t handle,
                                                uint32_t                 src_codec,
                                                const uint32_t         **dst_fmts,
                                                uint8_t                 *dst_fmts_num);

/**
 * @brief  Enable decode output frame pool for zero-copy sharing (e.g. with share_copier)
 *
 * @note  Call before element running. Creates the frame info queue immediately so acquire can block
 *        from `pipeline_run`. Pixel storage is allocated after header parse when frame size is known.
 *        `frame_count == 0` keeps the legacy path (decode into acquire_out buffer).
 *        `frame_count >= 1` enables pool mode; recommended value is 2 or 3.
 *
 * @param[in]  handle       Video decoder element handle
 * @param[in]  frame_count  Number of decoded frames kept in the pool
 *
 * @return
 *       - ESP_GMF_ERR_OK             Success
 *       - ESP_GMF_ERR_INVALID_ARG    Invalid argument
 *       - ESP_GMF_ERR_INVALID_STATE  Element already running / pool already in use
 *       - ESP_GMF_ERR_MEMORY_LACK    Failed to allocate pool port
 */
esp_gmf_err_t esp_gmf_video_dec_set_out_pool(esp_gmf_element_handle_t handle, int frame_count);

#ifdef __cplusplus
}
#endif /* __cplusplus */
