/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "video_render_internal.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Execute video render blend
 *
 * @param[in]  video_render  Video render context
 * @param[in]  backend       Video render backend context
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_FAIL         Failed to execute video render blend
 */
esp_video_render_err_t video_render_blend_execute(video_render_t *video_render, video_render_backend_t *backend);

/**
 * @brief  Set dirty monitor callback
 *
 * @param[in]  video_render  Video render context
 * @param[in]  cb            Dirty monitor callback
 */
void video_render_set_dirty_monitor_cb(video_render_t *video_render, video_render_dirty_monitor_cb cb);

/**
 * @brief  Video render blend thread
 *
 * @param[in]  arg  Video render context
 */
void video_render_blend_thread(void *arg);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
