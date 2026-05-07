/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "progress.h"
#include "esp_video_render.h"
#include "esp_vui_container.h"
#include "esp_vui_widget_default.h"
#include "esp_video_render_types.h"
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#define TAG  "PROGRESS"

#define RGB565_PACK(r, g, b)  ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

typedef struct {
    esp_vui_widget_t            widget;  // Base widget
    bool                        added;
    esp_vui_container_handle_t  container;
    uint16_t                    bg_color_rgb565;   // Background color in RGB565
    uint16_t                    bar_color_rgb565;  // Bar color in RGB565
    int                         bar_width;         // Full bar width (without padding)
    int                         bar_height;        // Bar height
    int                         padding;           // Padding around bar
    uint8_t                     current_percent;   // Current progress percentage (0-100)
    uint8_t                     draw_percent;
} progress_bar_widget_t;

static inline progress_bar_widget_t *get_progress_widget(esp_vui_widget_t *widget)
{
    return (progress_bar_widget_t *)widget;
}

static inline uint16_t rgb565_from_clr(const esp_video_render_clr_t *c)
{
    return RGB565_PACK(c->r, c->g, c->b);
}

static esp_video_render_err_t progress_bar_redraw(esp_vui_widget_t *self,
                                                  esp_video_render_fb_t *dst_fb,
                                                  const esp_video_render_rect_t *dst_rect,
                                                  const esp_video_render_rect_t *dirty)
{
    if (self == NULL || dst_fb == NULL || dst_fb->data == NULL || dst_rect == NULL) {
        ESP_LOGE(TAG, "redraw: Invalid arguments: self=%p, dst_fb=%p",
                 (void *)self, (void *)dst_fb);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    progress_bar_widget_t *bar = get_progress_widget(self);
    int widget_width = self->rect.width;
    int widget_height = self->rect.height;
    int filled_width = (bar->bar_width * bar->current_percent) / 100;
    if (filled_width < 0) {
        filled_width = 0;
    }
    if (filled_width > bar->bar_width) {
        filled_width = bar->bar_width;
    }

    // Bar area in widget coordinates
    int bar_x = bar->padding;
    int bar_y = bar->padding;
    int bar_filled_end_x = bar_x + filled_width;
    esp_video_render_rect_t draw_rect;
    if (dirty == NULL || (dirty->width == 0 && dirty->height == 0)) {
        // Full redraw
        draw_rect.x = 0;
        draw_rect.y = 0;
        draw_rect.width = widget_width;
        draw_rect.height = widget_height;
        ESP_LOGD(TAG, "redraw: Full redraw, dirty=NULL or empty");
    } else {
        // Partial redraw - use dirty region
        draw_rect = *dirty;
        ESP_LOGD(TAG, "redraw: Partial redraw, dirty: x=%d y=%d w=%d h=%d",
                 dirty->x, dirty->y, dirty->width, dirty->height);
        // Clip to widget bounds
        if (draw_rect.x >= widget_width || draw_rect.y >= widget_height) {
            return ESP_VIDEO_RENDER_ERR_OK;
        }
        if (draw_rect.x + draw_rect.width > widget_width) {
            draw_rect.width = widget_width - draw_rect.x;
        }
        if (draw_rect.y + draw_rect.height > widget_height) {
            draw_rect.height = widget_height - draw_rect.y;
        }
        if (draw_rect.width == 0 || draw_rect.height == 0) {
            return ESP_VIDEO_RENDER_ERR_OK;
        }
    }
    uint16_t fg_color;
    uint16_t bg_color;
    if (dst_fb->info.format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
        fg_color = __builtin_bswap16(bar->bar_color_rgb565);
        bg_color = __builtin_bswap16(bar->bg_color_rgb565);
    } else {
        fg_color = bar->bar_color_rgb565;
        bg_color = bar->bg_color_rgb565;
    }

    // Calculate destination pointer (container-relative coordinates)
    int bytes_per_pixel = 2;  // RGB565
    int dst_pitch = dst_fb->info.width * bytes_per_pixel;
    uint8_t *dst_data = (uint8_t *)dst_fb->data + dst_rect->y * dst_pitch + dst_rect->x * bytes_per_pixel;

    // Draw background for entire widget, then draw bar in the bar area
    int draw_start_x = draw_rect.x;
    int draw_end_x = draw_rect.x + draw_rect.width;
    int draw_start_y = draw_rect.y;
    int draw_end_y = draw_rect.y + draw_rect.height;

    ESP_LOGD(TAG, "redraw: Drawing region: x=%d-%d y=%d-%d",
             draw_start_x, draw_end_x, draw_start_y, draw_end_y);

    int pixels_drawn = 0;
    int bar_pixels = 0;
    int bg_pixels = 0;

    for (int y = draw_start_y; y < draw_end_y; y++) {
        // dst_data is already offset by dst_rect, so we need to add y offset
        uint16_t *dst_line = (uint16_t *)(dst_data + (y - draw_start_y) * dst_pitch);

        for (int x = draw_start_x; x < draw_end_x; x++) {
            // Check if we're in the bar area (vertical)
            if (y >= bar_y && y < bar_y + bar->bar_height) {
                // In bar area - check if filled
                if (x >= bar_x && x < bar_filled_end_x) {
                    // Draw bar color
                    dst_line[x - draw_start_x] = fg_color;
                    bar_pixels++;
                } else {
                    // Draw background color (unfilled portion of bar)
                    dst_line[x - draw_start_x] = bg_color;
                    bg_pixels++;
                }
            } else {
                // Outside bar area - draw background color (padding)
                dst_line[x - draw_start_x] = bg_color;
                bg_pixels++;
            }
            pixels_drawn++;
        }
    }

    ESP_LOGD(TAG, "redraw: Completed: pixels_drawn=%d bar_pixels=%d bg_pixels=%d",
             pixels_drawn, bar_pixels, bg_pixels);

    bar->draw_percent = bar->current_percent;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static void progress_bar_widget_destroy(esp_vui_widget_t *self)
{
    if (self == NULL) {
        return;
    }
    progress_bar_widget_t *bar = get_progress_widget(self);
    bar->widget.container = NULL;
    // Free the widget structure
    esp_gmf_oal_free(bar);
}

progress_bar_handle_t progress_bar_create(const esp_video_render_rect_t *video_rect,
                                          const progress_bar_cfg_t *cfg)
{
    if (video_rect == NULL || cfg == NULL || cfg->stream == NULL) {
        ESP_LOGE(TAG, "Invalid arguments");
        return NULL;
    }
    // Only support RGB565 for now
    if (cfg->format != ESP_VIDEO_RENDER_FORMAT_RGB565 &&
        cfg->format != ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
        ESP_LOGE(TAG, "Unsupported format: %d", cfg->format);
        return NULL;
    }
    esp_vui_overlay_handle_t overlay = NULL;
    esp_video_render_err_t ret = esp_video_render_stream_get_overlay(cfg->stream, &overlay);
    if (ret != ESP_VIDEO_RENDER_ERR_OK || overlay == NULL) {
        ESP_LOGE(TAG, "Failed to get overlay from stream: %d", ret);
        return NULL;
    }
    progress_bar_widget_t *bar = NULL;
    do {
        // Allocate progress bar widget
        bar = (progress_bar_widget_t *)esp_gmf_oal_calloc(1, sizeof(progress_bar_widget_t));
        if (bar == NULL) {
            ESP_LOGE(TAG, "Failed to allocate progress bar");
            break;
        }
        // Calculate progress bar dimensions
        bar->padding = cfg->padding > 0 ? cfg->padding : 4;
        bar->bar_width = (video_rect->width * 80) / 100;  // 80% of video width
        bar->bar_height = cfg->height > 0 ? cfg->height : 8;
        // Container size: bar width + padding on each side
        int container_width = bar->bar_width + (bar->padding * 2);
        int container_height = bar->bar_height + (bar->padding * 2);
        int container_x = video_rect->x + (video_rect->width - container_width) / 2;
        int container_y = video_rect->y + video_rect->height - container_height - cfg->bottom_margin;
        bar->bg_color_rgb565 = rgb565_from_clr(&cfg->bg_color);
        bar->bar_color_rgb565 = rgb565_from_clr(&cfg->bar_color);
        bar->current_percent = 0;
        bar->draw_percent = 0;

        // Create container
        esp_video_render_frame_info_t frame_info = {
            .width = container_width,
            .height = container_height,
            .format = cfg->format,
        };
        esp_video_render_pos_t container_pos = {.x = container_x, .y = container_y};
        ret = esp_vui_container_create(overlay, &frame_info, &container_pos, true, &bar->container);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "create: Failed to create container: %d", ret);
            break;
        }
        // Initialize widget to cover full container (including padding)
        // Widget rect should be container-local (0,0), not screen coordinates
        static const esp_vui_widget_ops_t progress_bar_ops = {
            .redraw = progress_bar_redraw,
            .destroy = progress_bar_widget_destroy,
        };
        bar->widget.ops = &progress_bar_ops;
        bar->widget.rect.x = 0;
        bar->widget.rect.y = 0;
        bar->widget.rect.width = container_width;
        bar->widget.rect.height = container_height;
        bar->widget.dirty = bar->widget.rect;
        bar->widget.visible = true;
        // Add widget to container
        ret = esp_vui_container_add_widget(bar->container, &bar->widget);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "create: Failed to add widget to container: %d", ret);
            break;
        }
        bar->added = true;
        return (progress_bar_handle_t)bar;
    } while (0);
    if (bar) {
        esp_vui_container_destroy(bar->container);
        esp_gmf_oal_free(bar);
    }
    return NULL;
}

esp_video_render_err_t progress_bar_update(progress_bar_handle_t handle, uint8_t percent)
{
    if (handle == NULL) {
        ESP_LOGE(TAG, "update: Invalid handle");
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    progress_bar_widget_t *bar = (progress_bar_widget_t *)handle;
    if (percent > 100) {
        percent = 100;
    }
    // Only update if percentage changed
    if (bar->current_percent == percent) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    if (bar->draw_percent > percent) {
        // Make all dirty (resetting)
        bar->widget.dirty.x = 0;
        bar->widget.dirty.y = 0;
        bar->widget.dirty.width = bar->bar_width + (bar->padding * 2);    // Full container width
        bar->widget.dirty.height = bar->bar_height + (bar->padding * 2);  // Full container height
        bar->current_percent = percent;
        esp_vui_container_notify_compose_changed(bar->container, &bar->widget.dirty, false);
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    int old_width = (bar->bar_width * bar->current_percent) / 100;
    bar->current_percent = percent;
    int new_width = (bar->bar_width * percent) / 100;

    ESP_LOGD(TAG, "update: old_width=%d new_width=%d", old_width, new_width);
    esp_video_render_rect_t widget_dirty;
    int bar_x = bar->padding;
    int bar_y = bar->padding;
    int dirty_start = 0;
    int dirty_end = (old_width > new_width) ? old_width : new_width;
    // If resetting to 0%, mark the old width to clear it
    if (new_width == 0 && old_width > 0) {
        dirty_start = 0;
        dirty_end = old_width;
    } else if (new_width > old_width) {
        // Growing: mark from 0 to new_width to ensure continuity
        dirty_start = 0;
        dirty_end = new_width;
    } else if (new_width < old_width) {
        // Shrinking: mark from new_width to old_width to clear old portion
        dirty_start = new_width;
        dirty_end = old_width;
    } else {
        // Same width (shouldn't happen due to early return, but handle it)
        dirty_start = 0;
        dirty_end = new_width;
    }
    // Convert to widget coordinates (add bar_x offset)
    widget_dirty.x = bar_x + dirty_start;
    widget_dirty.y = bar_y;
    widget_dirty.width = dirty_end - dirty_start;
    widget_dirty.height = bar->bar_height;
    if (widget_dirty.width == 0) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    ESP_LOGD(TAG, "update: widget_dirty: x=%d y=%d w=%d h=%d",
             widget_dirty.x, widget_dirty.y, widget_dirty.width, widget_dirty.height);
    bar->widget.dirty = widget_dirty;
    esp_vui_container_notify_compose_changed(bar->container, &widget_dirty, false);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t progress_bar_destroy(progress_bar_handle_t handle)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    progress_bar_widget_t *bar = (progress_bar_widget_t *)handle;
    // Save container pointer before removing widget (bar will be freed)
    esp_vui_container_handle_t container = bar->container;
    bool bar_added = bar->added;
    if (container) {
        if (bar_added) {
            esp_vui_container_remove_widget(container, &bar->widget);
        }
        esp_vui_container_destroy(container);
    }
    if (bar_added == false) {
        esp_gmf_oal_free(bar);
    }
    ESP_LOGD(TAG, "Progress bar destroyed");
    return ESP_VIDEO_RENDER_ERR_OK;
}
