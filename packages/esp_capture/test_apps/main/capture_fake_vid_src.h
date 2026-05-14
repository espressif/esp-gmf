/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_capture_video_src_if.h"
#include "esp_capture_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

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
