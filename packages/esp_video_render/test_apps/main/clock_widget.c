/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <math.h>
#include "esp_vui_widget_default.h"
#include "video_render_utils.h"
#include "esp_video_render_blender.h"
#include "video_render_test.h"
#include "video_pattern.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_video_render_types.h"
#include "esp_fourcc.h"

#define TAG  "CLOCK_WIDGET"

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif  /* M_PI */

typedef struct clock_widget_ctx {
    esp_vui_widget_t        widget;
    esp_video_render_fb_t   fb;
    bool                    gen_fb;
    bool                    fresh;
    int                     size;                 // Clock diameter
    int                     center_x;             // Center X in widget coordinates
    int                     center_y;             // Center Y in widget coordinates
    int                     hour_length;          // Hour hand length
    int                     minute_length;        // Minute hand length
    int                     second_length;        // Second hand length
    esp_video_render_clr_t  bg_color;             // Background color
    esp_video_render_clr_t  hour_color;           // Hour hand color
    esp_video_render_clr_t  minute_color;         // Minute hand color
    esp_video_render_clr_t  second_color;         // Second hand color
    esp_video_render_clr_t  marker_color;         // Clock face marker color
    esp_video_render_clr_t  border_color;         // Clock border color
    int                     line_width;           // Line width for hands
    int                     marker_big_radius;    // Big marker radius (12, 3, 6, 9)
    int                     marker_small_radius;  // Small marker radius (other hours)
    int                     border_width;         // Clock border width
    int                     center_dot_radius;    // Center dot radius
} clock_widget_ctx_t;

// Helper function to draw a filled circle at a specific position
static void draw_circle_at_position(uint8_t *buffer, esp_video_render_format_t format,
                                    int buffer_width, int buffer_height, int pitch,
                                    int center_x, int center_y, int radius,
                                    esp_video_render_clr_t *color)
{
    if (buffer == NULL || color == NULL || radius < 1) {
        return;
    }

    uint32_t fourcc_format = (uint32_t)format;
    if (fourcc_format == 0) {
        return;
    }

    int bytes_per_pixel = (format == ESP_VIDEO_RENDER_FORMAT_RGB565 || format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) ? 2 : 3;
    if (pitch == 0) {
        pitch = buffer_width * bytes_per_pixel;
    }

    int radius_sq = radius * radius;
    int start_x = center_x - radius;
    int end_x = center_x + radius;
    int start_y = center_y - radius;
    int end_y = center_y + radius;

    // Clamp to buffer bounds
    if (start_x < 0) {
        start_x = 0;
    }
    if (end_x >= buffer_width) {
        end_x = buffer_width - 1;
    }
    if (start_y < 0) {
        start_y = 0;
    }
    if (end_y >= buffer_height) {
        end_y = buffer_height - 1;
    }

    uint16_t rgb565 = 0;
    uint16_t rgb565_be = 0;
    if (format == ESP_VIDEO_RENDER_FORMAT_RGB565 || format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
        rgb565 = RGB565_PACK(color->r, color->g, color->b);
        if (format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
            rgb565_be = SWAP_EDIAN(rgb565);
        }
    }

    for (int y = start_y; y <= end_y; y++) {
        for (int x = start_x; x <= end_x; x++) {
            int dx = x - center_x;
            int dy = y - center_y;
            int dist_sq = dx * dx + dy * dy;

            if (dist_sq <= radius_sq) {
                uint8_t *pixel_ptr = buffer + y * pitch + x * bytes_per_pixel;

                switch (format) {
                    default:
                        break;
                    case ESP_VIDEO_RENDER_FORMAT_RGB565: {
                        *((uint16_t *)pixel_ptr) = rgb565;
                    } break;
                    case ESP_VIDEO_RENDER_FORMAT_RGB565_BE: {
                        *((uint16_t *)pixel_ptr) = rgb565_be;
                    } break;
                    case ESP_VIDEO_RENDER_FORMAT_RGB888: {
                        pixel_ptr[0] = color->r;
                        pixel_ptr[1] = color->g;
                        pixel_ptr[2] = color->b;
                    } break;
                    case ESP_VIDEO_RENDER_FORMAT_BGR888: {
                        pixel_ptr[0] = color->b;
                        pixel_ptr[1] = color->g;
                        pixel_ptr[2] = color->r;
                    } break;
                }
            }
        }
    }
}

// Helper function to fill background
static void fill_background(uint8_t *buffer, esp_video_render_format_t format, int width, int height, int pitch,
                            esp_video_render_clr_t *color)
{
    if (buffer == NULL || color == NULL) {
        return;
    }

    int bytes_per_pixel;
    switch (format) {
        case ESP_VIDEO_RENDER_FORMAT_RGB565:
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE:
            bytes_per_pixel = 2;
            break;
        case ESP_VIDEO_RENDER_FORMAT_RGB888:
        case ESP_VIDEO_RENDER_FORMAT_BGR888:
            bytes_per_pixel = 3;
            break;
        default:
            return;
    }

    if (pitch == 0) {
        pitch = width * bytes_per_pixel;
    }

    uint16_t rgb565 = 0;
    uint16_t rgb565_be = 0;
    if (format == ESP_VIDEO_RENDER_FORMAT_RGB565 || format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
        rgb565 = RGB565_PACK(color->r, color->g, color->b);
        if (format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
            rgb565_be = SWAP_EDIAN(rgb565);
        }
    }

    for (int y = 0; y < height; y++) {
        uint8_t *row = buffer + y * pitch;
        for (int x = 0; x < width; x++) {
            uint8_t *pixel_ptr = row + x * bytes_per_pixel;
            switch (format) {
                case ESP_VIDEO_RENDER_FORMAT_RGB565: {
                    *((uint16_t *)pixel_ptr) = rgb565;
                } break;
                case ESP_VIDEO_RENDER_FORMAT_RGB565_BE: {
                    *((uint16_t *)pixel_ptr) = rgb565_be;
                } break;
                case ESP_VIDEO_RENDER_FORMAT_RGB888: {
                    pixel_ptr[0] = color->r;
                    pixel_ptr[1] = color->g;
                    pixel_ptr[2] = color->b;
                } break;
                case ESP_VIDEO_RENDER_FORMAT_BGR888: {
                    pixel_ptr[0] = color->b;
                    pixel_ptr[1] = color->g;
                    pixel_ptr[2] = color->r;
                } break;
                default:
                    break;
            }
        }
    }
}

static esp_video_render_err_t clock_widget_redraw(esp_vui_widget_t *self,
                                                  esp_video_render_fb_t *dst_fb,
                                                  const esp_video_render_rect_t *dst_rect_in,
                                                  const esp_video_render_rect_t *dirty)
{
    clock_widget_ctx_t *clock = (clock_widget_ctx_t *)self;
    if (self == NULL || dst_fb == NULL || dst_rect_in == NULL || clock->fb.data == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    // Determine redraw region
    esp_video_render_rect_t redraw_rect;
    if (clock->fresh || dirty == NULL) {
        redraw_rect.x = 0;
        redraw_rect.y = 0;
        redraw_rect.width = clock->fb.info.width;
        redraw_rect.height = clock->fb.info.height;
    } else {
        redraw_rect = *dirty;
        if (redraw_rect.x >= clock->fb.info.width || redraw_rect.y >= clock->fb.info.height) {
            return ESP_VIDEO_RENDER_ERR_OK;
        }
        if (redraw_rect.x + redraw_rect.width > clock->fb.info.width) {
            redraw_rect.width = clock->fb.info.width - redraw_rect.x;
        }
        if (redraw_rect.y + redraw_rect.height > clock->fb.info.height) {
            redraw_rect.height = clock->fb.info.height - redraw_rect.y;
        }
    }

    // Get current time (using esp_timer as milliseconds since boot, convert to seconds)
    int64_t time_us = esp_timer_get_time();
    int64_t total_seconds = time_us / 1000000;
    int seconds = total_seconds % 60;
    int minutes = (total_seconds / 60) % 60;
    int hours = (total_seconds / 3600) % 12;  // 12-hour format

    // Calculate hand angles (0 = 12 o'clock, clockwise)
    float hour_angle = (hours * 30.0f + minutes * 0.5f) * M_PI / 180.0f;  // 30 degrees per hour
    float minute_angle = (minutes * 6.0f) * M_PI / 180.0f;                // 6 degrees per minute
    float second_angle = (seconds * 6.0f) * M_PI / 180.0f;                // 6 degrees per second

    // Calculate hand endpoints (using widget-local coordinates)
    int hour_x = clock->center_x + (int)(sinf(hour_angle) * clock->hour_length);
    int hour_y = clock->center_y - (int)(cosf(hour_angle) * clock->hour_length);
    int minute_x = clock->center_x + (int)(sinf(minute_angle) * clock->minute_length);
    int minute_y = clock->center_y - (int)(cosf(minute_angle) * clock->minute_length);
    int second_x = clock->center_x + (int)(sinf(second_angle) * clock->second_length);
    int second_y = clock->center_y - (int)(cosf(second_angle) * clock->second_length);

    // Get buffer info
    int width = clock->fb.info.width;
    int height = clock->fb.info.height;
    int bytes_per_pixel = (clock->fb.info.format == ESP_VIDEO_RENDER_FORMAT_RGB565 || clock->fb.info.format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) ? 2 : 3;
    int pitch = width * bytes_per_pixel;  // Framebuffer pitch
    uint8_t *buffer = clock->fb.data;

    // Fill background for dirty region
    // Calculate offset for dirty region
    uint8_t *dirty_buffer = buffer + redraw_rect.y * pitch + redraw_rect.x * bytes_per_pixel;
    fill_background(dirty_buffer, clock->fb.info.format, redraw_rect.width, redraw_rect.height, pitch, &clock->bg_color);

    // Draw circular border (only on full redraw or if border area is dirty)
    bool draw_border = clock->fresh || (dirty == NULL);
    if (!draw_border && dirty != NULL) {
        // Check if border area intersects with dirty region
        int border_outer_radius = clock->size / 2;
        draw_border = (redraw_rect.x < clock->center_x + border_outer_radius && redraw_rect.x + redraw_rect.width > clock->center_x - border_outer_radius && redraw_rect.y < clock->center_y + border_outer_radius && redraw_rect.y + redraw_rect.height > clock->center_y - border_outer_radius);
    }

    if (draw_border && clock->border_width > 0) {
        // Draw circular border ring (only in the dirty region for efficiency)
        int border_outer_radius = clock->size / 2;
        int border_inner_radius = border_outer_radius - clock->border_width;
        int border_outer_sq = border_outer_radius * border_outer_radius;
        int border_inner_sq = border_inner_radius * border_inner_radius;

        // Clamp border drawing to dirty region
        int start_y = (redraw_rect.y > 0) ? redraw_rect.y : 0;
        int end_y = (redraw_rect.y + redraw_rect.height < height) ? redraw_rect.y + redraw_rect.height : height;
        int start_x = (redraw_rect.x > 0) ? redraw_rect.x : 0;
        int end_x = (redraw_rect.x + redraw_rect.width < width) ? redraw_rect.x + redraw_rect.width : width;

        for (int y = start_y; y < end_y; y++) {
            for (int x = start_x; x < end_x; x++) {
                int dx = x - clock->center_x;
                int dy = y - clock->center_y;
                int dist_sq = dx * dx + dy * dy;

                // Draw border ring
                if (dist_sq >= border_inner_sq && dist_sq <= border_outer_sq) {
                    uint8_t *pixel_ptr = buffer + y * pitch + x * bytes_per_pixel;
                    uint32_t fourcc_format = (uint32_t)(clock->fb.info.format);

                    if (fourcc_format == ESP_FOURCC_RGB16 || fourcc_format == ESP_FOURCC_RGB16_BE) {
                        uint16_t rgb565 = RGB565_PACK(clock->border_color.r, clock->border_color.g, clock->border_color.b);
                        if (fourcc_format == ESP_FOURCC_RGB16_BE) {
                            rgb565 = SWAP_EDIAN(rgb565);
                        }
                        *((uint16_t *)pixel_ptr) = rgb565;
                    } else if (fourcc_format == ESP_FOURCC_RGB24) {
                        pixel_ptr[0] = clock->border_color.r;
                        pixel_ptr[1] = clock->border_color.g;
                        pixel_ptr[2] = clock->border_color.b;
                    } else if (fourcc_format == ESP_FOURCC_BGR24) {
                        pixel_ptr[0] = clock->border_color.b;
                        pixel_ptr[1] = clock->border_color.g;
                        pixel_ptr[2] = clock->border_color.r;
                    }
                }
            }
        }
    }

    // Draw clock hands (only if they intersect with dirty region)
    // For simplicity, always redraw hands if full redraw, otherwise check intersection
    bool draw_hands = true;
    if (!clock->fresh && dirty != NULL) {
        // Simple check: if center is in dirty region, draw all hands
        draw_hands = (clock->center_x >= redraw_rect.x && clock->center_x < redraw_rect.x + redraw_rect.width && clock->center_y >= redraw_rect.y && clock->center_y < redraw_rect.y + redraw_rect.height);
    }

    // Draw clock face markers (only on full redraw or if markers area is dirty)
    bool draw_markers = clock->fresh || (dirty == NULL);
    if (!draw_markers && dirty != NULL) {
        // Check if markers area intersects with dirty region
        // Markers are near the edge, so check if dirty region is near edges
        int marker_outer_radius = clock->size / 2 - 2;
        draw_markers = (redraw_rect.x < clock->center_x + marker_outer_radius && redraw_rect.x + redraw_rect.width > clock->center_x - marker_outer_radius && redraw_rect.y < clock->center_y + marker_outer_radius && redraw_rect.y + redraw_rect.height > clock->center_y - marker_outer_radius);
    }

    if (draw_markers) {
        // Draw hour markers: big circles at 12, 3, 6, 9; small circles for others
        for (int hour = 0; hour < 12; hour++) {
            float angle = (hour * 30.0f) * M_PI / 180.0f;  // 30 degrees per hour
            int marker_radius = (hour % 3 == 0) ? clock->marker_big_radius : clock->marker_small_radius;
            int marker_distance = clock->size / 2 - marker_radius - 2;  // Distance from center
            int marker_x = clock->center_x + (int)(sinf(angle) * marker_distance);
            int marker_y = clock->center_y - (int)(cosf(angle) * marker_distance);

            draw_circle_at_position(buffer, clock->fb.info.format, width, height, pitch,
                                    marker_x, marker_y, marker_radius, &clock->marker_color);
        }
    }

    if (draw_hands) {
        // Convert format for draw_line API
        draw_ctx_t ctx = {
            .buffer = buffer,
            .format = clock->fb.info.format,
            .width = width,
            .height = height,
            .pitch = pitch,
        };
        // Draw hour hand (thicker, shorter)
        draw_line(&ctx,
                  clock->center_x, clock->center_y, hour_x, hour_y,
                  clock->line_width + 1, &clock->hour_color);

        // Draw minute hand (medium thickness)
        draw_line(&ctx,
                  clock->center_x, clock->center_y, minute_x, minute_y,
                  clock->line_width, &clock->minute_color);

        // Draw second hand (thinner, longer)
        draw_line(&ctx,
                  clock->center_x, clock->center_y, second_x, second_y,
                  (clock->line_width + 1) / 2, &clock->second_color);

        // Draw center dot
        if (clock->center_dot_radius > 0) {
            draw_circle_at_position(buffer, clock->fb.info.format, width, height, pitch,
                                    clock->center_x, clock->center_y, clock->center_dot_radius,
                                    &clock->hour_color);
        }
    }

    // Blend to destination framebuffer
    esp_video_render_rect_t src_rect = redraw_rect;
    esp_video_render_rect_t dst_rect;
    dst_rect.x = dst_rect_in->x;
    dst_rect.y = dst_rect_in->y;
    dst_rect.width = src_rect.width;
    dst_rect.height = src_rect.height;

    ESP_LOGI(TAG, "redraw clock=%d fresh=%d dirty=%p src %d-%d %dx%d -> dst %d-%d %dx%d vis=%d time=%02d:%02d:%02d",
             clock->widget.id, clock->fresh, (void *)dirty,
             src_rect.x, src_rect.y, src_rect.width, src_rect.height,
             dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height, self->visible,
             hours, minutes, seconds);

    clock->fresh = false;
    esp_video_render_blend_handle_t blender = NULL;
    esp_video_render_err_t ret = esp_vui_widget_get_blender(self, &blender);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        return ret;
    }
    return esp_video_render_blend_bitblt(blender, dst_fb, &clock->fb, &dst_rect, &src_rect);
}

static void clock_widget_destroy(esp_vui_widget_t *self)
{
    if (self == NULL) {
        return;
    }
    clock_widget_ctx_t *clock = (clock_widget_ctx_t *)self;

    self->container = NULL;  // Clear container reference

    if (clock->gen_fb && clock->fb.data) {
        video_render_free(clock->fb.data);
        clock->fb.data = NULL;
        clock->gen_fb = false;
    }
    video_render_free(clock);
}

esp_vui_widget_t *esp_vui_widget_clock_init(esp_vui_container_handle_t container,
                                            esp_video_render_frame_info_t *frame_info,
                                            esp_video_render_pos_t *pos,
                                            int size,
                                            esp_video_render_clr_t *bg_color,
                                            esp_video_render_clr_t *hour_color,
                                            esp_video_render_clr_t *minute_color,
                                            esp_video_render_clr_t *second_color)
{
    if (container == NULL || frame_info == NULL || pos == NULL || size <= 0) {
        return NULL;
    }

    clock_widget_ctx_t *clock = (clock_widget_ctx_t *)video_render_calloc(1, sizeof(clock_widget_ctx_t));
    if (clock == NULL) {
        return NULL;
    }

    static int clock_id = 0;
    clock->widget.id = clock_id++;

    // Set widget size (use size for both width and height, make it square)
    clock->widget.rect.x = pos->x;
    clock->widget.rect.y = pos->y;
    clock->widget.rect.width = size;
    clock->widget.rect.height = size;
    clock->widget.dirty = clock->widget.rect;

    // Calculate framebuffer size
    int bytes_per_pixel;
    switch (frame_info->format) {
        case ESP_VIDEO_RENDER_FORMAT_RGB565:
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE:
            bytes_per_pixel = 2;
            break;
        case ESP_VIDEO_RENDER_FORMAT_RGB888:
        case ESP_VIDEO_RENDER_FORMAT_BGR888:
            bytes_per_pixel = 3;
            break;
        default:
            ESP_LOGE(TAG, "Unsupported format for clock widget");
            video_render_free(clock);
            return NULL;
    }

    uint32_t fb_size = size * size * bytes_per_pixel;
    clock->fb.data = video_render_malloc(fb_size);
    if (clock->fb.data == NULL) {
        ESP_LOGE(TAG, "Failed to allocate clock framebuffer");
        video_render_free(clock);
        return NULL;
    }
    memset(clock->fb.data, 0, fb_size);

    clock->fb.info = *frame_info;
    clock->fb.info.width = size;
    clock->fb.info.height = size;
    clock->fb.size = fb_size;
    clock->gen_fb = true;
    clock->fresh = true;

    // Set clock parameters with better proportions
    clock->size = size;
    clock->center_x = size / 2;
    clock->center_y = size / 2;

    // Hand lengths (better proportions)
    clock->hour_length = size * 18 / 100;    // 18% of size
    clock->minute_length = size * 32 / 100;  // 32% of size
    clock->second_length = size * 42 / 100;  // 42% of size (almost to markers)

    // Line width scales with size
    clock->line_width = size / 50;
    if (clock->line_width < 2) {
        clock->line_width = 2;
    }
    if (clock->line_width > 6) {
        clock->line_width = 6;
    }

    // Set marker parameters (better proportions)
    clock->marker_big_radius = size / 25;
    if (clock->marker_big_radius < 3) {
        clock->marker_big_radius = 3;
    }
    if (clock->marker_big_radius > 10) {
        clock->marker_big_radius = 10;
    }
    clock->marker_small_radius = clock->marker_big_radius * 2 / 3;
    if (clock->marker_small_radius < 2) {
        clock->marker_small_radius = 2;
    }

    // Border and center dot
    clock->border_width = size / 60;
    if (clock->border_width < 2) {
        clock->border_width = 2;
    }
    if (clock->border_width > 5) {
        clock->border_width = 5;
    }

    clock->center_dot_radius = size / 35;
    if (clock->center_dot_radius < 2) {
        clock->center_dot_radius = 2;
    }
    if (clock->center_dot_radius > 6) {
        clock->center_dot_radius = 6;
    }

    // Set colors (use defaults if not provided)
    if (bg_color) {
        clock->bg_color = *bg_color;
    } else {
        clock->bg_color.r = 255;
        clock->bg_color.g = 255;
        clock->bg_color.b = 255;
    }
    if (hour_color) {
        clock->hour_color = *hour_color;
    } else {
        clock->hour_color.r = 0;
        clock->hour_color.g = 0;
        clock->hour_color.b = 0;
    }
    if (minute_color) {
        clock->minute_color = *minute_color;
    } else {
        clock->minute_color.r = 0;
        clock->minute_color.g = 0;
        clock->minute_color.b = 0;
    }
    if (second_color) {
        clock->second_color = *second_color;
    } else {
        clock->second_color.r = 255;
        clock->second_color.g = 0;
        clock->second_color.b = 0;
    }

    // Set marker color (default to same as hour/minute hands)
    clock->marker_color.r = clock->hour_color.r;
    clock->marker_color.g = clock->hour_color.g;
    clock->marker_color.b = clock->hour_color.b;

    // Set border color (default to darker version of marker color)
    clock->border_color.r = clock->marker_color.r / 2;
    clock->border_color.g = clock->marker_color.g / 2;
    clock->border_color.b = clock->marker_color.b / 2;

    static const esp_vui_widget_ops_t clock_ops = {
        .redraw = clock_widget_redraw,
        .destroy = clock_widget_destroy,
    };

    clock->widget.ops = &clock_ops;

    ESP_LOGI(TAG, "create clock=%p size=%dx%d pos=%d,%d widget.rect %d-%d %dx%d",
             (void *)clock, size, size, pos->x, pos->y,
             clock->widget.rect.x, clock->widget.rect.y, clock->widget.rect.width, clock->widget.rect.height);

    if (esp_vui_container_add_widget(container, &clock->widget) != ESP_VIDEO_RENDER_ERR_OK) {
        video_render_free(clock->fb.data);
        video_render_free(clock);
        return NULL;
    }

    return &clock->widget;
}

// Function to update clock (marks as dirty for redraw)
esp_video_render_err_t esp_vui_widget_clock_update(esp_vui_widget_t *widget)
{
    if (widget == NULL || widget->container == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    clock_widget_ctx_t *clock = (clock_widget_ctx_t *)widget;

    // Mark the entire widget as dirty since hands move
    esp_vui_container_compose_lock(widget->container);
    widget->dirty = widget->rect;
    clock->fresh = true;
    esp_video_render_err_t ret = esp_vui_container_notify_compose_changed(widget->container, &widget->dirty, true);
    esp_vui_container_compose_unlock(widget->container);
    return ret;
}
