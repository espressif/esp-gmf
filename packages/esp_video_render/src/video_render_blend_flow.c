/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <string.h>
#include "esp_timer.h"
#include "video_render_blend_flow.h"
#include "video_render_sys.h"
#include "video_render_utils.h"
#include "esp_video_render_blender.h"
#include "video_render_compose.h"
#include "esp_video_render_log.h"
#include "video_render_measure.h"
#include "esp_log.h"

#define TAG                            "VIDEO_RENDER"
#define VIDEO_RENDER_FB_DONE_BIT       (1 << 0)
#define VIDEO_RENDER_EXIT_BIT          (1 << 1)
#define VIDEO_RENDER_MAX_DIRTY_REGION  (16)
#define VIDEO_RENDER_DEFAULT_FPS       (20)
#define RENDER_COUNT_REACH_RESET       (100000)

void video_render_set_dirty_monitor_cb(video_render_t *video_render, video_render_dirty_monitor_cb cb)
{
    video_render->dirty_monitor_cb = cb;
}

static video_render_fb_info_t *video_render_check_fb_info(video_render_backend_t *backend, uint8_t *fb_buffer)
{
    bool found = false;
    video_render_fb_info_t *iter = backend->fb_info;
    while (iter) {
        if (iter->fb_buffer == fb_buffer) {
            found = true;
            break;
        }
        iter = iter->next;
    }
    if (found) {
        return iter;
    }
    video_render_fb_info_t *cur = video_render_calloc(1, sizeof(video_render_fb_info_t));
    if (cur == NULL) {
        return NULL;
    }
    cur->fb_buffer = fb_buffer;
    if (backend->fb_info == NULL) {
        backend->fb_info = cur;
    } else {
        cur->next = backend->fb_info;
        backend->fb_info = cur;
    }
    return cur;
}

static bool has_fresh_stream(video_render_t *video_render)
{
    video_render_stream_t *stream = video_render->stream_list;
    while (stream) {
        if (stream->compose.is_fresh || stream->compose.dirty_count > 0 || (stream->overlay && esp_vui_overlay_is_dirty(stream->overlay))) {
            return true;
        }
        stream = stream->next;
    }
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "No fresh stream currently\n");
    return false;
}

static video_render_compose_t *video_render_next_stream_compose(video_render_compose_t *cur)
{
    video_render_stream_t *stream = (video_render_stream_t *)cur;
    video_render_stream_t *next = stream->next;
    while (next && next->running == false) {
        next = next->next;
    }
    return next ? &next->compose : NULL;
}

static video_render_compose_t *video_render_overlay_next_compose(video_render_compose_t *cur)
{
    esp_vui_overlay_rgn_t *overlay_region = (esp_vui_overlay_rgn_t *)cur;
    return overlay_region->next ? &overlay_region->next->compose : NULL;
}

static void get_initial_dirty_info(video_render_t *video_render, video_render_backend_t *backend)
{
    video_render_fb_info_t *fb_info = backend->cur_fb;
    fb_info->dirty_count = 0;
    fb_info->redraw_all = false;
    do {
        // Background not update yet
        if ((backend->is_bg_set && fb_info->is_bg_filled == false) ||
            (backend->is_bg_set == false && fb_info->is_bg_filled)) {
            fb_info->redraw_all = true;
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Redraw all for background not filled %p\n", fb_info);
            break;
        }
        // Only support one frame buffer case, no need copy last dirty regions
        if (backend->prev_fb && backend->prev_fb != fb_info) {
            if (backend->prev_fb->dirty_count) {
                // Even last frame is full paint we still need recalculate dirty region
                int size = backend->prev_fb->dirty_count * sizeof(esp_video_render_dirty_rect_t);
                memcpy(fb_info->dirty_region, backend->prev_fb->dirty_region, size);
                fb_info->dirty_count = backend->prev_fb->dirty_count;
            }
        }
    } while (0);
    // When full-redraw update all regions
    if (fb_info->redraw_all) {
        fb_info->dirty_count = 1;
        fb_info->dirty_region[0].rect.x = fb_info->dirty_region[0].rect.y = 0;
        fb_info->dirty_region[0].rect.width = backend->fb.info.width;
        fb_info->dirty_region[0].rect.height = backend->fb.info.height;
        fb_info->dirty_region[0].opaque = false;
    }
}

static uint8_t video_render_calc_new_dirty(video_render_t *video_render,
                                           video_render_backend_t *backend,
                                           esp_video_render_dirty_rect_t *final_rect,
                                           uint8_t limit)
{
    video_render_fb_info_t *fb_info = backend->cur_fb;
    uint8_t filled = 0;
    if (fb_info->redraw_all) {
        // TODO when have one display no need calc it
        for (video_render_stream_t *stream = video_render->stream_list; stream; stream = stream->next) {
            if (stream->running == false || stream->compose.visible == false || stream->compose.alpha == 0) {
                continue;
            }
            filled = merge_dirty_rect(final_rect, filled, limit,
                                      &stream->compose.disp_rect,
                                      (stream->compose.alpha == 255 && stream->compose.is_trans_color == false));
        }
        return filled;
    }

    if ((backend->is_bg_set && fb_info->is_bg_filled == false)) {
        // Mark all stream as refresh
        for (video_render_stream_t *stream = video_render->stream_list; stream; stream = stream->next) {
            if (stream->running == false) {
                continue;
            }
            stream->compose.is_fresh = true;
        }
    }
    for (video_render_stream_t *stream = video_render->stream_list; stream; stream = stream->next) {
        // Skip invisible stream
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Stream running %d visible %d\n", stream->running, stream->compose.visible);
        if (stream->running == false || stream->compose.visible == false) {
            continue;
        }
        if (stream->compose.is_fresh) {
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "isfresh\n");
            // Make all overlay as fresh
            if (stream->overlay) {
                esp_vui_overlay_rgn_t *iter = esp_vui_overlay_get_region(stream->overlay);
                while (iter) {
                    video_render_compose_t *c = &iter->compose;
                    c->is_fresh = true;
                    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "force region fresh %p\n", iter);
                    iter = iter->next;
                }
            }
        }
        // Refresh for overlay
        if (stream->overlay) {
            uint8_t overlay_dirty = 0;
            // Merge overlay removed region into dirty region firstly
            esp_video_render_dirty_rect_t *rect = esp_vui_overlay_get_dirty_rects(stream->overlay, &overlay_dirty);
            for (int i = 0; i < overlay_dirty; i++) {
                filled = merge_dirty_rect(final_rect, filled, limit, &rect[i].rect, rect[i].opaque);
            }
            // Update dirty area for overlay firstly
            esp_vui_overlay_rgn_t *iter = esp_vui_overlay_get_region(stream->overlay);
            if (iter) {
                filled = video_compose_calc_dirty_area(&iter->compose,
                                                       video_render_overlay_next_compose,
                                                       final_rect, filled, limit);
            }
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Overlay calcdirty %d\n", filled);
            for (int i = 0; i < filled; i++) {
                video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "  overlay %d: %d-%d %dx%d opaque=%d\n", i,
                                    final_rect[i].rect.x, final_rect[i].rect.y,
                                    final_rect[i].rect.width, final_rect[i].rect.height,
                                    final_rect[i].opaque);
            }
        }
    }
    // Update dirty area for stream
    if (video_render->stream_list) {
        filled = video_compose_calc_dirty_area(&video_render->stream_list->compose,
                                               video_render_next_stream_compose,
                                               final_rect, filled, limit);
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Stream calc dirty %d\n", filled);
        for (int i = 0; i < filled; i++) {
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "  final %d: %d-%d %dx%d opaque=%d\n", i,
                                final_rect[i].rect.x, final_rect[i].rect.y,
                                final_rect[i].rect.width, final_rect[i].rect.height,
                                final_rect[i].opaque);
        }
    }
    return filled;
}

static bool dirty_need_background_fill(const esp_video_render_dirty_rect_t *dirty_region, uint8_t dirty_count)
{
    for (int i = 0; i < dirty_count; ++i) {
        if (dirty_region[i].opaque == false) {
            return true;
        }
    }
    return false;
}

static inline bool fill_background(video_render_t *video_render, video_render_backend_t *backend)
{
    video_render_fb_info_t *fb_info = backend->cur_fb;
    if (backend->is_bg_set == false) {
        if (fb_info->is_bg_filled) {
            fb_info->is_bg_filled = false;
        }
        return false;
    }
    esp_video_render_rect_t full = {
        .x = 0,
        .y = 0,
        .width = backend->fb.info.width,
        .height = backend->fb.info.height,
    };
    if (fb_info->is_bg_filled == false) {
        fb_info->is_bg_filled = true;
        if (backend->bg_fb.data && backend->bg_fb.size == backend->fb.size) {
            esp_video_render_rect_t bg_full = {
                .x = 0,
                .y = 0,
                .width = backend->bg_fb.info.width,
                .height = backend->bg_fb.info.height,
            };
            esp_video_render_blend_bitblt(video_render->blender, &backend->fb, &backend->bg_fb, &full, &bg_full);
        } else {
            esp_video_render_blend_fill(video_render->blender, &backend->fb, &full, &backend->bg_color);
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "fill all background\n");
        }
        return true;
    }
    if (!dirty_need_background_fill(fb_info->dirty_region, fb_info->dirty_count)) {
        return false;
    }
    if (backend->bg_fb.data) {
        bool filled = false;
        for (int i = 0; i < fb_info->dirty_count; i++) {
            if (fb_info->dirty_region[i].opaque) {
                continue;
            }
            esp_video_render_rect_t *r = &fb_info->dirty_region[i].rect;
            esp_video_render_blend_process(video_render->blender, &backend->fb, &backend->bg_fb, r, r, 255);
            filled = true;
        }
        return filled;
    } else {
        bool filled = false;
        for (int i = 0; i < fb_info->dirty_count; i++) {
            if (fb_info->dirty_region[i].opaque) {
                continue;
            }
            esp_video_render_rect_t *r = &fb_info->dirty_region[i].rect;
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Refill background %d-%d %dx%d\n",
                                r->x, r->y, r->width, r->height);
            esp_video_render_blend_fill(video_render->blender, &backend->fb, r, &backend->bg_color);
            filled = true;
        }
        return filled;
    }
}

static void compose_finished(video_render_compose_t *compose)
{
    compose->dirty_count = 0;
    compose->is_fresh = false;
    compose->is_visible = compose->visible;
}

static bool stream_has_dirty_intersection(const video_render_stream_t *stream, const video_render_fb_info_t *fb_info)
{
    for (int i = 0; i < fb_info->dirty_count; i++) {
        esp_video_render_rect_t intersect;
        if (rect_intersect(&stream->compose.disp_rect, &fb_info->dirty_region[i].rect, &intersect, NULL)) {
            return true;
        }
    }
    return false;
}

static bool stream_need_blend_now(const video_render_stream_t *stream, const video_render_fb_info_t *fb_info)
{
    if (fb_info->redraw_all || stream->compose.is_fresh) {
        return true;
    }
    // When overlaying directly on stream data, multiple redraws are allowed.
    // This may cause a blinking effect if the overlay contains transparency.
    // We intentionally do not prevent this behavior (using a separate overlay stream can resolve the issue).
    // Note: If the overlay is fully opaque or has unchanging transparent regions,
    //       mismatched FPS between the overlay and stream still allowed.
    if (stream->overlay && esp_vui_overlay_is_dirty(stream->overlay)) {
        return true;
    }
    return stream_has_dirty_intersection(stream, fb_info);
}

static esp_video_render_err_t blend_overlay_region(video_render_t *video_render, video_render_backend_t *backend, video_render_stream_t *stream)
{
    if (stream->overlay == NULL || stream->compose.visible == false) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    video_render_fb_info_t *fb_info = backend->cur_fb;
    // Redraw of overlay
    esp_vui_overlay_rgn_t *iter = esp_vui_overlay_get_region(stream->overlay);
    for (; iter; iter = iter->next) {
        iter->compose.opaque = false;
        // Set frame buffer to overlay region so that widget can draw onto it directly
        if (iter->frame.data == NULL) {
            iter->fb = stream->fb.data ? &stream->fb : &backend->fb;
            continue;
        }
    }
    MEASURE_BEGIN("Render", "OverlayRedraw");
    if (fb_info->redraw_all) {
        esp_video_render_dirty_rect_t dirty_region = {
            .rect = stream->compose.disp_rect,
            .opaque = false,
        };
        ret = esp_vui_overlay_redraw(stream->overlay, &dirty_region, 1);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Fail to refresh overlay ret %d", ret);
        }
    } else if (fb_info->dirty_count) {
        ret = esp_vui_overlay_redraw(stream->overlay, fb_info->dirty_region, fb_info->dirty_count);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Fail to refresh overlay ret %d", ret);
        }
    }
    MEASURE_END("Render", "OverlayRedraw");

    esp_video_render_fb_t dst_fb = (stream->fb.data != NULL) ? stream->fb : backend->fb;
    if (fb_info->redraw_all) {
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Blend overlay redraw all\n");
    } else {
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Blend overlay partial\n");
    }
    iter = esp_vui_overlay_get_region(stream->overlay);
    video_render_compose_t *compose = &iter->compose;
    for (; iter; iter = iter->next, compose_finished(compose)) {
        compose = &iter->compose;
        if (iter->frame.data == NULL || compose->visible == false || iter->compose.alpha == 0) {
            continue;
        }
        if (fb_info->redraw_all || compose->is_fresh) {
            esp_video_render_rect_t dst_rect = compose->disp_rect;
            // If blending into stream framebuffer, convert to stream-relative coordinates
            if (stream->fb.data != NULL) {
                dst_rect.x -= stream->compose.disp_rect.x;
                dst_rect.y -= stream->compose.disp_rect.y;
            }
            esp_video_render_fb_t src_fb = {
                .info = {
                    .format = (esp_video_render_format_t)iter->frame.format,
                    .width = iter->frame.width,
                    .height = iter->frame.height,
                },
                .data = iter->frame.data,
                .size = iter->frame.size,
            };
            esp_video_render_rect_t src_rect = {
                .x = 0,
                .y = 0,
                .width = compose->disp_rect.width,
                .height = compose->disp_rect.height,
            };
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "    From: %d-%d %dx%d\n", src_rect.x, src_rect.y, src_rect.width, src_rect.height);
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "    To:   %d-%d %dx%d\n", dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height);
            MEASURE_BEGIN("Render", "OverlayBlendF");
            if (compose->is_trans_color) {
                ret = esp_video_render_blend_transparent_color(video_render->blender, &dst_fb, &src_fb, &dst_rect, &src_rect, &compose->trans_color);
            } else {
                ret = esp_video_render_blend_process(video_render->blender, &dst_fb, &src_fb, &dst_rect, &src_rect, compose->alpha);
            }
            MEASURE_END("Render", "OverlayBlendF");
            if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to blend overlay ret %d", ret);
            }
            continue;
        }
        for (int i = 0; i < fb_info->dirty_count; i++) {
            esp_video_render_rect_t intersect;
            if (rect_intersect(&compose->disp_rect, &fb_info->dirty_region[i].rect, &intersect, NULL)) {
                esp_video_render_rect_t dst_rect = intersect;
                // If blending into stream framebuffer, convert to stream-relative coordinates
                if (stream->fb.data != NULL) {
                    dst_rect.x -= stream->compose.disp_rect.x;
                    dst_rect.y -= stream->compose.disp_rect.y;
                }
                esp_video_render_fb_t src_fb = {
                    .info = {
                        .format = (esp_video_render_format_t)iter->frame.format,
                        .width = iter->frame.width,
                        .height = iter->frame.height,
                    },
                    .data = iter->frame.data,
                    .size = iter->frame.size,
                };
                esp_video_render_rect_t src_rect = {
                    .x = intersect.x - compose->disp_rect.x,
                    .y = intersect.y - compose->disp_rect.y,
                    .width = intersect.width,
                    .height = intersect.height,
                };
                video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "[%d] From: %d-%d %dx%d\n", i, src_rect.x, src_rect.y, src_rect.width, src_rect.height);
                video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "    To:   %d-%d %dx%d\n", dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height);
                MEASURE_BEGIN("Render", "OverlayBlendP");
                if (compose->is_trans_color) {
                    ret = esp_video_render_blend_transparent_color(video_render->blender, &dst_fb, &src_fb, &dst_rect, &src_rect, &compose->trans_color);
                } else {
                    ret = esp_video_render_blend_process(video_render->blender, &dst_fb, &src_fb, &dst_rect, &src_rect, compose->alpha);
                }
                MEASURE_END("Render", "OverlayBlendP");
                if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                    ESP_LOGE(TAG, "Failed to blend overlay ret %d", ret);
                    continue;
                }
            }
        }
    }
    return ret;
}

// Video-only path: blend a stream without considering overlay
static esp_video_render_err_t blend_video_stream(video_render_t *video_render,
                                                 video_render_backend_t *backend,
                                                 video_render_stream_t *stream)
{
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    do {
        if (stream->fb.data == NULL || (stream->fb.data && backend->fb.data == stream->fb.data)) {
            break;
        }
        if (stream->compose.visible == false || stream->compose.alpha == 0) {
            break;
        }
        esp_video_render_fb_t fb = stream->fb;
        esp_video_render_fb_t dst_fb = backend->fb;
        // Redraw all if fresh, stream->compose.is_fresh
        if (backend->cur_fb->redraw_all) {  // Always user dirty region to avoid change to fresh after dirty calc
            esp_video_render_rect_t dst_rect = stream->compose.disp_rect;
            esp_video_render_rect_t src_rect = {
                .width = stream->compose.disp_rect.width,
                .height = stream->compose.disp_rect.height,
            };
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "blend from %d-%d %dx%d\n",
                                src_rect.x, src_rect.y, src_rect.width, src_rect.height);
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "blend to %d-%d %dx%d\n",
                                dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height);
            MEASURE_BEGIN("Render", "BlendVideoF");
            ret = esp_video_render_blend_process(video_render->blender, &dst_fb, &fb, &dst_rect, &src_rect, stream->compose.alpha);
            MEASURE_END("Render", "BlendVideoF");
            if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to blend ret %d", ret);
            }
            break;
        } else {
            // Only blend for dirty area
            video_render_fb_info_t *fb_info = backend->cur_fb;
            for (int i = 0; i < fb_info->dirty_count; i++) {
                esp_video_render_rect_t intersect;
                if (rect_intersect(&stream->compose.disp_rect, &fb_info->dirty_region[i].rect, &intersect, NULL)) {
                    esp_video_render_rect_t dst_rect = intersect;
                    esp_video_render_rect_t src_rect = {
                        .x = (intersect.x - stream->compose.disp_rect.x),
                        .y = (intersect.y - stream->compose.disp_rect.y),
                        .width = intersect.width,
                        .height = intersect.height,
                    };
                    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "blend from %d-%d %dx%d\n",
                                        src_rect.x, src_rect.y, src_rect.width, src_rect.height);
                    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "blend to %d-%d %dx%d\n",
                                        dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height);
                    MEASURE_BEGIN("Render", "BlendVideoP");
                    ret = esp_video_render_blend_process(video_render->blender, &dst_fb, &fb, &dst_rect, &src_rect, stream->compose.alpha);
                    MEASURE_END("Render", "BlendVideoP");
                    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                        ESP_LOGE(TAG, "Failed to blend ret %d", ret);
                    }
                }
            }
        }
    } while (0);
    compose_finished(&stream->compose);
    return ret;
}

static esp_video_render_err_t blend_without_gram(video_render_t *video_render, video_render_backend_t *backend)
{
    esp_video_render_err_t ret = backend->ops->get_fb(backend->handle, &backend->fb);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get fb ret %d", ret);
        return ret;
    }
    video_render_fb_info_t *fb_info = video_render_check_fb_info(backend, backend->fb.data);
    if (fb_info == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    backend->cur_fb = fb_info;
    backend->ops->lock_fb(backend->handle, &backend->fb, true);
    video_render_mutex_lock(video_render->compose_mutex, VIDEO_RENDER_MAX_LOCK_TIME);

    get_initial_dirty_info(video_render, backend);
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Initial dirty %d redraw_all %d\n", fb_info->dirty_count, fb_info->redraw_all);
    // No need refresh
    if (fb_info->dirty_count == 0 && has_fresh_stream(video_render) == false) {
        video_render_mutex_unlock(video_render->compose_mutex);
        backend->ops->lock_fb(backend->handle, &backend->fb, false);
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    esp_video_render_dirty_rect_t new_dirty_region[VIDEO_RENDER_MAX_DIRTY_REGION];
    uint8_t new_dirty_count = 0;
    // Calculate new dirty regions
    MEASURE_BEGIN("Render", "CalcNewDirty");
    new_dirty_count = video_render_calc_new_dirty(video_render, backend,
                                                  new_dirty_region,
                                                  VIDEO_RENDER_MAX_DIRTY_REGION);
    MEASURE_END("Render", "CalcNewDirty");
    if (video_render->dirty_monitor_cb && new_dirty_count > 0) {
        video_render->dirty_monitor_cb(new_dirty_region, new_dirty_count);
    }
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Calc new dirty %d\n", new_dirty_count);
    if (fb_info->dirty_count + new_dirty_count == 0) {
        video_render_mutex_unlock(video_render->compose_mutex);
        ret = backend->ops->write_fb(backend->handle, &backend->fb, NULL, NULL);
        backend->ops->lock_fb(backend->handle, &backend->fb, false);
        backend->prev_fb = fb_info;
        return ret;
    }
    if (fb_info->dirty_count &&
        fb_info->dirty_region[0].rect.width == backend->fb.info.width &&
        fb_info->dirty_region[0].rect.height == backend->fb.info.height) {
        fb_info->redraw_all = true;
    }
    if (fb_info->redraw_all == false && new_dirty_count > 0) {
        for (int i = 0; i < new_dirty_count; i++) {
            fb_info->dirty_count = merge_dirty_rect(fb_info->dirty_region,
                                                    fb_info->dirty_count,
                                                    VIDEO_RENDER_MAX_DIRTY_REGION,
                                                    &new_dirty_region[i].rect,
                                                    new_dirty_region[i].opaque);
        }
        for (int i = 0; i < fb_info->dirty_count; i++) {
            esp_video_render_rect_t *rect = &fb_info->dirty_region[i].rect;
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "  Merged %d: %d-%d %dx%d opaque=%d\n", i, rect->x, rect->y,
                                rect->width, rect->height, fb_info->dirty_region[i].opaque);
        }
    }
    // Fill background firstly
    fill_background(video_render, backend);
    for (video_render_stream_t *stream = video_render->stream_list; stream; stream = stream->next) {
        if (stream->running == false || stream->compose.visible == false) {
            compose_finished(&stream->compose);
            continue;
        }
        if (!stream_need_blend_now(stream, fb_info)) {
            compose_finished(&stream->compose);
            continue;
        }
        MEASURE_BEGIN("Render", "LockStream");
        video_render_mutex_lock(stream->mutex, VIDEO_RENDER_MAX_LOCK_TIME);
        MEASURE_END("Render", "LockStream");
        // Blend overlay into stream video firstly
        MEASURE_BEGIN("Render", "BlendOverlay");
        blend_overlay_region(video_render, backend, stream);
        MEASURE_END("Render", "BlendOverlay");

        // Blend video stream into final display
        MEASURE_BEGIN("Render", "BlendVideo");
        blend_video_stream(video_render, backend, stream);
        MEASURE_END("Render", "BlendVideo");
        video_render_mutex_unlock(stream->mutex);
    }
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "###################################\n");
    video_render_mutex_unlock(video_render->compose_mutex);
    ret = backend->ops->write_fb(backend->handle, &backend->fb, NULL, NULL);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to write fb ret %d", ret);
    }
    // TODO reuse fb and write to other display then unlock fb??
    backend->ops->lock_fb(backend->handle, &backend->fb, false);
    fb_info->dirty_count = new_dirty_count;
    if (new_dirty_count > 0) {
        memcpy(fb_info->dirty_region, new_dirty_region, new_dirty_count * sizeof(esp_video_render_dirty_rect_t));
    }
    backend->prev_fb = fb_info;  // Set previous fb information
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "%p bgfill %d last dirty %d\n", fb_info, fb_info->is_bg_filled, fb_info->dirty_count);
    return ret;
}

static esp_video_render_err_t blend_with_gram_video_only(video_render_t *video_render, video_render_backend_t *backend)
{
    video_render_stream_t *stream = video_render->stream_list;
    backend->fb = stream->fb;
    // Allow UI mixed when video data is coming
    video_render_fb_info_t *fb_info = video_render_check_fb_info(backend, backend->fb.data);
    if (fb_info == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }

    int ret = ESP_VIDEO_RENDER_ERR_OK;
    video_render_mutex_lock(video_render->compose_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    bool using_fb = stream->using_fb;
    if (using_fb) {
        // When render using frame buffer will use frame buffer lock
        backend->ops->lock_fb(backend->handle, &backend->fb, true);
    }
    video_render_mutex_lock(stream->mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    bool overlay_dirty = (stream->overlay && esp_vui_overlay_is_dirty(stream->overlay));
    bool need_write = stream->compose.is_fresh || overlay_dirty;
    if (!need_write) {
        video_render_mutex_unlock(stream->mutex);
        if (using_fb) {
            backend->ops->lock_fb(backend->handle, &backend->fb, false);
        }
        video_render_mutex_unlock(video_render->compose_mutex);
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    // No need to calc dirty region for when write new frame, always dirty
    if (stream->overlay) {
        esp_vui_overlay_rgn_t *iter = esp_vui_overlay_get_region(stream->overlay);
        for (; iter; iter = iter->next) {
            iter->compose.opaque = false;
            // Set frame buffer to overlay region so that widget can draw onto it directly
            if (iter->frame.data == NULL) {
                iter->fb = &backend->fb;
                continue;
            }
        }
        MEASURE_BEGIN("Render", "OverlayRedraw");
        // Redraw overlay
        esp_video_render_dirty_rect_t dirty_region = {
            .rect = stream->compose.disp_rect,
            .opaque = false,
        };
        ret = esp_vui_overlay_redraw(stream->overlay, &dirty_region, 1);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Fail to refresh overlay ret %d", ret);
        }
        MEASURE_END("Render", "OverlayRedraw");
        esp_video_render_fb_t dst_fb = stream->fb;
        iter = esp_vui_overlay_get_region(stream->overlay);
        video_render_compose_t *compose = iter ? &iter->compose : NULL;
        for (; iter; iter = iter->next, compose_finished(compose)) {
            compose = &iter->compose;
            if (iter->frame.data == NULL || compose->visible == false || iter->compose.alpha == 0) {
                continue;
            }
            esp_video_render_rect_t dst_rect = compose->disp_rect;
            dst_rect.x -= stream->compose.disp_rect.x;
            dst_rect.y -= stream->compose.disp_rect.y;
            esp_video_render_fb_t src_fb = {
                .info = {
                    .format = (esp_video_render_format_t)iter->frame.format,
                    .width = iter->frame.width,
                    .height = iter->frame.height,
                },
                .data = iter->frame.data,
                .size = iter->frame.size,
            };
            esp_video_render_rect_t src_rect = {
                .x = 0,
                .y = 0,
                .width = compose->disp_rect.width,
                .height = compose->disp_rect.height,
            };
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "    From: %d-%d %dx%d\n", src_rect.x, src_rect.y, src_rect.width, src_rect.height);
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "    To:   %d-%d %dx%d\n", dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height);
            MEASURE_BEGIN("Render", "OverlayBlend");
            if (compose->is_trans_color) {
                ret = esp_video_render_blend_transparent_color(video_render->blender, &dst_fb, &src_fb, &dst_rect, &src_rect, &compose->trans_color);
            } else {
                ret = esp_video_render_blend_process(video_render->blender, &dst_fb, &src_fb, &dst_rect, &src_rect, compose->alpha);
            }
            MEASURE_END("Render", "OverlayBlend");
            if (ret != ESP_VIDEO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to blend overlay ret %d", ret);
            }
        }
    }
    compose_finished(&stream->compose);
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "###################################\n");
    esp_video_render_pos_t disp_pos = {
        stream->compose.disp_rect.x,
        stream->compose.disp_rect.y,
    };
    if (using_fb) {
        video_render_mutex_unlock(stream->mutex);
    }
    video_render_mutex_unlock(video_render->compose_mutex);

    MEASURE_BEGIN("Render", "WriteFB");
    ret = backend->ops->write_fb(backend->handle, &backend->fb, NULL, &disp_pos);
    MEASURE_END("Render", "WriteFB");
    if (using_fb) {
        // When render using frame buffer will use frame buffer lock
        backend->ops->lock_fb(backend->handle, &backend->fb, false);
    } else {
        video_render_mutex_unlock(stream->mutex);
    }
    // Unlock after stream data consumed
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to write fb ret %d", ret);
    }
    return ret;
}

static bool is_blend_with_video_only(video_render_t *video_render)
{
    video_render_stream_t *stream = video_render->stream_list;
    if (stream == NULL || video_render->active_stream_num != 1 || stream->fb.data == NULL) {
        return false;
    }
    // Full display
    if (stream->compose.disp_rect.width == video_render->display_width && stream->compose.disp_rect.height == video_render->display_height) {
        return true;
    }
    return false;
}

// Optimized version for GRAM: single framebuffer, no need to track previous framebuffer info
static void get_initial_dirty_info_gram(video_render_t *video_render, video_render_backend_t *backend, video_render_fb_info_t *fb_info)
{
    fb_info->dirty_count = 0;
    fb_info->redraw_all = false;

    // Background not filled yet - need full redraw
    if ((backend->is_bg_set && fb_info->is_bg_filled == false) ||
        (backend->is_bg_set == false && fb_info->is_bg_filled)) {
        fb_info->redraw_all = true;
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Redraw all for background changed (GRAM)\n");
    }

    // When full-redraw update all regions
    if (fb_info->redraw_all) {
        fb_info->dirty_count = 1;
        fb_info->dirty_region[0].rect.x = 0;
        fb_info->dirty_region[0].rect.y = 0;
        fb_info->dirty_region[0].rect.width = backend->fb.info.width;
        fb_info->dirty_region[0].rect.height = backend->fb.info.height;
        fb_info->dirty_region[0].opaque = false;
    }
}

static esp_video_render_err_t blend_with_gram(video_render_t *video_render, video_render_backend_t *backend)
{
    if (is_blend_with_video_only(video_render)) {
        return blend_with_gram_video_only(video_render, backend);
    }
    esp_video_render_err_t ret = backend->ops->get_fb(backend->handle, &backend->fb);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get fb ret %d", ret);
        return ret;
    }
    video_render_fb_info_t *fb_info = video_render_check_fb_info(backend, backend->fb.data);
    if (fb_info == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    backend->cur_fb = fb_info;

    backend->ops->lock_fb(backend->handle, &backend->fb, true);
    video_render_mutex_lock(video_render->compose_mutex, VIDEO_RENDER_MAX_LOCK_TIME);

    get_initial_dirty_info_gram(video_render, backend, fb_info);
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Initial dirty %d redraw_all %d (GRAM)\n", fb_info->dirty_count, fb_info->redraw_all);

    // No need refresh
    if (fb_info->dirty_count == 0 && has_fresh_stream(video_render) == false) {
        video_render_mutex_unlock(video_render->compose_mutex);
        backend->ops->lock_fb(backend->handle, &backend->fb, false);
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    if (fb_info->redraw_all == false) {
        MEASURE_BEGIN("Render", "CalcNewDirty");
        fb_info->dirty_count = video_render_calc_new_dirty(video_render, backend,
                                                           fb_info->dirty_region,
                                                           VIDEO_RENDER_MAX_DIRTY_REGION);
        MEASURE_END("Render", "CalcNewDirty");
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Calc new dirty %d (GRAM)\n", fb_info->dirty_count);
        if (video_render->dirty_monitor_cb && fb_info->dirty_count > 0) {
            video_render->dirty_monitor_cb(fb_info->dirty_region, fb_info->dirty_count);
        }
    }
    if (fb_info->dirty_count == 0) {
        video_render_mutex_unlock(video_render->compose_mutex);
        backend->ops->lock_fb(backend->handle, &backend->fb, false);
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    // Check if merged region covers full screen
    if (fb_info->dirty_count == 1 &&
        fb_info->dirty_region[0].rect.width == backend->fb.info.width &&
        fb_info->dirty_region[0].rect.height == backend->fb.info.height) {
        fb_info->redraw_all = true;
        fb_info->dirty_count = 1;
    }
    for (int i = 0; i < fb_info->dirty_count; i++) {
        esp_video_render_rect_t *rect = &fb_info->dirty_region[i].rect;
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "  Merged %d: %d-%d %dx%d (GRAM)\n", i, rect->x, rect->y,
                            rect->width, rect->height);
    }

    // Fill background firstly
    MEASURE_BEGIN("Render", "FillBackground");
    fill_background(video_render, backend);
    MEASURE_END("Render", "FillBackground");

    // Blend all streams
    for (video_render_stream_t *stream = video_render->stream_list; stream; stream = stream->next) {
        if (stream->running == false || stream->compose.visible == false) {
            compose_finished(&stream->compose);
            continue;
        }
        if (!stream_need_blend_now(stream, fb_info)) {
            compose_finished(&stream->compose);
            continue;
        }
        video_render_mutex_lock(stream->mutex, VIDEO_RENDER_MAX_LOCK_TIME);
        // Blend overlay into stream video firstly
        MEASURE_BEGIN("Render", "BlendOverlay");
        blend_overlay_region(video_render, backend, stream);
        MEASURE_END("Render", "BlendOverlay");
        // Blend video stream into final display
        MEASURE_BEGIN("Render", "BlendVideo");
        blend_video_stream(video_render, backend, stream);
        MEASURE_END("Render", "BlendVideo");
        video_render_mutex_unlock(stream->mutex);
    }
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "###################################\n");
    video_render_mutex_unlock(video_render->compose_mutex);

    // Merge all dirty regions into one rect for partial update (GRAM)
    esp_video_render_rect_t merged_rect = {
        .x = 0, .y = 0, .width = backend->fb.info.width, .height = backend->fb.info.height};
    const esp_video_render_rect_t *write_rect = NULL;  // NULL => full update
    if (!fb_info->redraw_all && fb_info->dirty_count > 0) {
        merged_rect = fb_info->dirty_region[0].rect;
        for (int i = 1; i < fb_info->dirty_count; i++) {
            merged_rect = rect_union(&merged_rect, &fb_info->dirty_region[i].rect);
        }
        // Keep full width to satisfy backends that prefer full-width updates.
        merged_rect.x = 0;
        merged_rect.width = backend->fb.info.width;
        write_rect = &merged_rect;
    }
    if (write_rect) {
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Final merge rect: %d-%d %dx%d\n",
                            write_rect->x, write_rect->y, write_rect->width, write_rect->height);
    }
    ret = backend->ops->write_fb(backend->handle, &backend->fb, write_rect, NULL);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to write fb ret %d", ret);
    }
    backend->ops->lock_fb(backend->handle, &backend->fb, false);

    return ret;
}

esp_video_render_err_t video_render_blend_execute(video_render_t *video_render, video_render_backend_t *backend)
{
    if (video_render == NULL || backend == NULL || backend->ops == NULL || backend->handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (backend->ops->with_gram(backend->handle)) {
        return blend_with_gram(video_render, backend);
    }
    return blend_without_gram(video_render, backend);
}

void video_render_blend_thread(void *arg)
{
    video_render_t *video_render = (video_render_t *)arg;
    uint8_t fps = video_render->cfg.fps ? video_render->cfg.fps : VIDEO_RENDER_DEFAULT_FPS;
    uint32_t period = 1000 / fps;
    uint32_t render_count = 0;
    uint32_t start_time = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG, "Blend task is running");
    uint32_t last_print = start_time;
    while (video_render->running) {
        video_render_mutex_lock(video_render->render_mutex, VIDEO_RENDER_MAX_LOCK_TIME);
        video_render_backend_t *backend = &video_render->backend;
        video_render_blend_execute(video_render, backend);
        video_render_mutex_unlock(video_render->render_mutex);
        video_render_event_grp_set_bits(video_render->event_grp, VIDEO_RENDER_FB_DONE_BIT);
        if (video_render->event_cb) {
            video_render->event_cb(ESP_VIDEO_RENDER_EVENT_TYPE_VSYNC, video_render->event_ctx);
        }
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "....................................................\n\n");
        // Waiting for vsync
        render_count++;
        // Avoid long time running sync lost for overflow
        if (render_count >= RENDER_COUNT_REACH_RESET) {
            render_count = 0;
            start_time = esp_timer_get_time() / 1000;
        }
        uint32_t end_time = start_time + period * render_count;
        uint32_t cur_time = esp_timer_get_time() / 1000;
        if (cur_time < end_time) {
            video_render_delay(end_time - cur_time);
        } else {
            if (cur_time > end_time + period) {
                if (cur_time - last_print > 2000) {
                    ESP_LOGW(TAG, "Render too slow %" PRIu32 " vsync %" PRIu32, (cur_time - start_time), period * render_count);
                    last_print = cur_time;
                }
            }
            // Yield cpu
            video_render_delay(1);
        }
    }
    video_render_event_grp_set_bits(video_render->event_grp, VIDEO_RENDER_EXIT_BIT);
    ESP_LOGI(TAG, "Video blender Exited");
    esp_gmf_oal_thread_delete(NULL);
}
