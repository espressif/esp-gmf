/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Capture video source interface (typedef alias)
 */
typedef struct esp_capture_video_src_if_t esp_capture_video_src_if_t;

/**
 * @brief  Capture video source interface
 */
struct esp_capture_video_src_if_t {
    /**
     * @brief  Open the video source for capturing
     *
     * @param[in]  src  Pointer to the video source interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to open video source
     */
    esp_capture_err_t (*open)(esp_capture_video_src_if_t *src);

    /**
     * @brief  Get the supported video codecs
     *
     * @param[in]   src     Pointer to the video source interface
     * @param[out]  codecs  Pointer to an array of supported codecs
     * @param[out]  num     Number of supported codecs
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to get supported codecs
     */
    esp_capture_err_t (*get_support_codecs)(esp_capture_video_src_if_t *src, const esp_capture_format_id_t **codecs, uint8_t *num);

    /**
     * @brief  Set fixed capability for video source
     *
     * @note  If fixed capability is set, `negotiate_caps` returns it directly if the format matches
     *
     * @param[in]   src       Pointer to the video source interface
     * @param[in]   aud_caps  Pointer to fixed video capability to set
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to set fixed capability
     */
    esp_capture_err_t (*set_fixed_caps)(esp_capture_video_src_if_t *src, const esp_capture_video_info_t *fixed_caps);

    /**
     * @brief  Negotiate capabilities between the source and the sink
     *
     * @param[in]   src       Pointer to the video source interface
     * @param[in]   in_caps   Pointer to input capabilities
     * @param[out]  out_caps  Pointer to output capabilities
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to negotiate capabilities
     */
    esp_capture_err_t (*negotiate_caps)(esp_capture_video_src_if_t *src, esp_capture_video_info_t *in_caps, esp_capture_video_info_t *out_caps);

    /**
     * @brief  Start capturing video from the source
     *
     * @param[in]  src  Pointer to the video source interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to start video source
     */
    esp_capture_err_t (*start)(esp_capture_video_src_if_t *src);

    /**
     * @brief  Acquire video frame from the source
     *
     * @note  The acquired frame should be released using release_frame when no longer needed
     *        The frame buffer is managed by the source and should not be freed by the caller
     *        Multiple frames can be acquired before releasing them
     *
     * @param[in]   src    Pointer to the video source interface
     * @param[out]  frame  Pointer to store the frame information
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to acquire video frame
     */
    esp_capture_err_t (*acquire_frame)(esp_capture_video_src_if_t *src, esp_capture_stream_frame_t *frame);

    /**
     * @brief  Release video frame from the source
     *
     * @note  This function should be called for each frame acquired using acquire_frame
     *        The frame must be released before acquiring a new one
     *
     * @param[in]  src    Pointer to the video source interface
     * @param[in]  frame  Pointer to the frame to be released
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to release video frame
     */
    esp_capture_err_t (*release_frame)(esp_capture_video_src_if_t *src, esp_capture_stream_frame_t *frame);

    /**
     * @brief  Stop capturing video from the source
     *
     * @param[in]  src  Pointer to the video source interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to stop video source
     */
    esp_capture_err_t (*stop)(esp_capture_video_src_if_t *src);

    /**
     * @brief  Close the video source and release resources
     *
     * @param[in]  src  Pointer to the video source interface
     *
     * @return
     *       - ESP_CAPTURE_ERR_OK  On success
     *       - Others              Failed to close video source
     */
    esp_capture_err_t (*close)(esp_capture_video_src_if_t *src);
};

#ifdef __cplusplus
}
#endif  /* __cplusplus */
