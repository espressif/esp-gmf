/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "esp_capture_video_src_if.h"
#include "esp_capture_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Fake video source framebuffer fill pattern type
 */
typedef enum {
    ESP_CAPTURE_FAKE_VID_PATTERN_NONE        = 0,  /*!< No pattern, fill with 0xFF */
    ESP_CAPTURE_FAKE_VID_PATTERN_PLAIN       = 1,  /*!< Plain pattern, fill with 0xFF */
    ESP_CAPTURE_FAKE_VID_PATTERN_RGB565_QUAD = 2,  /*!< RGB565 quad pattern, fill with caller-provided RGB565 colors */
} esp_capture_fake_vid_pattern_type_t;

/**
 * @brief  RGB565 colors for the four frame quadrants (top-left, top-right, bottom-left, bottom-right)
 */
typedef struct {
    uint16_t  tl;  /*!< Top-left color */
    uint16_t  tr;  /*!< Top-right color */
    uint16_t  bl;  /*!< Bottom-left color */
    uint16_t  br;  /*!< Bottom-right color */
} esp_capture_fake_vid_rgb565_quad_t;

/**
 * @brief  Fake video source framebuffer fill pattern
 */
typedef struct {
    esp_capture_fake_vid_pattern_type_t  type;
    esp_capture_fake_vid_rgb565_quad_t   rgb565_quad;
} esp_capture_fake_vid_pattern_t;

/**
 * @brief  This API provide a fake implementation for capture video source
 *         Specially used for unit test cases
 *
 * @return
 *       - NULL    Not enough memory for video fake source
 *       - Others  Fake video source interface
 */
esp_capture_video_src_if_t *esp_capture_new_video_fake_src(uint8_t frame_count);

/**
 * @brief  Configure framebuffer fill pattern (before start)
 *
 * @param[in]  src      Video source interface
 * @param[in]  pattern  Pattern configuration; for `ESP_CAPTURE_FAKE_VID_PATTERN_RGB565_QUAD`
 *                      fill `rgb565_quad` with caller-provided RGB565 colors
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK             On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Invalid argument
 *       - ESP_CAPTURE_ERR_INVALID_STATE  Source already started
 */
esp_capture_err_t esp_capture_fake_vid_src_set_pattern(esp_capture_video_src_if_t *src,
                                                       const esp_capture_fake_vid_pattern_t *pattern);

/**
 * @brief  Supply a fixed bitstream for the fake video source
 *
 * @param[in]  src   The video source interface
 * @param[in]  info  The video info
 * @param[in]  data  The compressed bitstream
 * @param[in]  size  The size of the compressed bitstream
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK           On success
 *       - ESP_CAPTURE_ERR_INVALID_ARG  If the input arguments are invalid
 */
esp_capture_err_t esp_capture_fake_vid_src_set_fixed_image(esp_capture_video_src_if_t *src,
                                                           esp_capture_video_info_t *info, const uint8_t *data,
                                                           uint32_t size);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
