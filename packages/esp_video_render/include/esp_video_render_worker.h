/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_video_render_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Video render worker handle
 *
 * @note  Video render worker is a helper module which helper use to process frame data in async way
 *        It internally create a cached queue to store frame data, and use a worker task to process frame data
 *        User can set alignment for the cached frame data
 *        User can set exit on error, if set, worker will exit the task and free the resources when error occurs
 */
typedef void *esp_video_render_worker_handle_t;

/**
 * @brief  Video render worker configuration
 */
typedef struct {
    esp_video_render_task_cfg_t  task_cfg;                      /*!< Task configuration */
    uint32_t                     cache_size;                    /*!< Cache size */
    uint8_t                      align_size;                    /*!< Alignment size */
    bool                         exit_on_error;                 /*!< Exit on error */
    int (*worker)(esp_video_render_frame_t *frame, void *ctx);  /*!< Worker function */
    void *ctx;                                                  /*!< Worker context */
} esp_video_render_worker_cfg_t;

/**
 * @brief  Create video render worker
 *
 * @param[in]   cfg     Configuration
 * @param[out]  handle  Worker handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_RESOURCE  Not enough resource
 */
esp_video_render_err_t esp_video_render_worker_create(esp_video_render_worker_cfg_t *cfg,
                                                      esp_video_render_worker_handle_t *handle);

/**
 * @brief  Write frame data into video render worker
 *
 * @param[in]  handle  Worker handle
 * @param[in]  frame   Frame data
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_INVALID_STATE  Worker not in valid state
 *       - ESP_VIDEO_RENDER_ERR_FAIL           Failed to write frame
 */
esp_video_render_err_t esp_video_render_worker_write(esp_video_render_worker_handle_t handle,
                                                     esp_video_render_frame_t *frame);

/**
 * @brief  Destroy video render worker
 *
 * @note  When destroying it will wait for worker exit and free all related resources
 *
 * @param[in]  handle  Worker handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_FAIL         Failed to destroy worker
 */
esp_video_render_err_t esp_video_render_worker_destroy(esp_video_render_worker_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
