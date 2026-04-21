/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include "esp_video_render_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Video render blend hardware context
 */
typedef struct video_render_blend_hw_ctx video_render_blend_hw_ctx_t;

/**
 * @brief  Open video render blend hardware
 *
 * @param[out]  ctx  Video render blend hardware context
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t video_render_blend_hw_open(video_render_blend_hw_ctx_t **ctx);

/**
 * @brief  Close video render blend hardware
 *
 * @param[in]  ctx  Video render blend hardware context
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_FAIL         Failed to close video render blend hardware
 */
esp_video_render_err_t video_render_blend_hw_close(video_render_blend_hw_ctx_t *ctx);

/**
 * @brief  Process hardware accel video render blend
 *
 * @param[in]  ctx           Video render blend hardware context
 * @param[in]  dst           Destination framebuffer
 * @param[in]  src           Source framebuffer
 * @param[in]  dst_rect      Destination rectangle
 * @param[in]  src_rect      Source rectangle
 * @param[in]  global_alpha  Global alpha value (0-255)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_FAIL         Failed to process video render blend hardware
 */
esp_video_render_err_t video_render_blend_hw_process(video_render_blend_hw_ctx_t *ctx,
                                                     esp_video_render_fb_t *dst,
                                                     esp_video_render_fb_t *src,
                                                     esp_video_render_rect_t *dst_rect,
                                                     esp_video_render_rect_t *src_rect,
                                                     uint8_t global_alpha);

/**
 * @brief  Process hardware accel video render blend transparent color
 *
 * @note  When source pixel color equals to trans_color, the pixel will be transparent
 *
 * @param[in]  ctx          Video render blend hardware context
 * @param[in]  dst          Destination framebuffer
 * @param[in]  src          Source framebuffer
 * @param[in]  dst_rect     Destination rectangle
 * @param[in]  src_rect     Source rectangle
 * @param[in]  trans_color  Transparent color
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_FAIL         Failed to process video render blend transparent color
 */
esp_video_render_err_t video_render_blend_hw_transparent_color(video_render_blend_hw_ctx_t *ctx,
                                                               esp_video_render_fb_t *dst,
                                                               esp_video_render_fb_t *src,
                                                               esp_video_render_rect_t *dst_rect,
                                                               esp_video_render_rect_t *src_rect,
                                                               esp_video_render_clr_t *trans_color);

/**
 * @brief  Process hardware accel video render blend fill
 *
 * @param[in]  ctx       Video render blend hardware context
 * @param[in]  dst       Destination framebuffer
 * @param[in]  dst_rect  Destination rectangle
 * @param[in]  color     Fill color
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_FAIL         Failed to process video render blend fill
 */
esp_video_render_err_t video_render_blend_hw_fill(video_render_blend_hw_ctx_t *ctx,
                                                  esp_video_render_fb_t *dst,
                                                  esp_video_render_rect_t *dst_rect,
                                                  esp_video_render_clr_t *color);

/**
 * @brief  Process hardware accel video render blend bitblt
 *
 * @param[in]  ctx       Video render blend hardware context
 * @param[in]  dst       Destination framebuffer
 * @param[in]  src       Source framebuffer
 * @param[in]  dst_rect  Destination rectangle
 * @param[in]  src_rect  Source rectangle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_FAIL         Failed to process video render blend bitblt
 */
esp_video_render_err_t video_render_blend_hw_bitblt(video_render_blend_hw_ctx_t *ctx,
                                                    esp_video_render_fb_t *dst,
                                                    esp_video_render_fb_t *src,
                                                    esp_video_render_rect_t *dst_rect,
                                                    esp_video_render_rect_t *src_rect);

/**
 * @brief  Check if hardware accel video render blend can be accelerated
 *
 * @param[in]  dst       Destination framebuffer
 * @param[in]  src       Source framebuffer
 * @param[in]  dst_rect  Destination rectangle
 * @param[in]  src_rect  Source rectangle
 *
 * @return
 *       - true   Can be accelerated
 *       - false  Cannot be accelerated
 */
bool video_render_blend_hw_can_accel(const esp_video_render_fb_t *dst,
                                     const esp_video_render_fb_t *src,
                                     const esp_video_render_rect_t *dst_rect,
                                     const esp_video_render_rect_t *src_rect);

/**
 * @brief  Check if hardware accel video render blend fill can be accelerated
 *
 * @param[in]  dst       Destination framebuffer
 * @param[in]  dst_rect  Destination rectangle
 *
 * @return
 *       - true   Can be accelerated
 *       - false  Cannot be accelerated
 */
bool video_render_blend_hw_can_fill(const esp_video_render_fb_t *dst,
                                    const esp_video_render_rect_t *dst_rect);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
