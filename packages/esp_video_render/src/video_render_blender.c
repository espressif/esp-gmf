/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_video_render_blender.h"
#include "video_render_sys.h"
#include "video_render_utils.h"
#include "video_blend_hw.h"
#include "esp_log.h"
#include "esp_video_render_log.h"
#include "video_render_measure.h"

#define TAG  "VIDEO_BLEND"

typedef struct {
    bool                         need_sw;
    video_render_blend_hw_ctx_t *hw;
} video_render_blend_t;

static bool force_sw_blend(esp_video_render_rect_t *rect)
{
    if (rect == NULL) {
        return true;
    }
    return ((uint32_t)rect->width * rect->height) <= (20 * 20);
}

static bool rect_in_fb(esp_video_render_fb_t *fb, esp_video_render_rect_t *rect)
{
    if (fb == NULL || rect == NULL) {
        return false;
    }
    if (rect->width == 0 || rect->height == 0) {
        return false;
    }
    if ((uint32_t)rect->x + rect->width > fb->info.width) {
        return false;
    }
    if ((uint32_t)rect->y + rect->height > fb->info.height) {
        return false;
    }
    return true;
}

static bool is_rgb565(esp_video_render_format_t format)
{
    return format == ESP_VIDEO_RENDER_FORMAT_RGB565 ||
           format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE;
}

esp_video_render_err_t esp_video_render_blend_open(esp_video_render_blend_cfg_t *cfg,
                                                   esp_video_render_blend_handle_t *handle)
{
    (void)cfg;
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_blend_t *blend = video_render_calloc(1, sizeof(video_render_blend_t));
    if (blend == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    if (video_render_blend_hw_open(&blend->hw) != ESP_VIDEO_RENDER_ERR_OK) {
        blend->hw = NULL;
        blend->need_sw = true;
    }
    *handle = (esp_video_render_blend_handle_t)blend;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_blend_process(esp_video_render_blend_handle_t handle,
                                                      esp_video_render_fb_t *dst, esp_video_render_fb_t *src,
                                                      esp_video_render_rect_t *dst_rect,
                                                      esp_video_render_rect_t *src_rect,
                                                      uint8_t global_alpha)
{
    if (handle == NULL || dst == NULL || src == NULL || dst->data == NULL || src->data == NULL) {
        ESP_LOGE(TAG, "Invalid argument handle=%p dst=%p src=%p", handle, dst, src);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (!rect_in_fb(dst, dst_rect) || !rect_in_fb(src, src_rect)) {
        ESP_LOGE(TAG, "Rect not in fb dst=%p dst_rect=%d-%d %dx%d src=%p src_rect=%d-%d %dx%d",
                 dst, dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height,
                 src, src_rect->x, src_rect->y, src_rect->width, src_rect->height);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (src_rect->width != dst_rect->width || src_rect->height != dst_rect->height) {
        ESP_LOGE(TAG, "src rect %dx%d != dst rect %dx%d", src_rect->width, src_rect->height,
                 dst_rect->width, dst_rect->height);
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    esp_video_render_err_t ret;
    if (!video_render_blend_hw_can_accel(dst, src, dst_rect, src_rect)) {
        MEASURE_BEGIN("Render", "BlendSW");
        ret = esp_video_render_blend_sw(dst, src, dst_rect, src_rect, global_alpha);
        MEASURE_END("Render", "BlendSW");
        return ret;
    }
    video_render_blend_t *blend = (video_render_blend_t *)handle;
    video_render_blend_hw_ctx_t *hw = blend->hw;
    MEASURE_BEGIN("Render", "BlendHW");
    ret = video_render_blend_hw_process(hw, dst, src, dst_rect, src_rect, global_alpha);
    if (ret == ESP_VIDEO_RENDER_ERR_OK) {
        MEASURE_END("Render", "BlendHW");
        return ret;
    }
    // Fallback to software blend
    MEASURE_BEGIN("Render", "BlendSW");
    ret = esp_video_render_blend_sw(dst, src, dst_rect, src_rect, global_alpha);
    MEASURE_END("Render", "BlendSW");
    return ret;
}

esp_video_render_err_t esp_video_render_blend_transparent_color(esp_video_render_blend_handle_t handle,
                                                                esp_video_render_fb_t *dst,
                                                                esp_video_render_fb_t *src,
                                                                esp_video_render_rect_t *dst_rect,
                                                                esp_video_render_rect_t *src_rect,
                                                                esp_video_render_clr_t *trans_color)
{
    if (dst == NULL || src == NULL || dst->data == NULL || src->data == NULL || trans_color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (dst->info.format != src->info.format) {
        ESP_LOGE(TAG, "Format not match %s != %s",
                 video_render_format_to_string(dst->info.format),
                 video_render_format_to_string(src->info.format));
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    if (!rect_in_fb(dst, dst_rect) || !rect_in_fb(src, src_rect)) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (src_rect->width != dst_rect->width || src_rect->height != dst_rect->height) {
        ESP_LOGE(TAG, "src rect %dx%d != dst rect %dx%d", src_rect->width, src_rect->height,
                 dst_rect->width, dst_rect->height);
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    video_render_blend_t *blend = (video_render_blend_t *)handle;
    video_render_blend_hw_ctx_t *hw = blend ? blend->hw : NULL;
    esp_video_render_err_t ret;
    if (!video_render_blend_hw_can_accel(dst, src, dst_rect, src_rect)) {
        MEASURE_BEGIN("Render", "TransSw");
        ret = esp_video_render_blend_transparent_color_sw(dst, src, dst_rect, src_rect, trans_color);
        MEASURE_END("Render", "TransSw");
        return ret;
    }
    MEASURE_BEGIN("Render", "TransHW");
    ret = video_render_blend_hw_transparent_color(hw, dst, src, dst_rect, src_rect, trans_color);
    if (ret == ESP_VIDEO_RENDER_ERR_OK) {
        MEASURE_END("Render", "TransHW");
        return ret;
    }
    // Fallback to software blend
    MEASURE_BEGIN("Render", "TransSw");
    ret = esp_video_render_blend_transparent_color_sw(dst, src, dst_rect, src_rect, trans_color);
    MEASURE_END("Render", "TransSw");
    return ret;
}

esp_video_render_err_t esp_video_render_blend_fill(esp_video_render_blend_handle_t handle,
                                                   esp_video_render_fb_t *dst,
                                                   esp_video_render_rect_t *dst_rect,
                                                   esp_video_render_clr_t *color)
{
    if (handle == NULL || dst == NULL || dst->data == NULL || color == NULL || !rect_in_fb(dst, dst_rect)) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_blend_t *blend = (video_render_blend_t *)handle;
    video_render_blend_hw_ctx_t *hw = blend ? blend->hw : NULL;
    esp_video_render_err_t ret;
    if (force_sw_blend(dst_rect) || !video_render_blend_hw_can_fill(dst, dst_rect)) {
        MEASURE_BEGIN("Render", "FillSW");
        ret = esp_video_render_blend_fill_sw(dst, dst_rect, color);
        MEASURE_END("Render", "FillSW");
        return ret;
    }
    MEASURE_BEGIN("Render", "FillHW");
    ret = video_render_blend_hw_fill(hw, dst, dst_rect, color);
    if (ret == ESP_VIDEO_RENDER_ERR_OK) {
        MEASURE_END("Render", "FillHW");
        return ret;
    }
    // Fallback to software fill
    MEASURE_BEGIN("Render", "FillSW");
    ret = esp_video_render_blend_fill_sw(dst, dst_rect, color);
    MEASURE_END("Render", "FillSW");
    return ret;
}

esp_video_render_err_t esp_video_render_blend_bitblt(esp_video_render_blend_handle_t handle,
                                                     esp_video_render_fb_t *dst,
                                                     esp_video_render_fb_t *src,
                                                     esp_video_render_rect_t *dst_rect,
                                                     esp_video_render_rect_t *src_rect)
{
    if (handle == NULL || dst == NULL || src == NULL || dst_rect == NULL || src_rect == NULL ||
        dst->data == NULL || src->data == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (src_rect->width != dst_rect->width || src_rect->height != dst_rect->height) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (!rect_in_fb(dst, dst_rect) || !rect_in_fb(src, src_rect)) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_blend_t *blend = (video_render_blend_t *)handle;
    video_render_blend_hw_ctx_t *hw = blend ? blend->hw : NULL;
    esp_video_render_err_t ret = video_render_blend_hw_bitblt(hw, dst, src, dst_rect, src_rect);
    if (ret == ESP_VIDEO_RENDER_ERR_OK) {
        return ret;
    }
    uint8_t pixel_bytes = video_render_get_pixel_bits(src->info.format) / 8;
    if (pixel_bytes == 0) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    int src_line_size = src->info.width * pixel_bytes;
    int dst_line_size = dst->info.width * pixel_bytes;
    int copy_size = src_rect->width * pixel_bytes;
    uint8_t *src_ptr = src->data + src_rect->x * pixel_bytes + src_rect->y * src_line_size;
    uint8_t *dst_ptr = dst->data + dst_rect->x * pixel_bytes + dst_rect->y * dst_line_size;
    if (src->info.format == dst->info.format) {
        for (int i = 0; i < src_rect->height; i++) {
            memcpy(dst_ptr, src_ptr, copy_size);
            src_ptr += src_line_size;
            dst_ptr += dst_line_size;
        }
    } else if (is_rgb565(src->info.format) && is_rgb565(dst->info.format)) {
        for (int i = 0; i < src_rect->height; i++) {
            uint16_t *src_row = (uint16_t *)src_ptr;
            uint16_t *dst_row = (uint16_t *)dst_ptr;
            for (int j = 0; j < src_rect->width; j++) {
                dst_row[j] = __builtin_bswap16(src_row[j]);
            }
            src_ptr += src_line_size;
            dst_ptr += dst_line_size;
        }
    } else {
        ESP_LOGE(TAG, "Format not match %s != %s",
                 video_render_format_to_string(dst->info.format),
                 video_render_format_to_string(src->info.format));
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_blend_close(esp_video_render_blend_handle_t handle)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    video_render_blend_t *blend = (video_render_blend_t *)handle;
    if (blend->hw) {
        video_render_blend_hw_close(blend->hw);
        blend->hw = NULL;
    }
    video_render_free(blend);
    return ESP_VIDEO_RENDER_ERR_OK;
}
