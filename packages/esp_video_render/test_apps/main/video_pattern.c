/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include <stdio.h>
#include "video_pattern.h"
#ifndef __EMU__
#include "esp_video_enc_default.h"
#include "esp_video_enc.h"
#include "esp_video_codec_utils.h"
#endif  /* __EMU__ */
#include "video_render_sys.h"
#include "video_render_utils.h"
#include "esp_log.h"

#define TAG  "VIDEO_PATTERN"
/**
 * @brief  YUV pixel
 */
typedef struct {
    uint8_t  y;  /*!< Y */
    uint8_t  u;  /*!< U */
    uint8_t  v;  /*!< V */
} yuv_pixel_t;

int gen_pattern_color_bar(pattern_info_t *info)
{
    uint8_t *pixel = info->pixel;
    bool vertical = info->vertical;
    uint8_t n = info->bar_count;

    switch (info->format_id) {
        case ESP_VIDEO_RENDER_FORMAT_BGR888: {
            esp_video_render_clr_t *color = (esp_video_render_clr_t *)malloc(n * sizeof(esp_video_render_clr_t));
            if (color == NULL) {
                return -1;
            }
            for (int i = 0; i < n; i++) {
                color[i].r = (uint16_t)(rand() & 0xFF);
                color[i].g = (uint16_t)(rand() & 0xFF);
                color[i].b = (uint16_t)(rand() & 0xFF);
            }
            if (vertical) {
                uint32_t bar_w = info->res.width / n;
                uint32_t last_bar_w = info->res.width - bar_w * (n - 1);
                for (int y = 0; y < info->res.height; y++) {
                    for (int i = 0; i < n; i++) {
                        int points = (i == n - 1 ? last_bar_w : bar_w);
                        for (int x = 0; x < points; x++) {
                            *(pixel++) = color[i].b;
                            *(pixel++) = color[i].g;
                            *(pixel++) = color[i].r;
                        }
                    }
                }
            } else {
                uint32_t bar_h = info->res.height / n;
                uint32_t last_bar_h = info->res.height - bar_h * (n - 1);
                for (int i = 0; i < n; i++) {
                    int points = (i == n - 1 ? last_bar_h : bar_h) * info->res.width;
                    for (int x = 0; x < points; x++) {
                        *(pixel++) = color[i].b;
                        *(pixel++) = color[i].g;
                        *(pixel++) = color[i].r;
                    }
                }
            }
            free(color);
        } break;
        case ESP_VIDEO_RENDER_FORMAT_RGB888: {
            esp_video_render_clr_t *color = (esp_video_render_clr_t *)malloc(n * sizeof(esp_video_render_clr_t));
            if (color == NULL) {
                return -1;
            }
            for (int i = 0; i < n; i++) {
                color[i].r = (uint8_t)(rand() & 0xFF);
                color[i].g = (uint8_t)(rand() & 0xFF);
                color[i].b = (uint8_t)(rand() & 0xFF);
            }
            if (vertical) {
                uint32_t bar_w = info->res.width / n;
                uint32_t last_bar_w = info->res.width - bar_w * (n - 1);
                for (int y = 0; y < info->res.height; y++) {
                    for (int i = 0; i < n; i++) {
                        int points = (i == n - 1 ? last_bar_w : bar_w);
                        for (int x = 0; x < points; x++) {
                            *(pixel++) = color[i].r;
                            *(pixel++) = color[i].g;
                            *(pixel++) = color[i].b;
                        }
                    }
                }
            } else {
                uint32_t bar_h = info->res.height / n;
                uint32_t last_bar_h = info->res.height - bar_h * (n - 1);
                for (int i = 0; i < n; i++) {
                    int points = (i == n - 1 ? last_bar_h : bar_h) * info->res.width;
                    for (int x = 0; x < points; x++) {
                        *(pixel++) = color[i].r;
                        *(pixel++) = color[i].g;
                        *(pixel++) = color[i].b;
                    }
                }
            }
            free(color);
        } break;

        case ESP_VIDEO_RENDER_FORMAT_RGB565:
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE: {
            uint16_t *color = (uint16_t *)malloc(n * sizeof(uint16_t));
            if (color == NULL) {
                return -1;
            }
            for (int i = 0; i < n; i++) {
                color[i] = (uint16_t)(rand() & 0xFFFF);
            }
            uint16_t *pixel16 = (uint16_t *)pixel;
            if (vertical) {
                uint32_t bar_w = info->res.width / n;
                uint32_t last_bar_w = info->res.width - bar_w * (n - 1);
                for (int y = 0; y < info->res.height; y++) {
                    for (int i = 0; i < n; i++) {
                        int points = (i == n - 1 ? last_bar_w : bar_w);
                        for (int x = 0; x < points; x++) {
                            *(pixel16++) = color[i];
                        }
                    }
                }
            } else {
                uint32_t bar_h = info->res.height / n;
                uint32_t last_bar_h = info->res.height - bar_h * (n - 1);
                for (int i = 0; i < n; i++) {
                    int points = (i == n - 1 ? last_bar_h : bar_h) * info->res.width;
                    for (int x = 0; x < points; x++) {
                        *(pixel16++) = color[i];
                    }
                }
            }
            free(color);
        } break;
        case ESP_VIDEO_RENDER_FORMAT_YUV420P: {
            yuv_pixel_t *color = (yuv_pixel_t *)malloc(n * sizeof(yuv_pixel_t));
            if (color == NULL) {
                return -1;
            }
            for (int i = 0; i < n; i++) {
                color[i].y = (uint8_t)(rand() & 0xFF);
                color[i].u = (uint8_t)(rand() & 0xFF);
                color[i].v = (uint8_t)(rand() & 0xFF);
            }
            if (vertical) {
                uint32_t bar_w = (info->res.width / n) >> 1 << 1;
                uint32_t last_bar_w = info->res.width - bar_w * (n - 1);
                // Fill Y firstly
                for (int y = 0; y < info->res.height; y++) {
                    for (int i = 0; i < n; i++) {
                        uint32_t bytes = (i == n - 1 ? last_bar_w : bar_w);
                        memset(pixel, color[i].y, bytes);
                        pixel += bytes;
                    }
                }
                // Fill U
                for (int y = 0; y < info->res.height >> 1; y++) {
                    for (int i = 0; i < n; i++) {
                        uint32_t bytes = (i == n - 1 ? last_bar_w : bar_w) >> 1;
                        memset(pixel, color[i].u, bytes);
                        pixel += bytes;
                    }
                }
                // Fill V
                for (int y = 0; y < info->res.height >> 1; y++) {
                    for (int i = 0; i < n; i++) {
                        uint32_t bytes = (i == n - 1 ? last_bar_w : bar_w) >> 1;
                        memset(pixel, color[i].v, bytes);
                        pixel += bytes;
                    }
                }
            } else {
                uint32_t bar_h = (info->res.height / n) >> 1 << 1;
                uint32_t last_bar_h = info->res.height - bar_h * (n - 1);
                // Fill Y firstly
                for (int i = 0; i < n; i++) {
                    uint32_t bytes = (i == n - 1 ? last_bar_h : bar_h) * info->res.width;
                    memset(pixel, color[i].y, bytes);
                    pixel += bytes;
                }
                // Fill U
                for (int i = 0; i < n; i++) {
                    uint32_t bytes = (i == n - 1 ? last_bar_h : bar_h) * info->res.width >> 2;
                    memset(pixel, color[i].u, bytes);
                    pixel += bytes;
                }
                // Fill V
                for (int i = 0; i < n; i++) {
                    uint32_t bytes = (i == n - 1 ? last_bar_h : bar_h) * info->res.width >> 2;
                    memset(pixel, color[i].v, bytes);
                    pixel += bytes;
                }
            }
            free(color);
        } break;
        case ESP_VIDEO_RENDER_FORMAT_UYVY: {
            yuv_pixel_t *color = (yuv_pixel_t *)malloc(n * sizeof(yuv_pixel_t));
            if (color == NULL) {
                return -1;
            }
            for (int i = 0; i < n; i++) {
                color[i].y = (uint8_t)(rand() & 0xFF);
                color[i].u = (uint8_t)(rand() & 0xFF);
                color[i].v = (uint8_t)(rand() & 0xFF);
            }
            if (vertical) {
                uint32_t bar_w = (info->res.width / n) >> 1 << 1;
                // Fill Y firstly
                for (int y = 0; y < info->res.height; y++) {
                    int bar_filled = 0;
                    int i = 0;
                    for (int x = 0; x < (info->res.width >> 1); x++) {
                        *pixel++ = color[i].u;
                        *pixel++ = color[i].y;
                        *pixel++ = color[i].v;
                        *pixel++ = color[i].y;
                        bar_filled += 2;
                        if (bar_filled >= bar_w) {
                            bar_filled = 0;
                            if (i < n - 1) {
                                i++;
                            }
                        }
                    }
                }
            } else {
                uint32_t bar_h = (info->res.height / n) >> 1 << 1;
                uint32_t last_bar_h = info->res.height - bar_h * (n - 1);
                // Fill Y firstly
                for (int i = 0; i < n; i++) {
                    uint32_t bytes = (i == n - 1 ? last_bar_h : bar_h) * info->res.width * 3 / 2;
                    while (bytes > 0) {
                        *pixel++ = color[i].u;
                        *pixel++ = color[i].y;
                        *pixel++ = color[i].v;
                        *pixel++ = color[i].y;
                        bytes -= 3;
                    }
                }
            }
            free(color);
        } break;
        case ESP_VIDEO_RENDER_FORMAT_YUV422: {
            yuv_pixel_t *color = (yuv_pixel_t *)malloc(n * sizeof(yuv_pixel_t));
            if (color == NULL) {
                return -1;
            }
            for (int i = 0; i < n; i++) {
                color[i].y = (uint8_t)(rand() & 0xFF);
                color[i].u = (uint8_t)(rand() & 0xFF);
                color[i].v = (uint8_t)(rand() & 0xFF);
            }
            if (vertical) {
                uint32_t bar_w = (info->res.width / n) >> 1 << 1;
                // Fill Y firstly
                for (int y = 0; y < info->res.height; y++) {
                    int bar_filled = 0;
                    int i = 0;
                    for (int x = 0; x < (info->res.width >> 1); x++) {
                        *pixel++ = color[i].y;
                        *pixel++ = color[i].u;
                        *pixel++ = color[i].y;
                        *pixel++ = color[i].v;
                        bar_filled += 2;
                        if (bar_filled >= bar_w) {
                            bar_filled = 0;
                            if (i < n - 1) {
                                i++;
                            }
                        }
                    }
                }
            } else {
                uint32_t bar_h = (info->res.height / n) >> 1 << 1;
                uint32_t last_bar_h = info->res.height - bar_h * (n - 1);
                // Fill Y firstly
                for (int i = 0; i < n; i++) {
                    uint32_t bytes = (i == n - 1 ? last_bar_h : bar_h) * info->res.width * 3 / 2;
                    while (bytes > 0) {
                        *pixel++ = color[i].y;
                        *pixel++ = color[i].u;
                        *pixel++ = color[i].y;
                        *pixel++ = color[i].v;
                        bytes -= 3;
                    }
                }
            }
            free(color);
        } break;
        case ESP_VIDEO_RENDER_FORMAT_O_UYY_E_VYY: {
            yuv_pixel_t *color = (yuv_pixel_t *)malloc(n * sizeof(yuv_pixel_t));
            if (color == NULL) {
                return -1;
            }
            for (int i = 0; i < n; i++) {
                color[i].y = (uint8_t)(rand() & 0xFF);
                color[i].u = (uint8_t)(rand() & 0xFF);
                color[i].v = (uint8_t)(rand() & 0xFF);
            }
            if (vertical) {
                uint32_t bar_w = (info->res.width / n) >> 1 << 1;
                // Fill Y firstly
                for (int y = 0; y < (info->res.height >> 1); y++) {
                    int bar_filled = 0;
                    int i = 0;
                    for (int x = 0; x < (info->res.width >> 1); x++) {
                        *pixel++ = color[i].u;
                        *pixel++ = color[i].y;
                        *pixel++ = color[i].y;
                        bar_filled += 2;
                        if (bar_filled >= bar_w) {
                            bar_filled = 0;
                            if (i < n - 1) {
                                i++;
                            }
                        }
                    }
                    bar_filled = 0;
                    i = 0;
                    for (int x = 0; x < (info->res.width >> 1); x++) {
                        *pixel++ = color[i].v;
                        *pixel++ = color[i].y;
                        *pixel++ = color[i].y;
                        bar_filled += 2;
                        if (bar_filled >= bar_w) {
                            bar_filled = 0;
                            if (i < n - 1) {
                                i++;
                            }
                        }
                    }
                }
            } else {
                uint32_t bar_h = (info->res.height / n) >> 1 << 1;
                uint32_t last_bar_h = info->res.height - bar_h * (n - 1);
                // Fill Y firstly
                for (int i = 0; i < n; i++) {
                    uint32_t height = (i == n - 1 ? last_bar_h : bar_h);
                    uint32_t width = info->res.width >> 1;
                    for (int y = 0; y < (height >> 1); y++) {
                        for (int x = 0; x < width; x++) {
                            *pixel++ = color[i].u;
                            *pixel++ = color[i].y;
                            *pixel++ = color[i].y;
                        }
                        for (int x = 0; x < width; x++) {
                            *pixel++ = color[i].v;
                            *pixel++ = color[i].y;
                            *pixel++ = color[i].y;
                        }
                    }
                }
            }
            free(color);
            break;
        }
        default:
            ESP_LOGE(TAG, "Unsupported format: %x", info->format_id);
            return -1;
    }
    return 0;
}

static int get_bytes_per_pixel(uint32_t format)
{
    switch (format) {
        case ESP_VIDEO_RENDER_FORMAT_RGB565:
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE:
            return 2;
        case ESP_VIDEO_RENDER_FORMAT_RGB888:
        case ESP_VIDEO_RENDER_FORMAT_BGR888:
            return 3;
        default:
            return 0;
    }
}

int draw_circle(draw_ctx_t *ctx, esp_video_render_clr_t *bg_color, esp_video_render_clr_t *fg_color, int radius)
{
    if (ctx == NULL || ctx->buffer == NULL || bg_color == NULL || fg_color == NULL) {
        return -1;
    }

    uint8_t *pixel = ctx->buffer;
    int width = ctx->width;
    int height = ctx->height;
    int center_x = width / 2;
    int center_y = height / 2;
    if (ctx->pitch == 0) {
        ctx->pitch = width * get_bytes_per_pixel(ctx->format);
    }
    if (radius <= 0) {
        radius = (width < height ? width : height) / 2 - 1;
    }
    if (radius < 1) {
        radius = 1;
    }

    int radius_sq = radius * radius;

    switch (ctx->format) {
        case ESP_VIDEO_RENDER_FORMAT_RGB565:
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE: {
            uint16_t bg_rgb565 = RGB565_PACK(bg_color->r, bg_color->g, bg_color->b);
            uint16_t fg_rgb565 = RGB565_PACK(fg_color->r, fg_color->g, fg_color->b);
            uint16_t *pixel16 = (uint16_t *)pixel;

            if (ctx->format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
                bg_rgb565 = SWAP_EDIAN(bg_rgb565);
                fg_rgb565 = SWAP_EDIAN(fg_rgb565);
            }

            // Fill entire image with background color, then draw circle
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    // Calculate distance from center
                    int dx = x - center_x;
                    int dy = y - center_y;
                    int dist_sq = dx * dx + dy * dy;

                    // Draw circle if within radius
                    if (dist_sq <= radius_sq) {
                        pixel16[y * width + x] = fg_rgb565;
                    } else {
                        pixel16[y * width + x] = bg_rgb565;
                    }
                }
            }
        } break;

        case ESP_VIDEO_RENDER_FORMAT_RGB888: {
            // Fill entire image with background color, then draw circle
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int dx = x - center_x;
                    int dy = y - center_y;
                    int dist_sq = dx * dx + dy * dy;

                    if (dist_sq <= radius_sq) {
                        pixel[(y * width + x) * 3 + 0] = fg_color->r;
                        pixel[(y * width + x) * 3 + 1] = fg_color->g;
                        pixel[(y * width + x) * 3 + 2] = fg_color->b;
                    } else {
                        pixel[(y * width + x) * 3 + 0] = bg_color->r;
                        pixel[(y * width + x) * 3 + 1] = bg_color->g;
                        pixel[(y * width + x) * 3 + 2] = bg_color->b;
                    }
                }
            }
        } break;

        case ESP_VIDEO_RENDER_FORMAT_BGR888: {
            // Fill entire image with background color, then draw circle
            for (int y = 0; y < height; y++) {
                for (int x = 0; x < width; x++) {
                    int dx = x - center_x;
                    int dy = y - center_y;
                    int dist_sq = dx * dx + dy * dy;

                    if (dist_sq <= radius_sq) {
                        pixel[(y * width + x) * 3 + 0] = fg_color->b;
                        pixel[(y * width + x) * 3 + 1] = fg_color->g;
                        pixel[(y * width + x) * 3 + 2] = fg_color->r;
                    } else {
                        pixel[(y * width + x) * 3 + 0] = bg_color->b;
                        pixel[(y * width + x) * 3 + 1] = bg_color->g;
                        pixel[(y * width + x) * 3 + 2] = bg_color->r;
                    }
                }
            }
        } break;

        default:
            // Unsupported format
            return -1;
    }

    return 0;
}

int draw_line(draw_ctx_t *ctx,
              int x1, int y1, int x2, int y2, int line_width, esp_video_render_clr_t *color)
{
    if (ctx == NULL || ctx->buffer == NULL || color == NULL || line_width < 1) {
        return -1;
    }
    int width = ctx->width;
    int height = ctx->height;
    if (ctx->pitch == 0) {
        ctx->pitch = width * get_bytes_per_pixel(ctx->format);
    }
    // Clamp coordinates to buffer bounds
    int min_x = (x1 < x2) ? x1 : x2;
    int max_x = (x1 > x2) ? x1 : x2;
    int min_y = (y1 < y2) ? y1 : y2;
    int max_y = (y1 > y2) ? y1 : y2;

    if (max_x < 0 || min_x >= width || max_y < 0 || min_y >= height) {
        return 0;  // Line completely outside buffer
    }

    // Prepare color values
    uint16_t rgb565 = 0;
    uint16_t rgb565_be = 0;
    if (ctx->format == ESP_VIDEO_RENDER_FORMAT_RGB565 || ctx->format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
        rgb565 = RGB565_PACK(color->r, color->g, color->b);
        if (ctx->format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
            rgb565_be = SWAP_EDIAN(rgb565);
        }
    }

    // Bresenham's line algorithm with width support
    int dx = abs(x2 - x1);
    int dy = abs(y2 - y1);
    int sx = (x1 < x2) ? 1 : -1;
    int sy = (y1 < y2) ? 1 : -1;
    int err = dx - dy;
    int err2;

    int x = x1;
    int y = y1;
    int half_width = line_width / 2;

    // Draw line using Bresenham's algorithm
    while (true) {
        // Draw line_width pixels around the center point (x, y)
        for (int wy = -half_width; wy <= half_width; wy++) {
            for (int wx = -half_width; wx <= half_width; wx++) {
                int px = x + wx;
                int py = y + wy;

                // Check bounds
                if (px < 0 || px >= width || py < 0 || py >= height) {
                    continue;
                }

                // Check if within line_width circle/rectangle
                if (wx * wx + wy * wy <= half_width * half_width + half_width) {
                    uint8_t *pixel_ptr = ctx->buffer + py * ctx->pitch + px * get_bytes_per_pixel(ctx->format);

                    switch (ctx->format) {
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

        if (x == x2 && y == y2) {
            break;
        }

        err2 = 2 * err;
        if (err2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (err2 < dx) {
            err += dx;
            y += sy;
        }
    }

    return 0;
}

#ifndef __EMU__
int gen_raw_frame(esp_video_render_img_t *image, bool vertical, int bar_count)
{
    esp_video_codec_resolution_t res = {
        .width = image->info.width,
        .height = image->info.height,
    };
    uint32_t size = esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)image->info.format, &res);
    if (image->size < size) {
        if (image->data) {
            video_render_free(image->data);
        }
        image->data = video_render_malloc_align(size, video_render_get_default_alignment());
        if (image->data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes for raw frame", (int)size);
            image->size = 0;
            return -1;
        }
    }
    pattern_info_t info = {
        .format_id = image->info.format,
        .res = {.width = res.width, .height = res.height},
        .pixel = image->data,
        .data_size = size,
        .bar_count = bar_count,
        .vertical = vertical,
    };
    int ret = gen_pattern_color_bar(&info);
    if (ret != 0) {
        if (image->data) {
            video_render_free(image->data);
            image->data = NULL;
            image->size = 0;
        }
        return ret;
    }
    image->size = size;
    return 0;
}

int gen_circle_image(esp_video_render_img_t *image, esp_video_render_clr_t *bg_color, esp_video_render_clr_t *fg_color, int radius)
{
    esp_video_codec_resolution_t res = {
        .width = image->info.width,
        .height = image->info.height,
    };
    uint32_t size = esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)image->info.format, &res);
    // Always allocate if buffer is NULL or size is insufficient
    if (image->data == NULL || image->size < size) {
        if (image->data) {
            video_render_free(image->data);
        }
        image->data = video_render_malloc_align(size, video_render_get_default_alignment());
        if (image->data == NULL) {
            ESP_LOGE(TAG, "Failed to allocate %d bytes for circle image", (int)size);
            return -1;
        }
        image->size = size;
    }
    esp_video_render_clr_t bg_rgb = {
        .r = bg_color->r,
        .g = bg_color->g,
        .b = bg_color->b,
    };
    esp_video_render_clr_t fg_rgb = {
        .r = fg_color->r,
        .g = fg_color->g,
        .b = fg_color->b,
    };
    draw_ctx_t ctx = {
        .buffer = image->data,
        .format = image->info.format,
        .width = image->info.width,
        .height = image->info.height,
    };
    int ret = draw_circle(&ctx, &bg_rgb, &fg_rgb, radius);
    if (ret != 0) {
        if (image->data) {
            video_render_free(image->data);
            image->data = NULL;
            image->size = 0;
        }
        return ret;
    }
    image->size = size;
    return 0;
}

int gen_image(esp_video_render_img_t *image, bool vertical, int bar_count)
{
    if (video_render_is_encoded(image->info.format) == false) {
        return gen_raw_frame(image, vertical, bar_count);
    }
    // Register encoder for UT to generate image, no need used it
    esp_video_enc_register_default();
    int ret = 0;
    esp_video_enc_handle_t enc_handle = NULL;
    uint8_t *encoded_data = NULL;
    do {
        esp_video_codec_query_t query = {
            .codec_type = (esp_video_codec_type_t)image->info.format,
        };
        esp_video_enc_caps_t enc_caps = {0};
        ret = esp_video_enc_query_caps(&query, &enc_caps);
        if (ret != ESP_VC_ERR_OK) {
            ESP_LOGE(TAG, "Failed to query encoder caps");
            break;
        }
        esp_video_enc_cfg_t enc_cfg = {
            .codec_type = (esp_video_codec_type_t)image->info.format,
            .in_fmt = enc_caps.in_fmts[0],
            .resolution = {
                .width = image->info.width,
                .height = image->info.height,
            },
            .fps = 1,
        };
        ret = esp_video_enc_open(&enc_cfg, &enc_handle);
        if (ret != ESP_VC_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open encoder");
            break;
        }

        uint32_t bitrate = enc_cfg.resolution.width * enc_cfg.resolution.height * enc_cfg.fps / 10;
        esp_video_enc_set_bitrate(enc_handle, bitrate);
        // Use 422 chroma subsampling when output is YUV422
        if (enc_cfg.in_fmt == ESP_VIDEO_CODEC_PIXEL_FMT_YUV422 || enc_cfg.in_fmt == ESP_VIDEO_CODEC_PIXEL_FMT_UYVY422) {
            esp_video_enc_set_chroma_subsampling(enc_handle, ESP_VIDEO_CODEC_CHROMA_SUBSAMPLING_422);
        }
        // Gen pattern for encode input
        image->info.format = (esp_video_render_format_t)enc_cfg.in_fmt;
        ret = gen_raw_frame(image, vertical, bar_count);
        // Recover encoded format
        image->info.format = (esp_video_render_format_t)enc_cfg.codec_type;
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to generate raw frame");
            break;
        }
        uint32_t encoded_size = image->size;
        uint8_t alignment = video_render_get_default_alignment();
        encoded_data = esp_video_codec_align_alloc(alignment, encoded_size, &encoded_size);
        if (encoded_data == NULL) {
            ret = ESP_VIDEO_RENDER_ERR_NO_MEM;
            break;
        }
        esp_video_enc_out_frame_t out_frame = {
            .data = encoded_data,
            .size = encoded_size,
        };
        esp_video_enc_in_frame_t in_frame = {
            .data = image->data,
            .size = image->size,
        };
        while (in_frame.consumed < in_frame.size) {
            ret = esp_video_enc_process(enc_handle, &in_frame, &out_frame);
            // Allow output decoded size to be 0, so that can retry
            if (ret != ESP_VC_ERR_OK) {
                ESP_LOGE(TAG, "Fail to encode frame");
                break;
            }
            in_frame.data += in_frame.consumed;
            in_frame.size -= in_frame.consumed;
            in_frame.consumed = 0;
        }
        if (ret != ESP_VC_ERR_OK) {
            ESP_LOGE(TAG, "Failed to encode frame");
            break;
        }
        if (image->data) {
            video_render_free(image->data);
        }
        image->size = out_frame.encoded_size;
        image->data = encoded_data;
        encoded_data = NULL;
    } while (0);

    if (ret != 0) {
        if (image->data) {
            video_render_free(image->data);
            image->data = NULL;
            image->size = 0;
        }
        if (encoded_data) {
            video_render_free(encoded_data);
        }
    }
    if (enc_handle) {
        esp_video_enc_close(enc_handle);
    }
    esp_video_enc_unregister_default();
    return ret;
}
#endif  /* __EMU__ */
