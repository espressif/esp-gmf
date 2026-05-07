/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include "esp_video_render.h"
#include "esp_video_render_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Progress bar configuration
 */
typedef struct {
    esp_video_render_stream_handle_t  stream;         // Stream handle (for getting overlay)
    esp_video_render_format_t         format;         // Format
    esp_video_render_clr_t            bg_color;       // Background color
    esp_video_render_clr_t            bar_color;      // Progress bar color
    int                               height;         // Bar height (default: 8)
    int                               padding;        // Padding around bar (default: 4)
    int                               bottom_margin;  // Margin from video bottom (default: 10)
} progress_bar_cfg_t;

/**
 * @brief  Progress bar handle
 */
typedef void *progress_bar_handle_t;

/**
 * @brief  Create a progress bar container overlay
 *
 * @param[in]  video_rect  Video rectangle in absolute screen coordinates
 * @param[in]  cfg         Progress bar configuration (must include stream handle)
 *
 * @return
 *       - NULL    On failure
 *       - Others  Progress bar handle
 */
progress_bar_handle_t progress_bar_create(const esp_video_render_rect_t *video_rect,
                                          const progress_bar_cfg_t *cfg);

/**
 * @brief  Update progress bar percentage
 *
 * @param[in]  handle   Progress bar handle
 * @param[in]  percent  Progress percentage (0-100)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t progress_bar_update(progress_bar_handle_t handle, uint8_t percent);

/**
 * @brief  Destroy progress bar
 *
 * @param[in]  handle  Progress bar handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t progress_bar_destroy(progress_bar_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
