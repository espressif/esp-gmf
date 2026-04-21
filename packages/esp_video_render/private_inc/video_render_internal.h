/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_video_render.h"
#include "video_render_compose.h"
#include "video_render_proc.h"
#include "esp_video_render_blender.h"
#include "esp_vui_overlay.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_oal_thread.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Maximum dirty region count
 */
#define VIDEO_RENDER_MAX_DIRTY_REGION  (16)

/**
 * @brief  Video render context
 */
typedef struct video_render_t video_render_t;

/**
 * @brief  Video render dirty monitor callback
 *
 * @param[in]  dirty   Dirty rectangle array
 * @param[in]  filled  Number of filled rectangles
 *
 * @return
 *       - 0       On success
 *       - Others  On error
 */
typedef int (*video_render_dirty_monitor_cb)(const esp_video_render_dirty_rect_t *dirty, int filled);

/**
 * @brief  Video render fb info
 */
typedef struct _video_render_fb_info_t {
    esp_video_render_dirty_rect_t   dirty_region[VIDEO_RENDER_MAX_DIRTY_REGION];  /*!< Dirty region array */
    uint8_t                         dirty_count;                                  /*!< Dirty count */
    bool                            is_bg_filled;                                 /*!< Is background filled */
    bool                            redraw_all;                                   /*!< Redraw all */
    uint8_t                        *fb_buffer;                                    /*!< Framebuffer buffer */
    struct _video_render_fb_info_t *next;                                         /*!< Next fb info */
} video_render_fb_info_t;

/**
 * @brief  Video render backend
 */
typedef struct {
    const esp_video_render_backend_ops_t *ops;            /*!< Backend operations */
    esp_video_render_backend_handle_t     handle;         /*!< Backend handle */
    esp_video_render_fb_t                 fb;             /*!< Framebuffer */
    esp_video_render_fb_t                 bg_fb;          /*!< Background framebuffer */
    esp_video_render_clr_t                bg_color;       /*!< Background color */
    bool                                  is_bg_decoded;  /*!< Is background decoded */
    bool                                  is_bg_set;      /*!< Is background set */
    video_render_fb_info_t               *fb_info;        /*!< Framebuffer info */
    video_render_fb_info_t               *prev_fb;        /*!< Previous framebuffer info */
    video_render_fb_info_t               *cur_fb;         /*!< Current framebuffer info */
} video_render_backend_t;

/**
 * @brief  Video render stream
 */
typedef struct _video_render_stream {
    video_render_compose_t         compose;       /*!< Compose */
    esp_video_render_frame_info_t  frame_info;    /*!< Frame information */
    esp_video_render_rect_t        src_rect;      /*!< Source rectangle */
    bool                           cached;        /*!< Is cached */
    uint8_t                       *cached_data;   /*!< Cached data */
    uint32_t                       cached_size;   /*!< Cached size */
    esp_video_render_fb_t          fb;            /*!< Framebuffer */
    bool                           using_fb;      /*!< Is using framebuffer */
    bool                           running;       /*!< Is running */
    bool                           need_rebuild;  /*!< Is need rebuild */
    video_render_mutex_handle_t    mutex;         /*!< Mutex */
    video_render_proc_handle_t     proc_hd;       /*!< Proc handle */

    int16_t                      degree;        /*!< Degree */
    struct _video_render_stream *next;          /*!< Next stream */
    video_render_t              *video_render;  /*!< Video render */
    esp_vui_overlay_handle_t     overlay;       /*!< Overlay */
    uint32_t                     write_start;   /*!< Write start */
    uint32_t                     write_count;   /*!< Write count */
} video_render_stream_t;

/**
 * @brief  Video render
 */
struct video_render_t {
    esp_video_render_cfg_t           cfg;                /*!< Configuration */
    esp_video_render_task_cfg_t      task_cfg;           /*!< Task configuration */
    esp_video_render_event_cb_t      event_cb;           /*!< Event callback */
    void                            *event_ctx;          /*!< Event context */
    video_render_backend_t           backend;            /*!< Backend */
    video_render_stream_t           *stream_list;        /*!< Stream list */
    bool                             running;            /*!< Is running */
    esp_video_render_blend_handle_t  blender;            /*!< Blender */
    video_render_mutex_handle_t      render_mutex;       /*!< Render mutex */
    video_render_event_grp_handle_t  event_grp;          /*!< Event group */
    esp_video_render_format_t        display_format;     /*!< Display format */
    uint16_t                         display_width;      /*!< Display width */
    uint16_t                         display_height;     /*!< Display height */
    uint8_t                          active_stream_num;  /*!< Active stream number */
    video_render_mutex_handle_t      compose_mutex;      /*!< Compose mutex */
    video_render_dirty_monitor_cb    dirty_monitor_cb;   /*!< Dirty monitor callback (debug only) */
};

#ifdef __cplusplus
}
#endif  /* __cplusplus */
