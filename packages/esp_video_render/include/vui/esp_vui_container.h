/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include "esp_video_render_types.h"
#include "esp_vui_overlay.h"
#include "esp_vui_widget.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Create video container
 *
 * @note  Container is a region on the screen that can contain widgets
 *        It can added to overlay and be part of stream
 *        Container can configure to have cache or not so that reduce the rendering time
 *        - When container with cache, widget will draw to the cache buffer
 *        - When container without cache, widget will draw to the frame buffer directly
 *
 * @param[in]   overlay     Overlay handle
 * @param[in]   info        Frame information
 * @param[in]   pos         Container position (absolute coords to screen)
 * @param[in]   with_cache  Whether to use cache for rendering
 * @param[out]  container   Container handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t esp_vui_container_create(esp_vui_overlay_handle_t overlay,
                                                esp_video_render_frame_info_t *info,
                                                esp_video_render_pos_t *pos,
                                                bool with_cache,
                                                esp_vui_container_handle_t *container);

/**
 * @brief  Get display rectangle of container
 *
 * @param[in]   container  Container handle
 * @param[out]  rect       Display rectangle pointerto store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_container_get_disp_rect(esp_vui_container_handle_t container,
                                                       const esp_video_render_rect_t **rect);

/**
 * @brief  Set background color of container
 *
 * @param[in]  container  Container handle
 * @param[in]  color      Background color
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_container_set_bg_color(esp_vui_container_handle_t container, esp_video_render_clr_t *color);

/**
 * @brief  Set transparent color for container
 *
 * @note  Only allow to set transparent color when container with cache
 *
 * @param[in]  container  Container handle
 * @param[in]  enable     True to enable transparent color mode
 * @param[in]  color      Transparent color
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_container_set_transparent_color(esp_vui_container_handle_t container,
                                                               bool enable, esp_video_render_clr_t *color);

/**
 * @brief  Set container alpha
 *
 * @note  Only allow to set alpha when container with cache
 *
 * @param[in]  container  Container handle
 * @param[in]  alpha      Alpha value (0=transparent, 255=opaque)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_container_set_alpha(esp_vui_container_handle_t container, uint8_t alpha);

/**
 * @brief  Lock container for composition (thread-safe)
 *
 * @param[in]  container  Container handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_TIMEOUT      Lock timeout
 */
esp_video_render_err_t esp_vui_container_compose_lock(esp_vui_container_handle_t container);

/**
 * @brief  Unlock container composition lock
 *
 * @param[in]  container  Container handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_container_compose_unlock(esp_vui_container_handle_t container);

/**
 * @brief  Add widget to container
 *
 * @param[in]  container  Container handle
 * @param[in]  widget     Widget to add
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_container_add_widget(esp_vui_container_handle_t container,
                                                    esp_vui_widget_t *widget);

/**
 * @brief  Remove widget from container
 *
 * @note  When widget is removed, it will auto call destroy callback of widget
 *
 * @param[in]  container  Container handle
 * @param[in]  widget     Widget to remove
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NOT_FOUND    Widget not found
 */
esp_video_render_err_t esp_vui_container_remove_widget(esp_vui_container_handle_t container,
                                                       esp_vui_widget_t *widget);

/**
 * @brief  Set visibility for container
 *
 * @param[in]  container  Container handle
 * @param[in]  visible    Whether container visible or not
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_container_set_visible(esp_vui_container_handle_t container,
                                                     bool visible);

/**
 * @brief  Notify container that composition changed
 *
 * @note  Will mark dirty and request widget redraw within dirty region
 *
 * @param[in]  container     Container handle
 * @param[in]  dirty_region  Dirty region
 * @param[in]  is_opaque     True if dirty region is opaque, false if not
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_container_notify_compose_changed(esp_vui_container_handle_t container,
                                                                const esp_video_render_rect_t *dirty_region,
                                                                bool is_opaque);

/**
 * @brief  Get blender for container usage
 *
 * @param[in]   container  Container handle
 * @param[out]  blender    Blender handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_container_get_blender(esp_vui_container_handle_t container,
                                                     esp_video_render_blend_handle_t *blender);

/**
 * @brief  Get render pool for container usage
 *
 * @param[in]   container  Container handle
 * @param[out]  pool       Pool handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_container_get_pool(esp_vui_container_handle_t container, void **pool);

/**
 * @brief  Destroy video container
 *
 * @note  When container is destroyed, it will auto call destroy callback of all widgets
 *
 * @param[in]  container  Container handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_container_destroy(esp_vui_container_handle_t container);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
