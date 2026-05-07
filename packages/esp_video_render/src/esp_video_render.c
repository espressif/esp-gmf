/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <sdkconfig.h>
#include <math.h>
#include "esp_timer.h"
#include "esp_video_render.h"
#include "esp_video_render_utils.h"
#include "esp_vui_overlay.h"
#include "video_render_proc.h"
#include "video_render_sys.h"
#include "video_render_utils.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_video_render.h"
#include "esp_video_render_blender.h"
#include "video_render_blend_flow.h"
#include "video_render_compose.h"
#include "esp_gmf_video_param.h"
#include "esp_log.h"
#include "esp_video_render_log.h"
#include "video_render_internal.h"
#include "video_render_measure.h"

#define TAG                                "VIDEO_RENDER"
#define VIDEO_RENDER_MAX_BACKEND_NUM       (2)
#define VIDEO_RENDER_FB_DONE_BIT           (1 << 0)
#define VIDEO_RENDER_EXIT_BIT              (1 << 1)
#define VIDEO_RENDER_MAX_DIRTY_REGION      (16)
#define VIDEO_RENDER_CLOSE_CLEANUP_PASSES  (2)

// Forward declarations
static esp_video_render_err_t video_render_check_proc(video_render_stream_t *stream, esp_video_render_proc_type_t proc_type);

static bool video_render_has_backend(video_render_t *video_render)
{
    return video_render && video_render->backend.handle;
}

esp_video_render_err_t esp_video_render_create(esp_video_render_cfg_t *cfg, esp_video_render_handle_t *render)
{
    if (cfg == NULL || render == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_t *video_render = video_render_calloc(1, sizeof(video_render_t));
    if (video_render == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
    video_render->cfg = *cfg;
    do {
        video_render->render_mutex = video_render_mutex_create();
        if (video_render->render_mutex == NULL) {
            break;
        }
        video_render->compose_mutex = video_render_mutex_create();
        if (video_render->compose_mutex == NULL) {
            break;
        }
        if (video_render->event_grp == NULL) {
            video_render_event_grp_create(&video_render->event_grp);
            if (video_render->event_grp == NULL) {
                ESP_LOGE(TAG, "Failed to create event group");
                break;
            }
        }
        // Open blender
        esp_video_render_blend_cfg_t blend_cfg = {};
        if (esp_video_render_blend_open(&blend_cfg, &video_render->blender) != ESP_VIDEO_RENDER_ERR_OK) {
            break;
        }
        *render = (esp_video_render_handle_t)video_render;
        return ESP_VIDEO_RENDER_ERR_OK;
    } while (0);
    esp_video_render_destroy((esp_video_render_handle_t)video_render);
    return ret;
}

esp_video_render_err_t esp_video_render_task_reconfigure(esp_video_render_handle_t render,
                                                         esp_video_render_task_cfg_t *task_cfg)
{
    if (render == NULL || task_cfg == NULL || task_cfg->stack_size == 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_t *video_render = (video_render_t *)render;
    if (video_render->running) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    video_render->task_cfg = *task_cfg;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_set_event_cb(esp_video_render_handle_t render,
                                                     esp_video_render_event_cb_t event_cb, void *ctx)
{
    if (render == NULL || event_cb == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_t *video_render = (video_render_t *)render;
    video_render->event_cb = event_cb;
    video_render->event_ctx = ctx;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_set_display(esp_video_render_handle_t render,
                                                    esp_video_render_backend_cfg_t *cfg)
{
    if (render == NULL || cfg == NULL || cfg->ops == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_t *video_render = (video_render_t *)render;
    if (video_render->running) {
        ESP_LOGE(TAG, "Not allow add display after running");
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    video_render_backend_t *backend = &video_render->backend;
    if (backend->handle) {
        backend->ops->deinit(backend->handle);
        backend->handle = NULL;
    }
    backend->ops = cfg->ops;
    // TODO init display when first stream running
    ret = backend->ops->init(cfg->cfg, cfg->cfg_size, &backend->handle);
    if (ret == ESP_VIDEO_RENDER_ERR_OK) {
        esp_video_render_disp_info_t disp_info = {};
        ret = backend->ops->get_display_info(backend->handle, &disp_info);
        if (video_render->display_format == ESP_VIDEO_RENDER_FORMAT_NONE) {
            video_render->display_format = disp_info.format;
            video_render->display_width = disp_info.width;
            video_render->display_height = disp_info.height;
        }
    }
    return ret;
}

esp_video_render_err_t esp_video_render_get_display_info(esp_video_render_handle_t render,
                                                         esp_video_render_disp_info_t *display)
{
    if (render == NULL || display == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_t *video_render = (video_render_t *)render;
    video_render_backend_t *backend = &video_render->backend;
    if (backend->handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    if (backend->ops->get_display_info == NULL) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    return backend->ops->get_display_info(backend->handle, display);
}

static void video_render_free_background(video_render_backend_t *backend)
{
    if (backend->bg_fb.data) {
        if (backend->is_bg_decoded) {
            video_render_free(backend->bg_fb.data);
        }
        backend->bg_fb.data = NULL;
    }
    memset(&backend->fb, 0, sizeof(backend->fb));
    backend->is_bg_set = false;
    backend->is_bg_decoded = false;
}

esp_video_render_err_t esp_video_render_set_bg_image(esp_video_render_handle_t render,
                                                     esp_video_render_img_t *img)
{
    // Allow image to be NULL to clear image
    if (render == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_t *video_render = (video_render_t *)render;
    video_render_backend_t *backend = &video_render->backend;
    if (backend->handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    video_render_mutex_lock(video_render->render_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    video_render_free_background(backend);
    if (img) {
        esp_video_render_frame_t frame = {};
        if (video_render_is_encoded(img->info.format)) {
            ret = esp_video_render_decode_image(video_render->cfg.pool, img, video_render->display_format, &frame);
            if (ret == ESP_VIDEO_RENDER_ERR_OK) {
                backend->is_bg_decoded = true;
            } else {
                ESP_LOGE(TAG, "Failed to decode background image ret %d", ret);
            }
        } else {
            frame.format = img->info.format;
            frame.width = img->info.width;
            frame.height = img->info.height;
            frame.data = img->data;
            frame.size = img->size;
        }
        if (ret == ESP_VIDEO_RENDER_ERR_OK) {
            esp_video_render_fb_t *fb = &backend->bg_fb;
            fb->info.format = (esp_video_render_format_t)frame.format;
            fb->info.width = frame.width;
            fb->info.height = frame.height;
            fb->data = frame.data;
            fb->size = frame.size;
            backend->is_bg_set = true;
        }
    }
    video_render_mutex_unlock(video_render->render_mutex);
    return ret;
}

esp_video_render_err_t esp_video_render_get_pool(esp_video_render_handle_t render, void **pool)
{
    if (render == NULL || pool == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_t *video_render = (video_render_t *)render;
    *pool = video_render->cfg.pool;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_set_bg_color(esp_video_render_handle_t render,
                                                     esp_video_render_clr_t *color)
{
    if (render == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_t *video_render = (video_render_t *)render;
    video_render_backend_t *backend = &video_render->backend;
    if (backend->handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    video_render_mutex_lock(video_render->render_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    video_render_free_background(backend);
    if (color) {
        backend->is_bg_set = true;
        backend->bg_color = *color;
    } else {
        backend->is_bg_set = false;
        memset(&backend->bg_color, 0, sizeof(backend->bg_color));
    }
    video_render_mutex_unlock(video_render->render_mutex);
    return ESP_VIDEO_RENDER_ERR_OK;
}

static bool is_display_compatible(esp_video_render_format_t display_format, esp_video_render_format_t format)
{
    if (display_format == format) {
        return true;
    }
    // TODO may support RGB565A with RGB565 for blend support it
    return false;
}

esp_video_render_err_t video_render_check_basic_process(video_render_stream_t *stream)
{
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    // Check for decoder
    if (video_render_is_encoded(stream->frame_info.format)) {
        ret = video_render_check_proc(stream, ESP_VIDEO_RENDER_PROC_DEC);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to add decode process ret %d", ret);
            return ret;
        }
    }
    // Check for color convert
    video_render_backend_t *backend = &stream->video_render->backend;
    if (backend->handle) {
        esp_video_render_disp_info_t disp_info = {};
        ret = backend->ops->get_display_info(backend->handle, &disp_info);
        if (ret == ESP_VIDEO_RENDER_ERR_OK) {
            if (is_display_compatible(disp_info.format, stream->frame_info.format) == false) {
                ret = video_render_check_proc(stream, ESP_VIDEO_RENDER_PROC_CCVT);
                if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGE(TAG, "Failed to add color convert process ret %d", ret);
                    return ret;
                }
                stream->fb.info.format = disp_info.format;
            }
        }
    }
    stream->need_rebuild = true;
    return ret;
}

static void sort_streams(video_render_t *video_render)
{
    if (video_render == NULL || video_render->stream_list == NULL || video_render->stream_list->next == NULL) {
        return;
    }
    video_render_stream_t *sorted = NULL;
    video_render_stream_t *cur = video_render->stream_list;
    while (cur) {
        video_render_stream_t *next = cur->next;
        video_render_stream_t *prev = NULL;
        video_render_stream_t *iter = sorted;
        while (iter && iter->compose.zorder <= cur->compose.zorder) {
            prev = iter;
            iter = iter->next;
        }
        if (prev == NULL) {
            cur->next = sorted;
            sorted = cur;
        } else {
            cur->next = prev->next;
            prev->next = cur;
        }
        cur = next;
    }
    video_render->stream_list = sorted;
}

static void add_stream(video_render_t *video_render, video_render_stream_t *stream)
{
    bool first_stream = false;
    video_render_mutex_lock(video_render->render_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    if (video_render->stream_list == NULL) {
        video_render->stream_list = stream;
    } else {
        video_render_stream_t *tail = video_render->stream_list;
        while (tail->next) {
            tail = tail->next;
        }
        tail->next = stream;
    }
    sort_streams(video_render);
    first_stream = (video_render->active_stream_num == 0);
    video_render->active_stream_num++;
    video_render_mutex_unlock(video_render->render_mutex);
    if (first_stream && video_render->event_cb) {
        video_render->event_cb(ESP_VIDEO_RENDER_EVENT_TYPE_OPENED, video_render->event_ctx);
    }
}

static bool remove_stream(video_render_t *video_render, video_render_stream_t *stream)
{
    bool found = false;
    bool need_sync_cleanup = false;
    video_render_mutex_lock(video_render->render_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    video_render_stream_t *cur = video_render->stream_list;
    while (cur) {
        if (cur == stream) {
            found = true;
        }
        cur = cur->next;
    }
    if (found == false) {
        ESP_LOGE(TAG, "Stream %p not found in stream list", stream);
        video_render_mutex_unlock(video_render->render_mutex);
        return false;
    }

    if (video_render->running) {
        stream->compose.prev_rect = stream->compose.disp_rect;
        stream->compose.is_fresh = true;
        stream->compose.visible = 0;
        video_render_mutex_unlock(video_render->render_mutex);
        for (int i = 0; i < VIDEO_RENDER_CLOSE_CLEANUP_PASSES; i++) {
            video_render_event_grp_clr_bits(video_render->event_grp, VIDEO_RENDER_FB_DONE_BIT);
            video_render_event_grp_wait_bits(video_render->event_grp, VIDEO_RENDER_FB_DONE_BIT, VIDEO_RENDER_MAX_LOCK_TIME);
        }
        // Wait for render complete
        video_render_mutex_lock(video_render->render_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    } else if (stream->fb.data && stream->compose.visible) {
        stream->compose.prev_rect = stream->compose.disp_rect;
        stream->compose.is_fresh = true;
        stream->compose.visible = false;
        need_sync_cleanup = true;
    }

    if (need_sync_cleanup && video_render_has_backend(video_render)) {
        for (int i = 0; i < VIDEO_RENDER_CLOSE_CLEANUP_PASSES; i++) {
            video_render_blend_execute(video_render, &video_render->backend);
        }
    }

    video_render_stream_t *pre = NULL;
    cur = video_render->stream_list;
    while (cur) {
        if (cur == stream) {
            if (pre) {
                pre->next = cur->next;
            } else {
                video_render->stream_list = cur->next;
            }
            break;
        }
        pre = cur;
        cur = cur->next;
    }
    video_render->active_stream_num--;
    video_render_mutex_unlock(video_render->render_mutex);
    return true;
}

static esp_video_render_err_t try_create_render_thread(video_render_t *video_render, bool force)
{
    // Stream handle already passed as parameter
    video_render_mutex_lock(video_render->render_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    bool need_create = video_render->active_stream_num > 1 || force;
    if (need_create && video_render->running == false) {
        esp_video_render_task_cfg_t task_cfg = video_render->task_cfg;
        if (task_cfg.stack_size == 0) {
            task_cfg.stack_size = CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_STACK_SIZE;
            task_cfg.priority = CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_PRIORITY;
            task_cfg.core_id = CONFIG_ESP_VIDEO_RENDER_BLEND_THREAD_CORE_ID;
        }
        video_render->running = true;
        if (ESP_GMF_ERR_OK != esp_gmf_oal_thread_create(NULL, "Blender", video_render_blend_thread, video_render,
                                                        task_cfg.stack_size, task_cfg.priority, true, task_cfg.core_id)) {
            video_render->running = false;
            ESP_LOGE(TAG, "Failed to create blender thread");
            video_render_mutex_unlock(video_render->render_mutex);
            return ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
        }
    }
    video_render_mutex_unlock(video_render->render_mutex);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_stream_open(esp_video_render_handle_t render,
                                                    esp_video_render_stream_info_t *stream_info,
                                                    esp_video_render_stream_handle_t *handle)
{
    if (render == NULL || stream_info == NULL || stream_info->info.format == ESP_VIDEO_RENDER_FORMAT_NONE) {
        ESP_LOGE(TAG, "Invalid argument when open render:%p stream_info:%p", render, stream_info);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    video_render_t *video_render = (video_render_t *)render;
    if (video_render_has_backend(video_render) == false) {
        ESP_LOGE(TAG, "Not support open when backend not set yet");
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    // TODO default put to last when change zorder sort stream list
    video_render_stream_t *stream = (video_render_stream_t *)video_render_calloc(1, sizeof(video_render_stream_t));
    if (stream == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    do {
        stream->frame_info = stream_info->info;
        stream->cached = stream_info->cached;
        stream->compose.visible = true;
        stream->compose.alpha = 255;
        stream->compose.is_empty = true;
        stream->compose.opaque = true;  // Stream no sub-widgets always set to opaque
        stream->video_render = video_render;
        add_stream(video_render, stream);
        ret = video_render_proc_create(video_render->cfg.pool, &stream->proc_hd);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create proc ret %d", ret);
            break;
        }
        ret = video_render_check_basic_process(stream);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to add basic process ret %d", ret);
            break;
        }
        stream->mutex = video_render_mutex_create();
        if (stream->mutex == NULL) {
            ret = ESP_VIDEO_RENDER_ERR_NO_MEM;
            break;
        }
        ret = try_create_render_thread(video_render, false);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create render thread ret %d", ret);
            break;
        }
        // Set basic information for stream input and output
        stream->compose.disp_rect.x = 0;
        stream->compose.disp_rect.y = 0;
        stream->compose.disp_rect.width = stream_info->info.width;
        stream->compose.disp_rect.height = stream_info->info.height;
        stream->src_rect.x = 0;
        stream->src_rect.y = 0;
        stream->src_rect.width = stream_info->info.width;
        stream->src_rect.height = stream_info->info.height;
        *handle = (esp_video_render_stream_handle_t)stream;
        return ESP_VIDEO_RENDER_ERR_OK;
    } while (0);
    esp_video_render_stream_close((esp_video_render_stream_handle_t)stream);
    return ret;
}

esp_video_render_err_t esp_video_render_stream_render_async(esp_video_render_stream_handle_t handle)
{
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    if (stream == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    // Ensure render thread processes this stream even without video frames
    return try_create_render_thread(stream->video_render, true);
}

esp_video_render_err_t esp_video_render_get_blender(esp_video_render_handle_t render,
                                                    esp_video_render_blend_handle_t *blender)
{
    if (render == NULL || blender == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_t *video_render = (video_render_t *)render;
    *blender = video_render->blender;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_stream_get_overlay(esp_video_render_stream_handle_t handle,
                                                           esp_vui_overlay_handle_t *overlay)
{
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    if (stream == NULL || overlay == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (stream->overlay == NULL) {
        esp_vui_overlay_cfg_t cfg = {
            .stream = stream,
            .render = stream->video_render,
        };
        esp_video_render_err_t ret = esp_vui_overlay_create(&cfg, &stream->overlay);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create overlay ret %d", ret);
            return ret;
        }
    }
    stream->running = true;
    *overlay = stream->overlay;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t video_render_check_proc(video_render_stream_t *stream, esp_video_render_proc_type_t proc_type)
{
    esp_gmf_element_handle_t element = video_render_proc_get_element(stream->proc_hd, proc_type);
    if (element) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    esp_video_render_err_t ret = video_render_proc_add(stream->proc_hd, &proc_type, 1);
    if (ret == ESP_VIDEO_RENDER_ERR_OK) {
        stream->need_rebuild = true;
    }
    return ret;
}

esp_video_render_err_t esp_video_render_stream_set_src_rect(esp_video_render_stream_handle_t handle,
                                                            esp_video_render_rect_t *src_rect)
{
    if (handle == NULL || src_rect == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    if (src_rect->x + src_rect->width > stream->frame_info.width || src_rect->y + src_rect->height > stream->frame_info.height) {
        ESP_LOGE(TAG, "Source rect over limited");
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (stream->frame_info.width != src_rect->width || stream->frame_info.height != src_rect->height) {
        esp_video_render_err_t ret = video_render_check_proc(stream, ESP_VIDEO_RENDER_PROC_CROP);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to add crop process ret %d", ret);
            return ret;
        }
        esp_gmf_element_handle_t crop_element = video_render_proc_get_element(stream->proc_hd, ESP_VIDEO_RENDER_PROC_CROP);
        // Configuration for crop information
        esp_gmf_video_rgn_t crop_region = {
            .x = src_rect->x,
            .y = src_rect->y,
            .width = src_rect->width,
            .height = src_rect->height,
        };
        if (esp_gmf_video_param_set_cropped_region(crop_element, &crop_region) != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set crop region");
            return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        }
        // Here force to rebuild to let setting take effect
        stream->need_rebuild = true;
    }
    stream->src_rect = *src_rect;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_stream_set_zorder(esp_video_render_stream_handle_t handle,
                                                          uint8_t zorder)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    video_render_t *video_render = stream->video_render;
    if (stream->compose.zorder == zorder) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    video_render_mutex_lock(video_render->render_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    stream->compose.zorder = zorder;
    stream->compose.is_fresh = true;
    if (video_render->stream_list && video_render->stream_list->next) {
        sort_streams(video_render);
    }
    video_render_mutex_unlock(video_render->render_mutex);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_stream_set_disp_rect(esp_video_render_stream_handle_t handle,
                                                             esp_video_render_rect_t *disp_rect)
{
    if (handle == NULL || disp_rect == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    if (rect_equal(&stream->compose.disp_rect, disp_rect)) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    if (stream->fb.data && stream->compose.visible) {
        stream->compose.prev_rect = stream->compose.disp_rect;
    }
    bool need_reconfig = false;
    if (disp_rect->width != stream->compose.disp_rect.width || disp_rect->height != stream->compose.disp_rect.height) {
        need_reconfig = true;
    }
    stream->compose.disp_rect = *disp_rect;
    if (stream->fb.data) {
        stream->compose.is_fresh = true;
    }
    esp_video_render_err_t ret = video_render_check_proc(stream, ESP_VIDEO_RENDER_PROC_SCALE);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to add rotate process ret %d", ret);
        return ret;
    }
    if (need_reconfig) {
        esp_gmf_element_handle_t scale_element = video_render_proc_get_element(stream->proc_hd, ESP_VIDEO_RENDER_PROC_SCALE);
        esp_gmf_video_resolution_t res = {
            .width = disp_rect->width,
            .height = disp_rect->height,
        };
        if (stream->degree == 90 || stream->degree == 270) {
            esp_gmf_element_handle_t rotate_element = video_render_proc_get_element(stream->proc_hd, ESP_VIDEO_RENDER_PROC_ROTATE);
            if (rotate_element != scale_element) {
                res.width = disp_rect->height;
                res.height = disp_rect->width;
            }
        }
        if (esp_gmf_video_param_set_dst_resolution(scale_element, &res) != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set dst resolution");
            return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        }
        // Here force to rebuild to let setting take effect
        stream->need_rebuild = true;
    }
    return ret;
}

esp_video_render_err_t esp_video_render_stream_set_rotate(esp_video_render_stream_handle_t handle,
                                                          int16_t degree)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    video_render_mutex_lock(stream->mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    if (stream->degree == degree) {
        video_render_mutex_unlock(stream->mutex);
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    stream->degree = degree;
    if (stream->fb.data) {
        if (stream->compose.visible) {
            stream->compose.prev_rect = stream->compose.disp_rect;
        }
        stream->compose.is_fresh = true;
    }
    esp_video_render_err_t ret = video_render_check_proc(stream, ESP_VIDEO_RENDER_PROC_ROTATE);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to add rotate process ret %d", ret);
        return ret;
    }
    esp_gmf_element_handle_t rotate_element = video_render_proc_get_element(stream->proc_hd, ESP_VIDEO_RENDER_PROC_ROTATE);
    degree = degree % 360;
    if (degree < 0) {
        degree += 360;
    }
    if (esp_gmf_video_param_set_rotate_angle(rotate_element, (uint16_t)degree) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to set rotate angle");
        return ESP_VIDEO_RENDER_ERR_FAIL;
    }
    stream->need_rebuild = true;
    video_render_mutex_unlock(stream->mutex);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_stream_set_visible(esp_video_render_stream_handle_t handle,
                                                           bool visible)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    if (stream->compose.visible == visible) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    video_render_mutex_lock(stream->video_render->compose_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    stream->compose.visible = visible;
    if (stream->fb.data) {
        stream->compose.is_fresh = true;
    }
    video_render_mutex_unlock(stream->video_render->compose_mutex);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_stream_set_alpha(esp_video_render_stream_handle_t handle,
                                                         uint8_t alpha)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    if (stream->compose.alpha == alpha) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    video_render_mutex_lock(stream->video_render->compose_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    stream->compose.alpha = alpha;
    if (stream->fb.data) {
        stream->compose.is_fresh = true;
    }
    video_render_mutex_unlock(stream->video_render->compose_mutex);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_stream_acquire_fb(esp_video_render_stream_handle_t handle,
                                                          esp_video_render_fb_t *fb)
{
    if (handle == NULL || fb == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    video_render_t *video_render = stream->video_render;
    video_render_backend_t *backend = &video_render->backend;
    if (backend->handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    esp_video_render_err_t ret = backend->ops->get_fb(backend->handle, fb);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        return ret;
    }
    return backend->ops->lock_fb(backend->handle, fb, true);
}

esp_video_render_err_t esp_video_render_stream_release_fb(esp_video_render_stream_handle_t handle,
                                                          esp_video_render_fb_t *fb)
{
    if (handle == NULL || fb == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    video_render_t *video_render = stream->video_render;
    video_render_backend_t *backend = &video_render->backend;
    if (backend->handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    return backend->ops->lock_fb(backend->handle, fb, false);
}

static inline esp_video_render_err_t video_render_write(video_render_t *video_render, video_render_stream_t *stream)
{
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    if (stream->compose.disp_rect.width == 0 || stream->compose.disp_rect.height == 0) {
        stream->compose.disp_rect.width = stream->frame_info.width;
        stream->compose.disp_rect.height = stream->frame_info.height;
        ESP_LOGE(TAG, "Write override display_rect: %dx%d", stream->compose.disp_rect.width, stream->compose.disp_rect.height);
    }
    if (stream->cached == false) {
        stream->compose.is_fresh = true;
        stream->compose.is_empty = false;
    }
    // Optimized for one stream case
    if (video_render->active_stream_num == 1 && video_render->running == false) {
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Render directly\n");
        video_render_backend_t *backend = &video_render->backend;
        if (backend->handle) {
            video_render_blend_execute(video_render, backend);
        }
    }
    return ret;
}

static int video_render_write_cb(esp_video_render_frame_t *frame, void *ctx)
{
    video_render_stream_t *stream = (video_render_stream_t *)ctx;
    video_render_t *video_render = stream->video_render;
    stream->fb.info.format = (esp_video_render_format_t)frame->format;
    stream->fb.info.width = frame->width;
    stream->fb.info.height = frame->height;
    // Keep source rect in sync with incoming frame if not configured yet.
    if (stream->src_rect.width == 0 || stream->src_rect.height == 0) {
        stream->src_rect.x = 0;
        stream->src_rect.y = 0;
        stream->src_rect.width = frame->width;
        stream->src_rect.height = frame->height;
    }
    if (stream->cached) {
        video_render_mutex_lock(video_render->compose_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
        video_render_mutex_lock(stream->mutex, VIDEO_RENDER_MAX_LOCK_TIME);
        stream->compose.is_fresh = true;
        stream->compose.is_empty = false;
        if (stream->cached_size < frame->size) {
            stream->cached_size = 0;
            video_render_free(stream->cached_data);
            stream->cached_data = video_render_malloc_align(frame->size, 64);
            if (stream->cached_data == NULL) {
                ESP_LOGE(TAG, "Fail to allocate stream cache");
                video_render_mutex_unlock(stream->mutex);
                video_render_mutex_unlock(video_render->compose_mutex);
                return ESP_VIDEO_RENDER_ERR_NO_MEM;
            }
            stream->cached_size = frame->size;
        }
        memcpy(stream->cached_data, frame->data, frame->size);
        stream->fb.data = stream->cached_data;
        stream->fb.size = frame->size;
        video_render_mutex_unlock(stream->mutex);
        video_render_mutex_unlock(video_render->compose_mutex);
    } else {
        stream->fb.data = frame->data;
        stream->fb.size = frame->size;
    }
    int ret;
    MEASURE_BLOCK({ret = video_render_write(video_render, stream);
    }, "Render", "RenderWrite");
    return ret;
}

esp_video_render_err_t video_render_build_proc(video_render_t *video_render, video_render_stream_t *stream)
{
    stream->running = true;
    esp_video_render_frame_info_t in_info = stream->frame_info;
    esp_video_render_frame_info_t out_info = stream->frame_info;
    esp_video_render_disp_info_t disp = {};
    // TODO user first display only
    if (video_render->backend.handle) {
        video_render->backend.ops->get_display_info(video_render->backend.handle, &disp);
    }
    out_info.format = disp.format;
    out_info.width = stream->compose.disp_rect.width;
    out_info.height = stream->compose.disp_rect.height;
    if (stream->degree == 90 || stream->degree == 270) {
        esp_gmf_element_handle_t rotate_element = video_render_proc_get_element(stream->proc_hd, ESP_VIDEO_RENDER_PROC_ROTATE);
        esp_gmf_element_handle_t scale_element = video_render_proc_get_element(stream->proc_hd, ESP_VIDEO_RENDER_PROC_SCALE);
        // When use same element dst resolution is final resolution
        if (rotate_element != scale_element) {
            out_info.width = stream->compose.disp_rect.height;
            out_info.height = stream->compose.disp_rect.width;
        }
    }
    video_render_proc_close(stream->proc_hd);
    esp_video_render_err_t ret = video_render_proc_open(stream->proc_hd, &in_info, &out_info);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open proc ret %d", ret);
        stream->running = false;
        return ret;
    }
    video_render_proc_set_writer(stream->proc_hd, video_render_write_cb, stream);
    stream->need_rebuild = false;
    stream->compose.is_fresh = false;
    return ret;
}

static void video_render_stream_rate_control(video_render_stream_t *stream)
{
    // Block next write
    uint32_t cur_time = esp_timer_get_time() / 1000;
    if (stream->write_count) {
        // Suppose write can finished in half frame time, so expect write at pre_time + period - half_frame time
        uint32_t period = (stream->write_count * 1000 - 500) / stream->frame_info.fps;
        uint32_t expect_time = stream->write_start + period;
        uint32_t tolerance = 3 * 1000 / stream->frame_info.fps;  // 3 frame tolerance
        if (cur_time < expect_time) {
            int delay_time = expect_time - cur_time;
            int limit = 1000 / stream->frame_info.fps;
            if (delay_time > limit) {
                delay_time = limit;
            }
            video_render_delay(delay_time);
        }
        if (cur_time > expect_time + tolerance) {
            ESP_LOGW(TAG, "Write too slow reset rate control");
            stream->write_count = 0;
            stream->write_start = cur_time;
        }
    } else {
        stream->write_start = esp_timer_get_time() / 1000;
    }
    stream->write_count++;
}

esp_video_render_err_t esp_video_render_stream_write_fb(esp_video_render_stream_handle_t handle,
                                                        esp_video_render_fb_t *fb)
{
    if (handle == NULL || fb == NULL || fb->data == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    video_render_t *video_render = stream->video_render;
    if (video_render_has_backend(video_render) == false) {
        ESP_LOGE(TAG, "Not support open when backend not set yet");
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    if (stream->running == false) {
        stream->running = true;
    }
    if (stream->frame_info.fps) {
        video_render_stream_rate_control(stream);
    }
    video_render_mutex_lock(stream->mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    stream->using_fb = true;
    stream->fb = *fb;
    // No need further processing
    ret = video_render_write(video_render, stream);
    video_render_mutex_unlock(stream->mutex);
    return ret;
}

esp_video_render_err_t esp_video_render_stream_lock(esp_video_render_stream_handle_t handle)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    int ret = video_render_mutex_lock(stream->mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    return ret ? ESP_VIDEO_RENDER_ERR_OK : ESP_VIDEO_RENDER_ERR_FAIL;
}

esp_video_render_err_t esp_video_render_stream_compose_lock(esp_video_render_stream_handle_t handle)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    int ret = video_render_mutex_lock(stream->video_render->compose_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    return ret ? ESP_VIDEO_RENDER_ERR_OK : ESP_VIDEO_RENDER_ERR_FAIL;
}

esp_video_render_err_t esp_video_render_stream_compose_unlock(esp_video_render_stream_handle_t handle)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    int ret = video_render_mutex_unlock(stream->video_render->compose_mutex);
    return ret ? ESP_VIDEO_RENDER_ERR_OK : ESP_VIDEO_RENDER_ERR_FAIL;
}

esp_video_render_err_t esp_video_render_stream_write(esp_video_render_stream_handle_t handle,
                                                     esp_video_render_frame_t *frame)
{
    if (handle == NULL || frame == NULL || frame->data == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    video_render_t *video_render = stream->video_render;
    if (video_render_has_backend(video_render) == false) {
        ESP_LOGE(TAG, "Not support open when backend not set yet");
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }

    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    if (stream->running == false || stream->need_rebuild) {
        ret = video_render_build_proc(video_render, stream);
        ESP_LOGI(TAG, "Rebuild proc ret %d", ret);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            return ret;
        }
    }
    if (stream->frame_info.fps) {
        video_render_stream_rate_control(stream);
    }
    // When not support cache lock for process also, otherwise only lock when copy to cache
    if (stream->cached == false) {
        video_render_mutex_lock(stream->mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    }
    stream->using_fb = false;
    MEASURE_BLOCK({ret = video_render_proc_write(stream->proc_hd, frame);
    }, "Render", "StreamWrite");
    if (stream->cached == false) {
        video_render_mutex_unlock(stream->mutex);
    }
    return ret;
}

esp_video_render_err_t esp_video_render_stream_unlock(esp_video_render_stream_handle_t handle)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    video_render_mutex_unlock(stream->mutex);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_stream_close(esp_video_render_stream_handle_t handle)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_stream_t *stream = (video_render_stream_t *)handle;
    video_render_t *video_render = stream->video_render;
    // Remove from stream list firstly
    if (remove_stream(video_render, stream) == false) {
        return ESP_VIDEO_RENDER_ERR_NOT_FOUND;
    }

    // Lock for stream
    if (stream->mutex) {
        video_render_mutex_lock(stream->mutex, VIDEO_RENDER_MAX_LOCK_TIME);
        stream->running = false;
        video_render_mutex_unlock(stream->mutex);
    }
    if (stream->overlay) {
        esp_vui_overlay_destroy(stream->overlay);
        stream->overlay = NULL;
    }
    // Free resources
    if (stream->proc_hd) {
        video_render_proc_close(stream->proc_hd);
        video_render_proc_destroy(stream->proc_hd);
        stream->proc_hd = NULL;
    }
    if (stream->mutex) {
        video_render_mutex_destroy(stream->mutex);
        stream->mutex = NULL;
    }
    if (stream->cached_data) {
        video_render_free(stream->cached_data);
        stream->cached_data = NULL;
    }
    video_render_free(stream);
    bool wait_for_blend = false;
    video_render_mutex_lock(video_render->render_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    if (video_render->running && video_render->active_stream_num == 0) {
        video_render->running = false;
        wait_for_blend = true;
    }
    video_render_mutex_unlock(video_render->render_mutex);
    if (wait_for_blend) {
        video_render_event_grp_wait_bits(video_render->event_grp, VIDEO_RENDER_EXIT_BIT, VIDEO_RENDER_MAX_LOCK_TIME);
        if (video_render->event_cb) {
            video_render->event_cb(ESP_VIDEO_RENDER_EVENT_TYPE_CLOSED, video_render->event_ctx);
        }
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_destroy(esp_video_render_handle_t render)
{
    if (render == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_t *video_render = (video_render_t *)render;
    bool running = video_render->running;
    if (video_render->render_mutex) {
        video_render_mutex_lock(video_render->render_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
        video_render->running = false;
        video_render_mutex_unlock(video_render->render_mutex);
    }
    // Close all stream
    while (video_render->stream_list) {
        esp_video_render_stream_close((esp_video_render_stream_handle_t)video_render->stream_list);
    }
    // Wait for exited
    if (running && video_render->event_grp) {
        video_render_event_grp_wait_bits(video_render->event_grp, VIDEO_RENDER_EXIT_BIT, VIDEO_RENDER_MAX_LOCK_TIME);
    }
    // Free display
    video_render_backend_t *backend = &video_render->backend;
    if (backend->handle) {
        video_render_free_background(backend);
        backend->ops->deinit(backend->handle);
        backend->handle = NULL;
    }
    video_render_fb_info_t *cur_fb = backend->fb_info;
    while (cur_fb) {
        video_render_fb_info_t *next = cur_fb->next;
        video_render_free(cur_fb);
        cur_fb = next;
    }
    backend->fb_info = NULL;
    backend->prev_fb = NULL;
    backend->cur_fb = NULL;
    if (video_render->blender) {
        esp_video_render_blend_close(video_render->blender);
        video_render->blender = NULL;
    }
    if (video_render->event_grp) {
        video_render_event_grp_destroy(video_render->event_grp);
        video_render->event_grp = NULL;
    }
    if (video_render->render_mutex) {
        video_render_mutex_destroy(video_render->render_mutex);
        video_render->render_mutex = NULL;
    }
    if (video_render->compose_mutex) {
        video_render_mutex_destroy(video_render->compose_mutex);
        video_render->compose_mutex = NULL;
    }
    video_render_free(video_render);
    return ESP_VIDEO_RENDER_ERR_OK;
}

void esp_video_render_measure_enable(bool enable)
{
#ifdef CONFIG_VIDEO_RENDER_MEASURE_ENABLE
    video_render_measure_enable(enable);
#endif  /* CONFIG_VIDEO_RENDER_MEASURE_ENABLE */
}
