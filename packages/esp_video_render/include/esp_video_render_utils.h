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
 * @brief  Decode image use prefer format
 *
 * @note  When decode success, decoded frame buffer is put to `frame->data`
 *        User need to call `free` API to free it when not used anymore
 *
 * @param[in]   pool        Elements pool of `esp_gmf_pool_handle_t`
 * @param[in]   img         Image to decode
 * @param[in]   prefer_fmt  Preferred output format (set to `ESP_VIDEO_RENDER_FORMAT_NONE` for default format)
 * @param[out]  frame       Output frame to store decoded result
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK             On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG    Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM         Not enough memory
 *       - ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED  Format not supported
 */
esp_video_render_err_t esp_video_render_decode_image(void *pool, esp_video_render_img_t *img,
                                                     esp_video_render_format_t prefer_fmt,
                                                     esp_video_render_frame_t *frame);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
