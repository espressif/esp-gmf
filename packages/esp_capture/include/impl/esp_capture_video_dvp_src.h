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
 * @brief  DVP (Digital Video Port) video source implementation for ESP Capture
 *
 * @note  This file implements the video source interface for capturing video data from
 *        DVP (Digital Video Port) camera devices using the ESP32-Camera component
 *        Currently supported on ESP32-S2 and ESP32-S3 platforms
 *
 *        Key Features:
 *          - Provides video source interface implementation for DVP camera devices
 *          - Supports multiple video formats: MJPEG, YUV422P, YUV420, RGB565
 *          - Handles video format negotiation and capability setting
 *          - Manages camera initialization and configuration via ESP32-Camera component
 *          - Implements automatic YUV422 to YUV420 format conversion when needed
 *          - Supports multiple resolution presets (QVGA, VGA, HD, FHD, etc.)
 */

/**
 * @brief  DVP video source configuration
 */
typedef struct {
    uint8_t   buf_count;  /*!< Buffer count to hold captured video frames */
    int16_t   pwr_pin;    /*!< Power pin */
    int16_t   reset_pin;  /*!< Reset pin */
    int16_t   xclk_pin;   /*!< XCLK pin */
    int16_t   data[8];    /*!< Data pins */
    int16_t   vsync_pin;  /*!< VSYNC pin */
    int16_t   href_pin;   /*!< HREF pin */
    int16_t   pclk_pin;   /*!< PCLK pin */
    uint32_t  xclk_freq;  /*!< XCLK frequency in Hz (typically 24MHz) */
    uint8_t   i2c_port;   /*!< I2C port, i2c driver must installed before call `esp_capture_start` */
} esp_capture_video_dvp_src_cfg_t;

/**
 * @brief  Create an instance for DVP video source
 *
 * @param[in]  cfg  DVP video source configuration
 *
 * @return
 *       - NULL   Not enough memory to hold DVP instance
 *       - Other  DVP video source instance
 */
esp_capture_video_src_if_t *esp_capture_new_video_dvp_src(esp_capture_video_dvp_src_cfg_t *cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
