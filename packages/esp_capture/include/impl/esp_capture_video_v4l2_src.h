/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_types.h"
#include "esp_capture_video_src_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Video4Linux2 (V4L2) video source implementation for ESP Capture
 *
 * @note  This file implements the video source interface for capturing video data from
 *        camera devices using the Linux V4L2 (Video4Linux2) architecture. Currently
 *        only supported on ESP32-P4 platform
 *
 *        Key Features:
 *          - Provides video source interface implementation for V4L2 camera devices
 *          - Supports multiple video formats: YUV420, YUV422P, MJPEG, RGB565
 *          - Handles video format negotiation and capability setting
 *          - Manages V4L2 buffer allocation and memory mapping
 *          - Implements frame acquisition and release with proper cache management
 */

/**
 * @brief  V4L2 video source configuration
 */
typedef struct {
    char     dev_name[16];  /*!< V4L2 device name (e.g. "/dev/video0") */
    uint8_t  buf_count;     /*!< Number of buffers to hold captured video frames (recommended: 4-8) */
} esp_capture_video_v4l2_src_cfg_t;

/**
 * @brief  Discrete frame size reported by V4L2 source
 */
typedef struct {
    uint16_t  width;   /*!< Frame width */
    uint16_t  height;  /*!< Frame height */
} esp_capture_v4l2_frame_size_t;

/**
 * @brief  Create an instance for V4L2 video source
 *
 * @param[in]  cfg  V4L2 video source configuration
 *
 * @return
 *       - NULL    No enough memory to hold V4L2 instance
 *       - Others  V4L2 video source instance
 */
esp_capture_video_src_if_t *esp_capture_new_video_v4l2_src(esp_capture_video_v4l2_src_cfg_t *cfg);

/**
 * @brief  Get supported capture formats discovered by the V4L2 source
 *
 * @note  Opens the device transiently when needed. On CSI/DVP/SPI the result is the
 *        union of formats available across sensor modes (native + ISP/convert).
 *        On UVC it mirrors standard VIDIOC_ENUM_FMT.
 *        When `formats` is NULL, only fills `*count` with the required number.
 *
 * @param[in]      src      V4L2 video source instance
 * @param[out]     formats  Output format array (optional)
 * @param[in,out]  count    In: capacity of `formats`; out: filled or required count
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid argument
 *       - ESP_CAPTURE_ERR_NOT_ENOUGH     Buffer too small (required count stored in `*count`)
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Not a V4L2 source / device open failed
 */
esp_capture_err_t esp_capture_v4l2_get_formats(esp_capture_video_src_if_t *src,
                                               esp_capture_format_id_t *formats,
                                               uint8_t *count);

/**
 * @brief  Get supported frame sizes for a capture format
 *
 * @note  Sizes are queried from V4L2/sensor modes on demand and are not retained by
 *        the source. Only the sensor mode selected by negotiation is retained.
 *        When `sizes` is NULL, only fills `*count` with the required number.
 *
 * @param[in]      src        V4L2 video source instance
 * @param[in]      format_id  Capture format id
 * @param[out]     sizes      Output size array (optional)
 * @param[in,out]  count      In: capacity of `sizes`; out: filled or required count
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid argument
 *       - ESP_CAPTURE_ERR_NOT_ENOUGH     Buffer too small (required count stored in `*count`)
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Format not supported
 */
esp_capture_err_t esp_capture_v4l2_get_frame_sizes(esp_capture_video_src_if_t *src,
                                                   esp_capture_format_id_t format_id,
                                                   esp_capture_v4l2_frame_size_t *sizes,
                                                   uint8_t *count);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
