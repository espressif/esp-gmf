/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_video_render.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define VIDEO_RENDER_LVGL_SUPPORT

#define BREAK_ON_FAIL(sta)  {                                             \
    int _ret = sta;                                                       \
    if (_ret) {                                                           \
        ESP_LOGE(TAG, "Fail at %s:%d ret %d", __func__, __LINE__, _ret);  \
        break;                                                            \
    }                                                                     \
}

/**
 * @brief  Enable or disable LVGL backend usage
 *
 * @param[in]  use  True to use LVGL backend, false to use default backend
 *
 * @return
 *       - None
 */
void video_render_use_lvgl(bool use);

/**
 * @brief  Create video render instance
 *
 * @param[in]  fps  Frame rate (frames per second)
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int create_video_render(uint8_t fps);

/**
 * @brief  Get video render handle
 *
 * @return
 *       - Video  render handle on success
 *       - NULL   on failure
 */
esp_video_render_handle_t get_video_render(void);

/**
 * @brief  Destroy video render instance
 *
 * @return
 *       - None
 */
void destroy_video_render(void);

/**
 * @brief  Play a single video file
 *
 * @param[in]  mjpeg_path   Path to MJPEG video file
 * @param[in]  sync_render  True for synchronous rendering, false for asynchronous
 * @param[in]  fps          Frame rate (frames per second)
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_render_play_one_video(char *mjpeg_path, bool sync_render, int fps);

/**
 * @brief  Play a single video file with progress bar
 *
 * @param[in]  mjpeg_path  Path to MJPEG video file
 * @param[in]  fps         Frame rate (frames per second)
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_render_play_one_video_with_progress(char *mjpeg_path, int fps);

/**
 * @brief  Play two videos side-by-side
 *
 * @param[in]  mjpeg_path1  Path to first MJPEG video file (left side)
 * @param[in]  mjpeg_path2  Path to second MJPEG video file (right side)
 * @param[in]  fps          Frame rate (frames per second)
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_render_play_dual_video(char *mjpeg_path1, char *mjpeg_path2, int fps);

/**
 * @brief  Play two videos side-by-side with progress bars
 *
 * @param[in]  mjpeg_path1  Path to first MJPEG video file (left side)
 * @param[in]  mjpeg_path2  Path to second MJPEG video file (right side)
 * @param[in]  fps          Frame rate (frames per second)
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_render_play_dual_video_with_progress(char *mjpeg_path1, char *mjpeg_path2, int fps);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
