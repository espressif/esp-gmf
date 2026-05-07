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
 * @brief  Video render blender configuration
 */
typedef struct {
} esp_video_render_blend_cfg_t;

/**
 * @brief  Open video render blender
 *
 * @param[in]   cfg      Blender configuration
 * @param[out]  blender  Blender handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t esp_video_render_blend_open(esp_video_render_blend_cfg_t *cfg, esp_video_render_blend_handle_t *blender);

/**
 * @brief  Blend source framebuffer into destination framebuffer
 *
 * @note  Implements: dst = src * alpha + dst * (1-alpha)
 *        Blend constraint: src rect size must be same as dst rect size
 *
 * @param[in]  blender       Blender handle
 * @param[in]  dst           Destination framebuffer
 * @param[in]  src           Source framebuffer
 * @param[in]  global_alpha  Global alpha value (0-255)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_blend_process(esp_video_render_blend_handle_t blender,
                                                      esp_video_render_fb_t *dst, esp_video_render_fb_t *src,
                                                      esp_video_render_rect_t *dst_rect,
                                                      esp_video_render_rect_t *src_rect,
                                                      uint8_t global_alpha);

/**
 * @brief  Blend source framebuffer into destination framebuffer
 *
 * @note  Implements: dst = src  if (src_pixel != trans_color)
 *
 * @param[in]  blender      Blender handle
 * @param[in]  dst          Destination framebuffer
 * @param[in]  src          Source framebuffer
 * @param[in]  trans_color  When source pixel color equals to trans_color, the pixel will be transparent
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_blend_transparent_color(esp_video_render_blend_handle_t blender,
                                                                esp_video_render_fb_t *dst, esp_video_render_fb_t *src,
                                                                esp_video_render_rect_t *dst_rect,
                                                                esp_video_render_rect_t *src_rect,
                                                                esp_video_render_clr_t *trans_color);

/**
 * @brief  Fill framebuffer with color
 *
 * @param[in]  blender  Blender handle
 * @param[in]  dst      Destination framebuffer
 * @param[in]  color    Fill color
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_blend_fill(esp_video_render_blend_handle_t blender,
                                                   esp_video_render_fb_t *dst,
                                                   esp_video_render_rect_t *dst_rect,
                                                   esp_video_render_clr_t *color);

/**
 * @brief  Bit block transfer (copy) from source to destination
 *
 * @param[in]  blender   Blender handle
 * @param[in]  dst       Destination framebuffer
 * @param[in]  src       Source framebuffer
 * @param[in]  dst_rect  Destination rectangle
 * @param[in]  src_rect  Source rectangle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_blend_bitblt(esp_video_render_blend_handle_t blender,
                                                     esp_video_render_fb_t *dst,
                                                     esp_video_render_fb_t *src,
                                                     esp_video_render_rect_t *dst_rect,
                                                     esp_video_render_rect_t *src_rect);

/**
 * @brief  Close video render blender
 *
 * @param[in]  blender  Blender handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_blend_close(esp_video_render_blend_handle_t blender);

/**
 * @brief  Software blend source framebuffer into destination framebuffer
 *
 * @note  Implements: dst = src * alpha + dst * (1-alpha)
 *
 * @param[in]  dst           Destination framebuffer
 * @param[in]  src           Source framebuffer
 * @param[in]  global_alpha  Global alpha value (0-255)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_blend_sw(esp_video_render_fb_t *dst, esp_video_render_fb_t *src,
                                                 esp_video_render_rect_t *dst_rect,
                                                 esp_video_render_rect_t *src_rect,
                                                 uint8_t global_alpha);

/**
 * @brief  Software blend source framebuffer into destination framebuffer
 *
 * @note  Implements: dst = src  if (src_pixel != trans_color)
 *
 * @param[in]  dst          Destination framebuffer
 * @param[in]  src          Source framebuffer
 * @param[in]  trans_color  When source pixel color equals to trans_color, the pixel will be transparent
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_blend_transparent_color_sw(esp_video_render_fb_t *dst,
                                                                   esp_video_render_fb_t *src,
                                                                   esp_video_render_rect_t *dst_rect,
                                                                   esp_video_render_rect_t *src_rect,
                                                                   esp_video_render_clr_t *trans_color);

/**
 * @brief  Software fill framebuffer with color
 *
 * @param[in]  dst    Destination framebuffer
 * @param[in]  color  Fill color
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_video_render_blend_fill_sw(esp_video_render_fb_t *dst, esp_video_render_rect_t *dst_rect,
                                                      esp_video_render_clr_t *color);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
