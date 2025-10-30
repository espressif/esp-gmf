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
 * @brief  Video encoder configuration
 */
typedef struct {
    uint32_t  codec_cc;  /*!< FourCC code to force a specific encoder implementation
                              See `esp_video_codec_desc_t` in `esp_video_codec_types.h` for details
                              If zero, the system will select a suitable encoder (HW preferred) */
} esp_gmf_video_enc_cfg_t;

/**
 * @brief  Initializes the GMF video encoder with the provided configuration
 *
 * @param[in]   cfg     Pointer to the video encoder configuration
 * @param[out]  handle  Pointer to the video encoder handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_video_enc_init(esp_gmf_video_enc_cfg_t *cfg, esp_gmf_element_handle_t *handle);

/**
 * @brief  Setting for video encoder destination codec (e.g., H.264, MJPEG)
 *
 * @note  This API should only called before element running
 *
 * @param[in]  handle     Video encoder element handle
 * @param[in]  dst_codec  Video encode destination codec (use GMF FourCC presentation)
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 */
esp_gmf_err_t esp_gmf_video_enc_set_dst_codec(esp_gmf_element_handle_t handle, uint32_t dst_codec);

/**
 * @brief  Do presetting for video encoder
 *
 * @note  This function pre-configures the video encoder with both source and destination information
 *        This allows querying encoder capabilities before runtime
 *        Normally, source information is reported from dependent element in the running state
 *        Call this function only before element running
 *
 * @param[in]  handle     Video encoder element handle
 * @param[in]  src_info   Video encode input video information
 * @param[in]  dst_codec  Destination video codec (use GMF FourCC presentation)
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 */
esp_gmf_err_t esp_gmf_video_enc_preset(esp_gmf_element_handle_t handle, esp_gmf_info_video_t *src_info,
                                       uint32_t dst_codec);

/**
 * @brief  Get video encoder output frame size
 *
 * @note  This API should only be called after `esp_gmf_video_enc_preset` set or after element running
 *
 * @param[in]   handle      Video encoder element handle
 * @param[out]  frame_size  Output frame size to store
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 */
esp_gmf_err_t esp_gmf_video_enc_get_out_size(esp_gmf_element_handle_t handle, uint32_t *frame_size);

/**
 * @brief  Get supported source formats (e.g., YUV420P, RGB565LE etc) for a given video encoder type
 *         This function retrieves the list of supported input formats that can be encoded
 *         into the specified destination codec
 *
 * @param[in]   handle        Video encoder element handle
 * @param[in]   dst_codec     Video encode destination codec (GMF FourCC representation of video codec)
 * @param[out]  src_fmts      Source formats to stored (GMF FourCC representation of video format)
 * @param[out]  src_fmts_num  Source format number to store
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 */
esp_gmf_err_t esp_gmf_video_enc_get_src_formats(esp_gmf_element_handle_t handle,
                                                uint32_t                 dst_codec,
                                                const uint32_t         **src_fmts,
                                                uint8_t                 *src_fmts_num);

/**
 * @brief  Setting for video encoder bitrate
 *
 * @note  Support setting both before and after element running
 *
 * @param[in]  handle   Video encoder element handle
 * @param[in]  bitrate  Output bitrate
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 */
esp_gmf_err_t esp_gmf_video_enc_set_bitrate(esp_gmf_element_handle_t handle, uint32_t bitrate);

/**
 * @brief  Set the GOP (Group of Pictures) for video encoder.
 *
 * @note  This setting applies specifically to the H.264 encoder
 *        The H.264 encoder generates an I-frame at the start of each GOP
 *        Can be configured both before and after the encoder element is running
 *
 * @param[in]  handle  Handle to the video encoder element
 * @param[in]  gop     GOP size for H.264 (in number of frames)
 *
 * @return
 *      - ESP_GMF_ERR_OK           On success
 *      - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 */
esp_gmf_err_t esp_gmf_video_enc_set_gop(esp_gmf_element_handle_t handle, uint32_t gop);

/**
 * @brief  Set the QP (Quantization Parameter) range for video encoder
 *
 * @note  This setting applies specifically to the H.264 encoder
 *        Higher QP values generally result in lower image quality
 *        Can be configured both before and after the encoder element is running
 *
 * @param[in]  handle  Handle to the video encoder element
 * @param[in]  min_qp  Minimum QP value
 * @param[in]  max_qp  Maximum QP value
 *
 * @return
 *      - ESP_GMF_ERR_OK           On success
 *      - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 */
esp_gmf_err_t esp_gmf_video_enc_set_qp(esp_gmf_element_handle_t handle, uint32_t min_qp, uint32_t max_qp);

#ifdef __cplusplus
}
#endif /* __cplusplus */
