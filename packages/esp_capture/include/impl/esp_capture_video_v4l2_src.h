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
 * @brief  Create an instance for V4L2 video source
 *
 * @param[in]  cfg  V4L2 video source configuration
 *
 * @return
 *       - NULL    No enough memory to hold V4L2 instance
 *       - Others  V4L2 video source instance
 */
esp_capture_video_src_if_t *esp_capture_new_video_v4l2_src(esp_capture_video_v4l2_src_cfg_t *cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
