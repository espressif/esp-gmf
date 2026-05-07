/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_vui_overlay.h"
#include "esp_vui_container.h"
#include "video_render_sys.h"
#include "video_render_compose.h"
#include "video_render_utils.h"
#include "esp_video_render.h"
#include "esp_log.h"
#include "esp_video_render_log.h"

#define TAG  "VIDEO_OVERLAY"

typedef struct esp_video_render_overlay {
    video_render_compose_t  compose;  // Specially used when region is removed
    bool                    is_dirty;
    esp_vui_overlay_rgn_t  *head;
    esp_vui_overlay_rgn_t  *tail;
    void                   *stream;  // opaque stream owner
    void                   *render;  // render handle
} esp_vui_overlay_t;

esp_video_render_err_t esp_vui_overlay_create(esp_vui_overlay_cfg_t *cfg, esp_vui_overlay_handle_t *overlay)
{
    if (cfg == NULL || overlay == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)video_render_calloc(1, sizeof(esp_vui_overlay_t));
    if (ov == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    ov->stream = cfg->stream;
    ov->render = cfg->render;
    *overlay = (esp_vui_overlay_handle_t)ov;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_overlay_compose_lock(esp_vui_overlay_handle_t overlay)
{
    if (overlay == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    return esp_video_render_stream_compose_lock(ov->stream);
}

esp_video_render_err_t esp_vui_overlay_compose_unlock(esp_vui_overlay_handle_t overlay)
{
    if (overlay == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    return esp_video_render_stream_compose_unlock(ov->stream);
}

esp_video_render_err_t esp_vui_overlay_mark_dirty(esp_vui_overlay_handle_t overlay)
{
    if (overlay == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    ov->is_dirty = true;
    return ESP_VIDEO_RENDER_ERR_OK;
}

bool esp_vui_overlay_is_dirty(esp_vui_overlay_handle_t overlay)
{
    if (overlay == NULL) {
        return false;
    }
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    return ov->is_dirty || ov->compose.dirty_count;
}

esp_video_render_dirty_rect_t *esp_vui_overlay_get_dirty_rects(esp_vui_overlay_handle_t overlay, uint8_t *dirty_count)
{
    if (overlay == NULL || dirty_count == NULL) {
        return NULL;
    }
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    *dirty_count = ov->compose.dirty_count;
    return ov->compose.dirty_area;
}

esp_video_render_err_t esp_vui_overlay_add_region(esp_vui_overlay_handle_t overlay, esp_vui_overlay_rgn_t *region)
{
    if (overlay == NULL || region == NULL || region->frame.width == 0 || region->frame.height == 0) {
        ESP_LOGE(TAG, "Bad region not support to add");
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_overlay_compose_lock(overlay);
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    if (region->compose.disp_rect.width == 0) {
        region->compose.disp_rect.width = region->frame.width;
    }
    if (region->compose.disp_rect.height == 0) {
        region->compose.disp_rect.height = region->frame.height;
    }
    region->compose.is_fresh = true;
    // For safety force clear next
    region->next = NULL;
    if (ov->head == NULL) {
        ov->head = ov->tail = region;
    } else {
        ov->tail->next = region;
        ov->tail = region;
    }
    ESP_LOGI(TAG, "add_region: overlay=%p rgn=%p frame %dx%d fmt=%u disp %d-%d %dx%d vis=%d alpha=%u",
             (void *)ov, (void *)region, region->frame.width, region->frame.height, region->frame.format,
             region->compose.disp_rect.x, region->compose.disp_rect.y, region->compose.disp_rect.width, region->compose.disp_rect.height,
             region->compose.visible, region->compose.alpha);
    ov->is_dirty = true;
    esp_vui_overlay_compose_unlock(overlay);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_overlay_remove_region(esp_vui_overlay_handle_t overlay, esp_vui_overlay_rgn_t *region)
{
    if (overlay == NULL || region == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    esp_vui_overlay_rgn_t *prev = NULL;
    esp_vui_overlay_compose_lock(overlay);
    esp_vui_overlay_rgn_t *cur = ov->head;
    while (cur) {
        if (cur == region) {
            // Change dirty count
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Cur dirty %d add %d-%d %dx%d\n",
                                ov->compose.dirty_count,
                                region->compose.disp_rect.x, region->compose.disp_rect.y,
                                region->compose.disp_rect.width, region->compose.disp_rect.height);
            ov->compose.dirty_count = merge_compose_dirty_rect(&ov->compose, &region->compose.disp_rect, false);
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Overlay remove dirty:\n");
            for (int i = 0; i < ov->compose.dirty_count; ++i) {
                video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, " %d: %d-%d %dx%d opaque=%d\n",
                                    i, ov->compose.dirty_area[i].rect.x, ov->compose.dirty_area[i].rect.y,
                                    ov->compose.dirty_area[i].rect.width, ov->compose.dirty_area[i].rect.height,
                                    ov->compose.dirty_area[i].opaque);
            }
            if (prev) {
                prev->next = cur->next;
            } else {
                ov->head = cur->next;
            }
            if (ov->tail == cur) {
                ov->tail = prev;
            }
            ESP_LOGI(TAG, "remove_region: overlay=%p rgn=%p", (void *)ov, (void *)region);
            esp_vui_overlay_compose_unlock(overlay);
            return ESP_VIDEO_RENDER_ERR_OK;
        }
        prev = cur;
        cur = cur->next;
    }
    esp_vui_overlay_compose_unlock(overlay);
    return ESP_VIDEO_RENDER_ERR_NOT_FOUND;
}

esp_video_render_err_t esp_vui_overlay_add_container(esp_vui_overlay_handle_t overlay, esp_vui_container_handle_t container)
{
    if (overlay == NULL || container == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    return esp_vui_overlay_add_region(overlay, (esp_vui_overlay_rgn_t *)container);
}

esp_video_render_err_t esp_vui_overlay_remove_container(esp_vui_overlay_handle_t overlay, esp_vui_container_handle_t container)
{
    if (overlay == NULL || container == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    return esp_vui_overlay_remove_region(overlay, (esp_vui_overlay_rgn_t *)container);
}

esp_vui_overlay_rgn_t *esp_vui_overlay_get_region(esp_vui_overlay_handle_t overlay)
{
    if (overlay == NULL) {
        return NULL;
    }
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    return ov->head;
}

esp_video_render_err_t esp_vui_overlay_update_region(esp_vui_overlay_handle_t overlay,
                                                     esp_vui_overlay_rgn_t *region,
                                                     esp_video_render_rect_t *new_disp_rect)
{
    if (overlay == NULL || region == NULL || new_disp_rect == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_overlay_compose_lock(overlay);
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;

    // Save old position before updating - this ensures old position is marked dirty
    esp_video_render_rect_t old_rect = region->compose.disp_rect;

    // Check if region has actually moved (position or size changed)
    bool has_moved = (old_rect.x != new_disp_rect->x || old_rect.y != new_disp_rect->y || old_rect.width != new_disp_rect->width || old_rect.height != new_disp_rect->height);

    // If region has moved and had a valid previous position, immediately add it to overlay's dirty area
    // This prevents multiple update_region calls from overwriting prev_rect before blend task processes it
    if (has_moved && old_rect.width > 0 && old_rect.height > 0) {
        // Immediately merge old position into overlay's dirty area (treat as removed region)
        ov->compose.dirty_count = merge_compose_dirty_rect(&ov->compose, &old_rect, false);
        ESP_LOGI(TAG, "update_region: moved from %d-%d %dx%d to %d-%d %dx%d, added to overlay dirty (count=%d)",
                 old_rect.x, old_rect.y, old_rect.width, old_rect.height,
                 new_disp_rect->x, new_disp_rect->y, new_disp_rect->width, new_disp_rect->height,
                 ov->compose.dirty_count);
    } else if (has_moved) {
        ESP_LOGW(TAG, "update_region: moved but old_rect invalid (%d-%d %dx%d), not adding to dirty",
                 old_rect.x, old_rect.y, old_rect.width, old_rect.height);
    }

    // Update compose rectangles and mark fresh
    region->compose.disp_rect = *new_disp_rect;
    region->compose.is_fresh = true;
    ov->is_dirty = true;
    esp_vui_overlay_compose_unlock(overlay);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_overlay_redraw(esp_vui_overlay_handle_t overlay,
                                              const esp_video_render_dirty_rect_t *dirty_region,
                                              uint8_t dirty_count)
{
    if (overlay == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    // For now, overlay refresh simply clears freshness after a frame push
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    esp_vui_overlay_rgn_t *iter = ov->head;
    while (iter) {
        if (iter->compose.visible) {
            if (iter->refresh) {
                iter->refresh(iter, dirty_region, dirty_count, iter->user);
            }
        }
        iter = iter->next;
    }
    ov->compose.dirty_count = 0;
    ov->is_dirty = false;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_overlay_destroy(esp_vui_overlay_handle_t overlay)
{
    if (overlay == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    esp_vui_overlay_rgn_t *cur = ov->head;
    ov->head = NULL;
    ov->tail = NULL;
    while (cur) {
        esp_vui_overlay_rgn_t *next = cur->next;
        cur->next = NULL;
        // TODO currently only container has refresh function
        if (cur->refresh) {
            esp_vui_container_destroy((esp_vui_container_handle_t)cur);
        }
        cur = next;
    }
    // Region is maintainer by user
    video_render_free(ov);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_overlay_get_blender(esp_vui_overlay_handle_t overlay,
                                                   esp_video_render_blend_handle_t *blender)
{
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    if (ov == NULL || blender == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    *blender = NULL;
    if (ov->render == NULL) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    return esp_video_render_get_blender(ov->render, blender);
}

esp_video_render_err_t esp_vui_overlay_get_pool(esp_vui_overlay_handle_t overlay, void **pool)
{
    esp_vui_overlay_t *ov = (esp_vui_overlay_t *)overlay;
    if (ov == NULL || pool == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    *pool = NULL;
    if (ov->render == NULL) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    return esp_video_render_get_pool(ov->render, pool);
}
