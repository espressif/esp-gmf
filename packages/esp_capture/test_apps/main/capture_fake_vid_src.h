/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_capture_video_src_if.h"

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

#ifdef __cplusplus
}
#endif  /* __cplusplus */
