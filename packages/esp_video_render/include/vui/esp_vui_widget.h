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
 * @brief  Video widget structure
 */
typedef struct esp_vui_widget esp_vui_widget_t;

typedef struct {
    /**
     * @brief  Redraw callback for widget
     *
     * @note  If dirty is NULL in redraw callback, redraw full region; else redraw only dirty region.
     *
     * @param[in]  self      Widget handle
     * @param[in]  dst_fb    Destination frame buffer
     * @param[in]  dst_rect  Destination rectangle in dst_fb coordinates
     * @param[in]  dirty     Dirty rectangle in widget-local coordinates (or NULL for full redraw)
     *
     * @return
     *       - ESP_VIDEO_RENDER_ERR_OK  On success
     *       - Others                   Failed to redraw
     */
    esp_video_render_err_t (*redraw)(esp_vui_widget_t *self,
                                     esp_video_render_fb_t *dst_fb,
                                     const esp_video_render_rect_t *dst_rect,
                                     const esp_video_render_rect_t *dirty);

    /**
     * @brief  Destroy callback for widget
     *
     * @note  Widget needs destroy all resource it used including widget itself
     *
     * @param[in]  self  Widget handle
     */
    void (*destroy)(esp_vui_widget_t *self);
} esp_vui_widget_ops_t;

/**
 * @brief  Video widget structure definition
 *
 * @note  Redraw callback receives two rectangles:
 *        - dst_rect: destination rectangle in destination framebuffer coordinates
 *        - dirty: dirty rectangle in widget-local coordinates (relative to widget->rect top-left)
 *
 *        If dirty is NULL, widget should redraw its full region.
 */
struct esp_vui_widget {
    const esp_vui_widget_ops_t *ops;        /*!< Widget operations */
    bool                        visible;    /*!< Visibility flag */
    esp_video_render_rect_t     dirty;      /*!< Dirty rectangle (offset to container) */
    esp_video_render_rect_t     rect;       /*!< Widget rectangle (offset to container) */
    esp_vui_container_handle_t  container;  /*!< Container handle */
    uint16_t                    id;         /*!< Widget ID */
};

/**
 * @brief  Destroy video widget
 *
 * @param[in]  widget  Widget handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_widget_destroy(esp_vui_widget_t *widget);

/**
 * @brief  Set widget visibility
 *
 * @param[in]  widget   Widget handle
 * @param[in]  visible  True to make widget visible, false to hide
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_widget_set_visible(esp_vui_widget_t *widget, bool visible);

/**
 * @brief  Set widget position
 *
 * @param[in]  widget  Widget handle
 * @param[in]  pos     New position
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_widget_set_pos(esp_vui_widget_t *widget, esp_video_render_pos_t *pos);

/**
 * @brief  Get render pool for widget usage
 *
 * @param[in]   widget  Widget handle
 * @param[out]  pool    Pool handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_widget_get_pool(esp_vui_widget_t *widget, void **pool);

/**
 * @brief  Get blender for widget usage
 *
 * @param[in]   widget   Widget handle
 * @param[out]  blender  Blender handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_widget_get_blender(esp_vui_widget_t *widget,
                                                  esp_video_render_blend_handle_t *blender);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
