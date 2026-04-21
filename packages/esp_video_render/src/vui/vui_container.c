/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdint.h>
#include "esp_vui_container.h"
#include "esp_vui_widget.h"
#include "video_render_sys.h"
#include "video_render_utils.h"
#include "video_render_compose.h"
#include "esp_video_render_blender.h"
#include "esp_log.h"
#include "esp_video_render_log.h"

#define TAG  "VIDEO_CONTAINER"

typedef struct widget_node {
    esp_vui_widget_t   *widget;
    struct widget_node *next;
} widget_node_t;

typedef struct esp_vui_container {
    esp_vui_overlay_rgn_t     region;
    esp_vui_overlay_handle_t  overlay;
    esp_video_render_clr_t    bg_color;
    bool                      is_bg_set;
    widget_node_t            *widgets_head;
    widget_node_t            *widgets_tail;
    bool                      has_dirty_from_widgets;
} esp_vui_container_t;

esp_video_render_err_t esp_vui_overlay_get_blender(esp_vui_overlay_handle_t overlay,
                                                   esp_video_render_blend_handle_t *blender);
esp_video_render_err_t esp_vui_overlay_get_pool(esp_vui_overlay_handle_t overlay, void **pool);

esp_video_render_err_t esp_vui_container_redraw(esp_vui_overlay_rgn_t *rgn,
                                                const esp_video_render_dirty_rect_t *dirty_region,
                                                uint8_t dirty_count,
                                                void *user);

esp_video_render_err_t esp_vui_container_create(esp_vui_overlay_handle_t overlay,
                                                esp_video_render_frame_info_t *info,
                                                esp_video_render_pos_t *pos,
                                                bool with_cache,
                                                esp_vui_container_handle_t *container)
{
    if (overlay == NULL || info == NULL || pos == NULL || container == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)video_render_calloc(1, sizeof(esp_vui_container_t));
    if (ctr == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_NO_MEM;
    do {
        ctr->region.frame.size = video_render_get_image_size(info);
        if (ctr->region.frame.size == 0) {
            ret = ESP_VIDEO_RENDER_ERR_INVALID_ARG;
            break;
        }
        if (with_cache) {
            int align_size = ALIGN_UP(ctr->region.frame.size, 64);
            ctr->region.frame.data = (uint8_t *)video_render_malloc_align(align_size, 64);
            if (ctr->region.frame.data == NULL) {
                break;
            }
            memset(ctr->region.frame.data, 0, ctr->region.frame.size);
        }
        ctr->overlay = overlay;
        ctr->region.compose.visible = true;
        ctr->region.compose.alpha = 255;
        ctr->region.compose.disp_rect.x = pos->x;
        ctr->region.compose.disp_rect.y = pos->y;
        ctr->region.frame.format = info->format;
        ctr->region.frame.width = info->width;
        ctr->region.frame.height = info->height;
        ctr->region.refresh = esp_vui_container_redraw;
        ctr->region.user = ctr;

        ret = esp_vui_overlay_add_region(ctr->overlay, &ctr->region);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            break;
        }
        ESP_LOGI(TAG, "create container=%p frame %dx%d fmt=%u pos=%d,%d with_cache=%d", (void *)ctr,
                 ctr->region.frame.width, ctr->region.frame.height, ctr->region.frame.format,
                 ctr->region.compose.disp_rect.x, ctr->region.compose.disp_rect.y, with_cache);
        *container = (esp_vui_container_handle_t)ctr;
        return ESP_VIDEO_RENDER_ERR_OK;
    } while (0);
    esp_vui_container_destroy((esp_vui_container_handle_t)ctr);
    return ret;
}

esp_video_render_err_t esp_vui_container_get_blender(esp_vui_container_handle_t container,
                                                     esp_video_render_blend_handle_t *blender)
{
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    if (ctr == NULL || blender == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    *blender = NULL;
    if (ctr->overlay == NULL) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    return esp_vui_overlay_get_blender(ctr->overlay, blender);
}

esp_video_render_err_t esp_vui_container_get_pool(esp_vui_container_handle_t container, void **pool)
{
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    if (ctr == NULL || pool == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    *pool = NULL;
    if (ctr->overlay == NULL) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    return esp_vui_overlay_get_pool(ctr->overlay, pool);
}

esp_video_render_err_t esp_vui_container_destroy(esp_vui_container_handle_t container)
{
    if (container == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    if (ctr->overlay) {
        esp_vui_overlay_remove_region(ctr->overlay, &ctr->region);
    }
    widget_node_t *node = ctr->widgets_head;
    while (node) {
        widget_node_t *next = node->next;
        if (node->widget && node->widget->ops->destroy) {
            node->widget->ops->destroy(node->widget);
        }
        video_render_free(node);
        node = next;
    }
    if (ctr->region.frame.data) {
        video_render_free(ctr->region.frame.data);
    }
    video_render_free(ctr);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_container_get_disp_rect(esp_vui_container_handle_t container, const esp_video_render_rect_t **rect)
{
    if (container == NULL || rect == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    *rect = &ctr->region.compose.disp_rect;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_container_set_transparent_color(esp_vui_container_handle_t container,
                                                               bool enable, esp_video_render_clr_t *color)
{
    if (container == NULL || (color == NULL && enable == true)) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    if (ctr->region.frame.data == NULL) {
        ESP_LOGW(TAG, "Not allowed to set transparent color for container without frame data");
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    ctr->region.compose.is_trans_color = enable;
    if (color) {
        ctr->region.compose.trans_color = *color;
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_container_set_bg_color(esp_vui_container_handle_t container, esp_video_render_clr_t *color)
{
    if (container == NULL || color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    esp_vui_container_compose_lock(container);
    ctr->bg_color = *color;
    ctr->is_bg_set = true;
    if (ctr->region.frame.data) {
        esp_video_render_blend_handle_t blender = NULL;
        esp_vui_container_get_blender(ctr, &blender);
        esp_video_render_fb_t dst_fb = {
            .info = {
                .format = ctr->region.frame.format,
                .width = ctr->region.frame.width,
                .height = ctr->region.frame.height,
            },
            .data = ctr->region.frame.data,
            .size = ctr->region.frame.size,
        };
        esp_video_render_rect_t dst_rect = {
            .x = 0,
            .y = 0,
            .width = ctr->region.frame.width,
            .height = ctr->region.frame.height,
        };
        esp_video_render_blend_fill(blender, &dst_fb, &dst_rect, &ctr->bg_color);
    }
    esp_vui_container_notify_compose_changed(container, NULL, true);
    esp_vui_container_compose_unlock(container);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_container_set_alpha(esp_vui_container_handle_t container, uint8_t alpha)
{
    if (container == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    if (ctr->region.frame.data == NULL) {
        ESP_LOGW(TAG, "Not allowed to set alpha for container without frame data");
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    esp_vui_container_compose_lock(container);
    ctr->region.compose.alpha = alpha;
    ctr->region.compose.is_fresh = true;
    esp_vui_overlay_mark_dirty(ctr->overlay);
    esp_vui_container_compose_unlock(container);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_container_add_widget(esp_vui_container_handle_t container,
                                                    esp_vui_widget_t *widget)
{
    if (container == NULL || widget == NULL || widget->ops == NULL) {
        ESP_LOGE(TAG, "add_widget: Invalid argument container:%p widget:%p ops:%p", container, widget, widget->ops);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    if (widget->rect.x + widget->rect.width > ctr->region.frame.width || widget->rect.y + widget->rect.height > ctr->region.frame.height) {
        ESP_LOGE(TAG, "Widget rect is out of container frame %d-%d %dx%d > %dx%d",
                 widget->rect.x, widget->rect.y, widget->rect.width, widget->rect.height,
                 ctr->region.frame.width, ctr->region.frame.height);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (widget->rect.width == 0 || widget->rect.height == 0) {
        ESP_LOGW(TAG, "add_widget: widget=%p has zero rect %d-%d %dx%d; did you set widget->rect?",
                 widget, widget->rect.x, widget->rect.y, widget->rect.width, widget->rect.height);
    }
    widget_node_t *node = (widget_node_t *)video_render_calloc(1, sizeof(widget_node_t));
    if (node == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    widget->container = container;
    widget->visible = true;
    node->widget = widget;
    if (ctr->widgets_tail == NULL) {
        ctr->widgets_head = ctr->widgets_tail = node;
    } else {
        ctr->widgets_tail->next = node;
        ctr->widgets_tail = node;
    }
    ESP_LOGI(TAG, "add_widget: container=%p widget=%p rect %d-%d %dx%d dirty %d-%d %dx%d visible=%d",
             (void *)ctr, (void *)widget, widget->rect.x, widget->rect.y, widget->rect.width, widget->rect.height,
             widget->dirty.x, widget->dirty.y, widget->dirty.width, widget->dirty.height, widget->visible);
    if (widget->dirty.width && widget->dirty.height) {
        esp_vui_container_compose_lock(ctr);
        esp_vui_container_notify_compose_changed(ctr, &widget->dirty, true);
        esp_vui_container_compose_unlock(ctr);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_container_remove_widget(esp_vui_container_handle_t container,
                                                       esp_vui_widget_t *widget)
{
    if (container == NULL || widget == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    widget_node_t *prev = NULL;
    widget_node_t *cur = ctr->widgets_head;
    // Update widgets on it

    while (cur) {
        if (cur->widget == widget) {
            // Mark region as dirty
            esp_vui_container_compose_lock(container);
            esp_vui_container_notify_compose_changed(container, &cur->widget->rect, false);
            if (prev) {
                prev->next = cur->next;
            } else {
                ctr->widgets_head = cur->next;
            }
            if (ctr->widgets_tail == cur) {
                ctr->widgets_tail = prev;
            }
            if (cur->widget->ops->destroy) {
                cur->widget->ops->destroy(cur->widget);
            }
            esp_vui_container_compose_unlock(container);
            video_render_free(cur);
            return ESP_VIDEO_RENDER_ERR_OK;
        }
        prev = cur;
        cur = cur->next;
    }
    return ESP_VIDEO_RENDER_ERR_NOT_FOUND;
}

esp_video_render_err_t esp_vui_container_set_visible(esp_vui_container_handle_t container,
                                                     bool visible)
{
    if (container == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    if (ctr->region.compose.visible == visible) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    esp_vui_container_compose_lock(container);
    ctr->region.compose.visible = visible;
    if (visible == false) {
        ctr->region.compose.prev_rect = ctr->region.compose.disp_rect;
    } else {
        ctr->region.compose.is_fresh = true;
    }
    esp_vui_overlay_mark_dirty(ctr->overlay);
    esp_vui_container_compose_unlock(container);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t redraw_to_cache(esp_vui_container_t *ctr)
{
    if (ctr->region.frame.data == NULL) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    // No need redraw if no dirty from widgets and region is fresh
    if (ctr->has_dirty_from_widgets == false && ctr->region.compose.is_fresh == false) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    esp_video_render_fb_t dst_fb = {
        .info = {
            .format = ctr->region.frame.format,
            .width = ctr->region.frame.width,
            .height = ctr->region.frame.height,
        },
        .data = ctr->region.frame.data,
        .size = ctr->region.frame.size,
    };
    bool is_fresh = ctr->region.compose.is_fresh;
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "redraw (cache): fresh=%d has_dirty_from_widgets=%d", is_fresh, ctr->has_dirty_from_widgets);
    // First loop try to fill background
    esp_video_render_blend_handle_t blender = NULL;
    esp_vui_container_get_blender(ctr, &blender);
    for (widget_node_t *node = ctr->widgets_head; node; node = node->next) {
        if (node->widget->visible) {
            continue;
        }
        if (node->widget->dirty.width && node->widget->dirty.height) {
            esp_video_render_rect_t dst_rect;
            dst_rect.x = node->widget->dirty.x;
            dst_rect.y = node->widget->dirty.y;
            dst_rect.width = node->widget->dirty.width;
            dst_rect.height = node->widget->dirty.height;
            esp_video_render_clr_t clr = {0};
            if (ctr->is_bg_set) {
                clr = ctr->bg_color;
            }
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Widget %d fill with background %d-%d %dx%d\n",
                                node->widget->id,
                                dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height);
            esp_video_render_blend_fill(blender, &dst_fb, &dst_rect, &clr);
            memset(&node->widget->dirty, 0, sizeof(esp_video_render_rect_t));
        }
    }
    // When refresh redraw all widgets with their rect not dirty region
    for (widget_node_t *node = ctr->widgets_head; node; node = node->next) {
        if (node->widget->visible == false) {
            continue;
        }
        esp_video_render_rect_t dirty_rect;
        if (is_fresh) {
            dirty_rect = node->widget->rect;
        } else if (node->widget->dirty.width && node->widget->dirty.height) {
            dirty_rect = node->widget->dirty;
        } else {
            continue;
        }
        if (ctr->is_bg_set) {
            esp_video_render_blend_fill(blender, &dst_fb, &dirty_rect, &ctr->bg_color);
        }
        esp_video_render_rect_t dst_rect;
        dst_rect.x = dirty_rect.x;
        dst_rect.y = dirty_rect.y;
        dst_rect.width = dirty_rect.width;
        dst_rect.height = dirty_rect.height;
        dirty_rect.x -= node->widget->rect.x;
        dirty_rect.y -= node->widget->rect.y;
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, " redraw(widget-cache)=%p dirty %d-%d %dx%d", (void *)node->widget,
                            dirty_rect.x, dirty_rect.y, dirty_rect.width, dirty_rect.height);
        node->widget->ops->redraw(node->widget, &dst_fb, &dst_rect, &dirty_rect);
        // Clear dirty region after draw
        memset(&node->widget->dirty, 0, sizeof(esp_video_render_rect_t));
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_container_redraw(esp_vui_overlay_rgn_t *rgn,
                                                const esp_video_render_dirty_rect_t *dirty_region,
                                                uint8_t dirty_count,
                                                void *user)
{
    if (rgn == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)rgn;
    if (ctr->region.frame.data) {
        // Redraw into cache, video render will blend the cached buffer
        redraw_to_cache(ctr);
        ctr->has_dirty_from_widgets = false;
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    if (ctr->region.fb == NULL) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    esp_video_render_fb_t dst_fb = *ctr->region.fb;
    bool is_fresh = ctr->region.compose.is_fresh;
    esp_video_render_blend_handle_t blender = NULL;
    esp_vui_container_get_blender(ctr, &blender);

    for (int i = 0; i < dirty_count; i++) {
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "redraw (no-cache): fresh=%d dirty_count=%u\n", is_fresh, dirty_count);
        // Draw background firstly
        esp_video_render_rect_t intersect;
        if (rect_intersect(&dirty_region[i].rect, &ctr->region.compose.disp_rect, &intersect, NULL) == false) {
            continue;
        }
        if (ctr->is_bg_set) {
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Container %p fill with background %d-%d %dx%d\n",
                                ctr, intersect.x, intersect.y, intersect.width, intersect.height);
            esp_video_render_blend_fill(blender, &dst_fb, &intersect, &ctr->bg_color);
        }
        for (widget_node_t *node = ctr->widgets_head; node; node = node->next) {
            if (node->widget->visible == false) {
                continue;
            }
            esp_video_render_rect_t cur = node->widget->rect;
            cur.x += ctr->region.compose.disp_rect.x;
            cur.y += ctr->region.compose.disp_rect.y;
            esp_video_render_rect_t dirty_rect;
            if (rect_intersect(&cur, &intersect, &dirty_rect, NULL)) {
                video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "  = %d-%d %dx%d\n", dirty_rect.x, dirty_rect.y, dirty_rect.width, dirty_rect.height);
                esp_video_render_rect_t dst_rect = dirty_rect;  // Coords to display
                dirty_rect.x -= (ctr->region.compose.disp_rect.x + node->widget->rect.x);
                dirty_rect.y -= (ctr->region.compose.disp_rect.y + node->widget->rect.y);
                video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, " redraw widget=%d:%p dirty %d-%d %dx%d dst %d-%d %dx%d\n",
                                    node->widget->id, (void *)node->widget,
                                    dirty_rect.x, dirty_rect.y, dirty_rect.width, dirty_rect.height,
                                    dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height);
                node->widget->ops->redraw(node->widget, &dst_fb, &dst_rect, &dirty_rect);
            }
        }
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_container_compose_lock(esp_vui_container_handle_t container)
{
    if (container == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    return esp_vui_overlay_compose_lock(ctr->overlay);
}

esp_video_render_err_t esp_vui_container_compose_unlock(esp_vui_container_handle_t container)
{
    if (container == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    return esp_vui_overlay_compose_unlock(ctr->overlay);
}

esp_video_render_err_t esp_vui_container_notify_compose_changed(esp_vui_container_handle_t container,
                                                                const esp_video_render_rect_t *dirty_region,
                                                                bool is_opaque)
{
    if (container == NULL ||
        (dirty_region != NULL && (dirty_region->width == 0 || dirty_region->height == 0))) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_t *ctr = (esp_vui_container_t *)container;
    video_render_compose_t *compose = &ctr->region.compose;
    esp_video_render_rect_t rect;
    if (dirty_region == NULL) {
        rect = compose->disp_rect;
    } else {
        rect.x = compose->disp_rect.x + dirty_region->x;
        rect.y = compose->disp_rect.y + dirty_region->y;
        rect.width = dirty_region->width;
        rect.height = dirty_region->height;
    }

    bool parent_opaque = (ctr->region.frame.data != NULL) && ctr->region.compose.alpha == 255 && ctr->region.compose.is_trans_color == false;
    bool opaque = is_opaque || parent_opaque;  // When container has buffer always opaque
    for (int i = 0; i < compose->dirty_count; i++) {
        esp_video_render_rect_t *prev_rect = &compose->dirty_area[i].rect;
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "  %d:%p Before %d-%d %dx%d opaque=%d\n",
                            i, compose, prev_rect->x, prev_rect->y, prev_rect->width, prev_rect->height,
                            compose->dirty_area[i].opaque);
    }
    compose->dirty_count = merge_compose_dirty_rect(compose, &rect, opaque);
    // Ask widgets to redraw just the dirty region on next refresh callback
    ctr->has_dirty_from_widgets = true;
    for (int i = 0; i < compose->dirty_count; i++) {
        esp_video_render_rect_t *merged_rect = &compose->dirty_area[i].rect;
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "  merged %d-%d %dx%d opaque=%d\n",
                            merged_rect->x, merged_rect->y, merged_rect->width, merged_rect->height,
                            compose->dirty_area[i].opaque);
    }
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG,
                        "notify_compose_changed: container=%p add dirty %d-%d %dx%d -> dirty_count=%u",
                        (void *)ctr, rect.x, rect.y, rect.width, rect.height, compose->dirty_count);
    esp_vui_overlay_mark_dirty(ctr->overlay);
    return ESP_VIDEO_RENDER_ERR_OK;
}
