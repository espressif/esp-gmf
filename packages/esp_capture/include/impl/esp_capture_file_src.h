/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_audio_src_if.h"
#include "esp_capture_video_src_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Audio file source configuration
 */
typedef struct {
    const char *url;               /*!< File URL */
    int        frame_duration_ms;  /*!< Frame duration in ms, used to calculate PTS for encoded frame
                                        (Default 20ms if not set) */
} esp_capture_audio_file_src_cfg_t;

/**
 * @brief  Video file source configuration
 */
typedef struct {
    const char *url; /*!< File URL */
} esp_capture_video_file_src_cfg_t;

/**
 * @brief  Create audio file source for codec
 *
 * @param[in]  cfg  Audio file source configuration
 *
 * @return
 *       - NULL    Not enough memory to new an audio file source instance
 *       - Others  Audio file source instance
 */
esp_capture_audio_src_if_t *esp_capture_new_audio_file_src(esp_capture_audio_file_src_cfg_t *cfg);

/**
 * @brief  Create video file source for codec
 *
 * @param[in]  cfg  Video file source configuration
 *
 * @return
 *       - NULL    Not enough memory to new a video file source instance
 *       - Others  Video file source instance
 */
esp_capture_video_src_if_t *esp_capture_new_video_file_src(esp_capture_video_file_src_cfg_t *cfg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
