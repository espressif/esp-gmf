/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_video_render_types.h"
#include "esp_video_render.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define VIDEO_RENDER_COMPOSE_MAX_DIRTY_AREA  (2)  /*!< Maximum number of dirty areas */

/**
 * @brief  Video render overlay configuration
 */
typedef struct {
    void *stream;  /*!< Stream handle */
    void *render;  /*!< Render handle */
} esp_vui_overlay_cfg_t;

/**
 * @brief  Video render overlay region
 */
typedef struct esp_vui_overlay_rgn esp_vui_overlay_rgn_t;

/**
 * @brief  Video render overlay refresh callback
 *
 * @param[in]  rgn           Overlay region
 * @param[in]  dirty_region  Dirty regions array
 * @param[in]  dirty_count   Number of dirty regions
 * @param[in]  user          User context
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK  On success
 *       - Others                   Error code
 */
typedef esp_video_render_err_t (*esp_vui_overlay_refresh_cb_t)(esp_vui_overlay_rgn_t *rgn,
                                                               const esp_video_render_dirty_rect_t *dirty_region,
                                                               uint8_t dirty_count,
                                                               void *user);

/**
 * @brief  Video render compose information
 */
typedef struct {
    uint8_t                        visible        : 1;                               /*!< Visibility flag */
    uint8_t                        is_visible     : 1;                               /*!< Current visibility state */
    uint8_t                        is_fresh       : 1;                               /*!< Fresh flag (needs redraw) */
    uint8_t                        is_empty       : 1;                               /*!< Empty flag */
    uint8_t                        opaque         : 1;                               /*!< Opaque flag */
    uint8_t                        is_trans_color : 1;                               /*!< Transparent color flag */
    esp_video_render_clr_t         trans_color;                                      /*!< Transparent color */
    uint8_t                        alpha;                                            /*!< Alpha value (0-255) */
    uint8_t                        zorder;                                           /*!< Z-order (layer order) */
    esp_video_render_rect_t        prev_rect;                                        /*!< Previous rectangle (for tracking movement) */
    esp_video_render_rect_t        disp_rect;                                        /*!< Display rectangle */
    uint8_t                        dirty_count;                                      /*!< Number of dirty areas */
    esp_video_render_dirty_rect_t  dirty_area[VIDEO_RENDER_COMPOSE_MAX_DIRTY_AREA];  /*!< Dirty area array */
} video_render_compose_t;

/**
 * @brief  Video render overlay region structure
 */
struct esp_vui_overlay_rgn {
    video_render_compose_t        compose;  /*!< Compose information */
    esp_video_render_frame_t      frame;    /*!< Frame data */
    esp_vui_overlay_refresh_cb_t  refresh;  /*!< Refresh callback */
    void                         *user;     /*!< User context */
    struct esp_vui_overlay_rgn   *next;     /*!< Next region pointer */
    esp_video_render_fb_t        *fb;       /*!< Framebuffer pointer */
};

/**
 * @brief  Create video render overlay
 *
 * @param[in]   cfg      Overlay configuration
 * @param[out]  overlay  Overlay handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t esp_vui_overlay_create(esp_vui_overlay_cfg_t *cfg, esp_vui_overlay_handle_t *overlay);

/**
 * @brief  Mark overlay as dirty (needs redraw)
 *
 * @param[in]  overlay  Overlay handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_overlay_mark_dirty(esp_vui_overlay_handle_t overlay);

/**
 * @brief  Check if overlay is dirty
 *
 * @param[in]  overlay  Overlay handle
 *
 * @return
 *       - true   Overlay is dirty
 *       - false  Overlay is not dirty
 */
bool esp_vui_overlay_is_dirty(esp_vui_overlay_handle_t overlay);

/**
 * @brief  Get dirty rectangles
 *
 * @param[in]   overlay      Overlay handle
 * @param[out]  dirty_count  Number of dirty rectangles
 *
 * @return
 *       - Pointer  to dirty rectangles array on success
 *       - NULL     on failure
 */
esp_video_render_dirty_rect_t *esp_vui_overlay_get_dirty_rects(esp_vui_overlay_handle_t overlay, uint8_t *dirty_count);

/**
 * @brief  Lock overlay for composition (thread-safe)
 *
 * @param[in]  overlay  Overlay handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_TIMEOUT      Lock timeout
 */
esp_video_render_err_t esp_vui_overlay_compose_lock(esp_vui_overlay_handle_t overlay);

/**
 * @brief  Unlock overlay composition lock
 *
 * @param[in]  overlay  Overlay handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_overlay_compose_unlock(esp_vui_overlay_handle_t overlay);

/**
 * @brief  Add region to overlay
 *
 * @param[in]  overlay  Overlay handle
 * @param[in]  region   Region to add
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_overlay_add_region(esp_vui_overlay_handle_t overlay, esp_vui_overlay_rgn_t *region);

/**
 * @brief  Remove region from overlay
 *
 * @param[in]  overlay  Overlay handle
 * @param[in]  region   Region to remove
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NOT_FOUND    Region not found
 */
esp_video_render_err_t esp_vui_overlay_remove_region(esp_vui_overlay_handle_t overlay, esp_vui_overlay_rgn_t *region);

/**
 * @brief  Add container to overlay
 *
 * @param[in]  overlay    Overlay handle
 * @param[in]  container  Container handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_overlay_add_container(esp_vui_overlay_handle_t overlay, esp_vui_container_handle_t container);

/**
 * @brief  Remove container from overlay
 *
 * @param[in]  overlay    Overlay handle
 * @param[in]  container  Container handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NOT_FOUND    Container not found
 */
esp_video_render_err_t esp_vui_overlay_remove_container(esp_vui_overlay_handle_t overlay, esp_vui_container_handle_t container);

/**
 * @brief  Get region from overlay
 *
 * @param[in]  overlay  Overlay handle
 *
 * @return
 *       - Region  pointer on success
 *       - NULL    on failure
 */
esp_vui_overlay_rgn_t *esp_vui_overlay_get_region(esp_vui_overlay_handle_t overlay);

/**
 * @brief  Update region position
 *
 * @note  Implementation will track prev_rect and mark fresh
 *
 * @param[in]  overlay        Overlay handle
 * @param[in]  region         Region to update
 * @param[in]  new_disp_rect  New display rectangle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_overlay_update_region(esp_vui_overlay_handle_t overlay,
                                                     esp_vui_overlay_rgn_t *region,
                                                     esp_video_render_rect_t *new_disp_rect);

/**
 * @brief  Redraw overlay region
 *
 * @param[in]  overlay       Overlay handle
 * @param[in]  dirty_region  Dirty regions array
 * @param[in]  dirty_count   Number of dirty regions
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_overlay_redraw(esp_vui_overlay_handle_t overlay,
                                              const esp_video_render_dirty_rect_t *dirty_region,
                                              uint8_t dirty_count);

/**
 * @brief  Get blender for overlay usage
 *
 * @param[in]   overlay  Overlay handle
 * @param[out]  blender  Blender handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_overlay_get_blender(esp_vui_overlay_handle_t overlay,
                                                   esp_video_render_blend_handle_t *blender);

/**
 * @brief  Get render pool for overlay usage
 *
 * @param[in]   overlay  Overlay handle
 * @param[out]  pool     Pool handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_overlay_get_pool(esp_vui_overlay_handle_t overlay, void **pool);

/**
 * @brief  Destroy video render overlay
 *
 * @param[in]  overlay  Overlay handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_overlay_destroy(esp_vui_overlay_handle_t overlay);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
