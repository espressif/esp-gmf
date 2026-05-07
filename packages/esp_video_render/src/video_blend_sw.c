/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_video_render_blender.h"
#include "video_render_utils.h"
#include "video_blend_hw.h"
#include "esp_log.h"

#define TAG  "VIDEO_BLEND_SW"

/**
 * @brief  Software blend realization, supported blend processor
 *         - Blender
 *         - Transparent color blender
 *         - Fill blender
 *         - Bitblt blender
 */

#define RGB565_R(p)           (((p) >> 11) & 0x1F)
#define RGB565_G(p)           (((p) >> 5) & 0x3F)
#define RGB565_B(p)           ((p) & 0x1F)
#define RGB565_PACK(r, g, b)  (((r) << 11) | ((g) << 5) | (b))

/**
 * @brief  RGB565 macros for fast pixel manipulation
 */
#define ALPHA_BLEND_5BIT(src, dst, alpha)  \
    (((src * alpha) + (dst * (255 - alpha))) >> 8)
#define ALPHA_BLEND_6BIT(src, dst, alpha)  \
    (((src * alpha) + (dst * (255 - alpha))) >> 8)
#define TRANS_COLOR_TOLERANCE  \
    8

static inline bool is_blend_supported_format(esp_video_render_format_t format)
{
    if (format == ESP_VIDEO_RENDER_FORMAT_RGB565) {
        return true;
    }
    if (format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
        return true;
    }
    return false;
}

static void blend_rgb565_le(esp_video_render_fb_t *dst_fb,
                            esp_video_render_fb_t *src_fb,
                            esp_video_render_rect_t *dst_rect,
                            esp_video_render_rect_t *src_rect,
                            uint8_t global_alpha)
{
    if (global_alpha == 0) {
        return;  // Fully transparent, no blending needed
    }

    uint8_t pixel_bytes = 2;
    uint32_t src_pitch = src_fb->info.width * pixel_bytes;
    uint32_t dst_pitch = dst_fb->info.width * pixel_bytes;
    uint8_t *src_ptr = src_fb->data + src_rect->x * pixel_bytes + src_rect->y * src_pitch;
    uint8_t *dst_ptr = dst_fb->data + dst_rect->x * pixel_bytes + dst_rect->y * dst_pitch;
    bool need_swap = src_fb->info.format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE;

    // Fast path for fully opaque blending
    if (global_alpha == 255) {
        for (int i = 0; i < dst_rect->height; i++) {
            uint16_t *src_row = (uint16_t *)src_ptr;
            uint16_t *dst_row = (uint16_t *)dst_ptr;

            // Copy entire row for fully opaque
            for (int j = 0; j < dst_rect->width; j++) {
                if (need_swap) {
                    dst_row[j] = __builtin_bswap16(src_row[j]);
                } else {
                    dst_row[j] = src_row[j];
                }
            }
            src_ptr += src_pitch;
            dst_ptr += dst_pitch;
        }
        return;
    }

    // Alpha blending path
    for (int i = 0; i < dst_rect->height; i++) {
        uint16_t *src_row = (uint16_t *)src_ptr;
        uint16_t *dst_row = (uint16_t *)dst_ptr;

        for (int j = 0; j < dst_rect->width; j++) {
            uint16_t src_pixel = src_row[j];
            uint16_t dst_pixel = dst_row[j];
            if (need_swap) {
                src_pixel = __builtin_bswap16(src_pixel);
            }

            // Extract RGB components
            uint8_t src_r = RGB565_R(src_pixel);
            uint8_t src_g = RGB565_G(src_pixel);
            uint8_t src_b = RGB565_B(src_pixel);

            uint8_t dst_r = RGB565_R(dst_pixel);
            uint8_t dst_g = RGB565_G(dst_pixel);
            uint8_t dst_b = RGB565_B(dst_pixel);

            // Perform alpha blending: result = src * alpha + dst * (1-alpha)
            uint8_t blend_r = ALPHA_BLEND_5BIT(src_r, dst_r, global_alpha);
            uint8_t blend_g = ALPHA_BLEND_6BIT(src_g, dst_g, global_alpha);
            uint8_t blend_b = ALPHA_BLEND_5BIT(src_b, dst_b, global_alpha);

            // Pack result back to RGB565
            dst_row[j] = RGB565_PACK(blend_r, blend_g, blend_b);
        }

        src_ptr += src_pitch;
        dst_ptr += dst_pitch;
    }
}

static void blend_rgb565_be(esp_video_render_fb_t *dst_fb, esp_video_render_fb_t *src_fb,
                            esp_video_render_rect_t *dst_rect,
                            esp_video_render_rect_t *src_rect,
                            uint8_t global_alpha)
{
    if (global_alpha == 0) {
        return;  // Fully transparent, no blending needed
    }

    uint8_t pixel_bytes = 2;
    uint32_t src_pitch = src_fb->info.width * pixel_bytes;
    uint32_t dst_pitch = dst_fb->info.width * pixel_bytes;
    uint8_t *src_ptr = src_fb->data + src_rect->x * pixel_bytes + src_rect->y * src_pitch;
    uint8_t *dst_ptr = dst_fb->data + dst_rect->x * pixel_bytes + dst_rect->y * dst_pitch;
    bool need_swap = src_fb->info.format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE;
    // Fast path for fully opaque blending
    if (global_alpha == 255) {
        for (int i = 0; i < dst_rect->height; i++) {
            uint16_t *src_row = (uint16_t *)src_ptr;
            uint16_t *dst_row = (uint16_t *)dst_ptr;

            // Copy entire row for fully opaque
            for (int j = 0; j < dst_rect->width; j++) {
                if (!need_swap) {
                    dst_row[j] = __builtin_bswap16(src_row[j]);
                } else {
                    dst_row[j] = src_row[j];
                }
            }
            src_ptr += src_pitch;
            dst_ptr += dst_pitch;
        }
        return;
    }

    // Alpha blending path with byte swap for big endian
    for (int i = 0; i < dst_rect->height; i++) {
        uint16_t *src_row = (uint16_t *)src_ptr;
        uint16_t *dst_row = (uint16_t *)dst_ptr;

        for (int j = 0; j < dst_rect->width; j++) {
            // Convert from big endian to native
            uint16_t src_pixel = src_row[j];
            uint16_t dst_pixel = __builtin_bswap16(dst_row[j]);
            if (need_swap) {
                src_pixel = __builtin_bswap16(src_pixel);
            }
            // Extract RGB components
            uint8_t src_r = RGB565_R(src_pixel);
            uint8_t src_g = RGB565_G(src_pixel);
            uint8_t src_b = RGB565_B(src_pixel);

            uint8_t dst_r = RGB565_R(dst_pixel);
            uint8_t dst_g = RGB565_G(dst_pixel);
            uint8_t dst_b = RGB565_B(dst_pixel);

            // Perform alpha blending: result = src * alpha + dst * (1-alpha)
            uint8_t blend_r = ALPHA_BLEND_5BIT(src_r, dst_r, global_alpha);
            uint8_t blend_g = ALPHA_BLEND_6BIT(src_g, dst_g, global_alpha);
            uint8_t blend_b = ALPHA_BLEND_5BIT(src_b, dst_b, global_alpha);

            // Pack result back to RGB565 and convert to big endian
            dst_row[j] = __builtin_bswap16(RGB565_PACK(blend_r, blend_g, blend_b));
        }

        src_ptr += src_pitch;
        dst_ptr += dst_pitch;
    }
}

esp_video_render_err_t esp_video_render_blend_sw(esp_video_render_fb_t *dst, esp_video_render_fb_t *src,
                                                 esp_video_render_rect_t *dst_rect,
                                                 esp_video_render_rect_t *src_rect,
                                                 uint8_t global_alpha)
{
    // Validate input parameters
    if (!dst || !src || !dst->data || !src->data) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    // Check if src and dst formats match
    if (!is_blend_supported_format(dst->info.format) || !is_blend_supported_format(src->info.format)) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    // Check if rect sizes match
    if (dst_rect->width != src_rect->width || dst_rect->height != src_rect->height) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    switch (dst->info.format) {
        case ESP_VIDEO_RENDER_FORMAT_RGB565:
            blend_rgb565_le(dst, src, dst_rect, src_rect, global_alpha);
            break;
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE:
            blend_rgb565_be(dst, src, dst_rect, src_rect, global_alpha);
            break;
        default:
            return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }

    return ESP_VIDEO_RENDER_ERR_OK;
}

static inline uint8_t rgb565_5_to_8(uint8_t v)
{
    return (uint8_t)((v << 3) | (v >> 2));
}

static inline uint8_t rgb565_6_to_8(uint8_t v)
{
    return (uint8_t)((v << 2) | (v >> 4));
}

static inline uint8_t diff_u8(uint8_t a, uint8_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

static bool pixel_match_trans_color_rgb565(uint16_t pixel, esp_video_render_clr_t *trans_color)
{
    uint8_t pr = rgb565_5_to_8(RGB565_R(pixel));
    uint8_t pg = rgb565_6_to_8(RGB565_G(pixel));
    uint8_t pb = rgb565_5_to_8(RGB565_B(pixel));
    return diff_u8(pr, trans_color->r) <= TRANS_COLOR_TOLERANCE &&
           diff_u8(pg, trans_color->g) <= TRANS_COLOR_TOLERANCE &&
           diff_u8(pb, trans_color->b) <= TRANS_COLOR_TOLERANCE;
}

esp_video_render_err_t esp_video_render_blend_transparent_color_sw(esp_video_render_fb_t *dst,
                                                                   esp_video_render_fb_t *src,
                                                                   esp_video_render_rect_t *dst_rect,
                                                                   esp_video_render_rect_t *src_rect,
                                                                   esp_video_render_clr_t *trans_color)
{
    if (!dst || !src || !dst->data || !src->data || !dst_rect || !src_rect || !trans_color) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (dst->info.format != src->info.format) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (dst_rect->width != src_rect->width || dst_rect->height != src_rect->height) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    uint8_t pixel_bytes = 2;
    uint32_t src_pitch = src->info.width * pixel_bytes;
    uint32_t dst_pitch = dst->info.width * pixel_bytes;
    uint8_t *src_ptr = src->data + src_rect->x * pixel_bytes + src_rect->y * src_pitch;
    uint8_t *dst_ptr = dst->data + dst_rect->x * pixel_bytes + dst_rect->y * dst_pitch;

    switch (dst->info.format) {
        case ESP_VIDEO_RENDER_FORMAT_RGB565:
            for (int i = 0; i < dst_rect->height; i++) {
                uint16_t *src_row = (uint16_t *)src_ptr;
                uint16_t *dst_row = (uint16_t *)dst_ptr;
                for (int j = 0; j < dst_rect->width; j++) {
                    if (!pixel_match_trans_color_rgb565(src_row[j], trans_color)) {
                        dst_row[j] = src_row[j];
                    }
                }
                src_ptr += src_pitch;
                dst_ptr += dst_pitch;
            }
            break;
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE:
            for (int i = 0; i < dst_rect->height; i++) {
                uint16_t *src_row = (uint16_t *)src_ptr;
                uint16_t *dst_row = (uint16_t *)dst_ptr;
                for (int j = 0; j < dst_rect->width; j++) {
                    uint16_t src_native = __builtin_bswap16(src_row[j]);
                    if (!pixel_match_trans_color_rgb565(src_native, trans_color)) {
                        dst_row[j] = src_row[j];
                    }
                }
                src_ptr += src_pitch;
                dst_ptr += dst_pitch;
            }
            break;
        default:
            return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_blend_fill_sw(esp_video_render_fb_t *dst,
                                                      esp_video_render_rect_t *dst_rect,
                                                      esp_video_render_clr_t *color)
{
    // Validate input parameters
    if (!dst || !dst->data || !dst_rect || !color) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    uint8_t pixel_bytes = 2;
    uint32_t dst_pitch = dst->info.width * pixel_bytes;
    uint8_t *dst_ptr = dst->data + dst_rect->x * pixel_bytes + dst_rect->y * dst_pitch;

    switch (dst->info.format) {
        case ESP_VIDEO_RENDER_FORMAT_RGB565: {
            // Convert RGB888 color to RGB565 format
            uint16_t rgb565_color = RGB565_PACK((color->r >> 3), (color->g >> 2), (color->b >> 3));

            for (int i = 0; i < dst_rect->height; i++) {
                uint16_t *dst_row = (uint16_t *)dst_ptr;
                // Fill entire row with the color
                for (int j = 0; j < dst_rect->width; j++) {
                    dst_row[j] = rgb565_color;
                }
                dst_ptr += dst_pitch;
            }
            break;
        }
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE: {
            // Convert RGB888 color to RGB565 format and byte swap for big endian
            uint16_t rgb565_color = __builtin_bswap16(RGB565_PACK((color->r >> 3), (color->g >> 2), (color->b >> 3)));

            for (int i = 0; i < dst_rect->height; i++) {
                uint16_t *dst_row = (uint16_t *)dst_ptr;
                // Fill entire row with the color
                for (int j = 0; j < dst_rect->width; j++) {
                    dst_row[j] = rgb565_color;
                }
                dst_ptr += dst_pitch;
            }
            break;
        }
        default:
            return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}
