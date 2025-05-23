
/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdlib.h>
#include "esp_capture_text_overlay.h"
#include "capture_os.h"
#include "esp_painter_font.h"
#include "esp_log.h"

#define TAG "TEXT_OVERLAY"

#define RGN_OVERFLOW(base, rgn) ((rgn)->x + (rgn)->width > base->width || (rgn)->y + (rgn)->height > base->height)

typedef struct {
    esp_capture_overlay_if_t    base;
    esp_capture_format_id_t     format_id;
    esp_capture_rgn_t           rgn;
    esp_capture_stream_frame_t  frame;
    capture_mutex_handle_t      mutex;
    bool                        opened;
    uint8_t                     alpha;
} text_overlay_t;

static esp_capture_err_t text_overlay_close(esp_capture_overlay_if_t *h);

const esp_painter_basic_font_t *get_font(uint16_t font_size)
{
    switch (font_size) {
        default:
            return NULL;
#if CONFIG_ESP_PAINTER_BASIC_FONT_12
        case 12:
            return &esp_painter_basic_font_12;
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_12 */
#if CONFIG_ESP_PAINTER_BASIC_FONT_16
        case 16:
            return &esp_painter_basic_font_16;
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_16 */
#if CONFIG_ESP_PAINTER_BASIC_FONT_20
        case 20:
            return &esp_painter_basic_font_20;
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_20 */
#if CONFIG_ESP_PAINTER_BASIC_FONT_24
        case 24:
            return &esp_painter_basic_font_24;
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_24 */
#if CONFIG_ESP_PAINTER_BASIC_FONT_28
        case 28:
            return &esp_painter_basic_font_28;
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_28 */
#if CONFIG_ESP_PAINTER_BASIC_FONT_32
        case 32:
            return &esp_painter_basic_font_32;
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_32 */
#if CONFIG_ESP_PAINTER_BASIC_FONT_36
        case 36:
            return &esp_painter_basic_font_36;
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_36 */
#if CONFIG_ESP_PAINTER_BASIC_FONT_40
        case 40:
            return &esp_painter_basic_font_40;
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_40 */
#if CONFIG_ESP_PAINTER_BASIC_FONT_44
        case 44:
            return &esp_painter_basic_font_44;
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_44 */
#if CONFIG_ESP_PAINTER_BASIC_FONT_48
        case 48:
            return &esp_painter_basic_font_48;
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_48 */
    }
}

static esp_capture_err_t text_overlay_open(esp_capture_overlay_if_t *h)
{
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    do {
        capture_mutex_create(&text_overlay->mutex);
        if (text_overlay->mutex == NULL) {
            break;
        }
        text_overlay->format_id = ESP_CAPTURE_FMT_ID_RGB565;
        text_overlay->frame.stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
        text_overlay->frame.size = text_overlay->rgn.width * text_overlay->rgn.height * 2;
        text_overlay->frame.data = capture_calloc(1, text_overlay->frame.size);
        if (text_overlay->frame.data == NULL) {
            break;
        }
        text_overlay->opened = true;
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    text_overlay_close(h);
    return ESP_CAPTURE_ERR_NO_RESOURCES;
}

static esp_capture_err_t text_overlay_get_region(esp_capture_overlay_if_t *h, esp_capture_format_id_t *codec, esp_capture_rgn_t *rgn)
{
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    if (text_overlay->opened == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    *codec = text_overlay->format_id;
    *rgn = text_overlay->rgn;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t text_overlay_get_frame(esp_capture_overlay_if_t *h, esp_capture_stream_frame_t *frame)
{
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    if (text_overlay->opened == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    capture_mutex_lock(text_overlay->mutex, CAPTURE_MAX_LOCK_TIME);
    *frame = text_overlay->frame;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t text_overlay_release_frame(esp_capture_overlay_if_t *h, esp_capture_stream_frame_t *frame)
{
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    if (text_overlay->opened == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    capture_mutex_unlock(text_overlay->mutex);
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_text_overlay_draw_start(esp_capture_overlay_if_t *h)
{
    if (h == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    if (text_overlay->opened == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    capture_mutex_lock(text_overlay->mutex, CAPTURE_MAX_LOCK_TIME);
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_text_overlay_clear(esp_capture_overlay_if_t *h, esp_capture_rgn_t *rgn, uint16_t color)
{
    if (h == NULL || rgn == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    esp_capture_rgn_t *base = &text_overlay->rgn;
    if (text_overlay->opened == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    if (RGN_OVERFLOW(base, rgn)) {
        ESP_LOGE(TAG, "Region overflow");
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    bool pure_color = (color >> 8) == (color & 0xFF);
    if (pure_color) {
        uint8_t color_l = color & 0xFF;
        uint16_t *v = (uint16_t *)text_overlay->frame.data;
        v += (rgn->y * text_overlay->rgn.width + rgn->x);
        for (int i = 0; i < rgn->height; i++) {
            memset(v, color_l, rgn->width * 2);
            v += text_overlay->rgn.width;
        }
    } else {
        uint16_t *v = (uint16_t *)text_overlay->frame.data;
        v += (rgn->y * text_overlay->rgn.width + rgn->x);
        for (int i = 0; i < rgn->height; i++) {
            uint16_t *col = v;
            for (int j = 0; j < rgn->width; j++) {
                *(col++) = color;
            }
            v += text_overlay->rgn.width;
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_text_overlay_draw_text(esp_capture_overlay_if_t *h, esp_capture_text_overlay_draw_info_t *info, char *str)
{
    if (h == NULL || info == NULL || str == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    if (text_overlay->opened == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }

    const esp_painter_basic_font_t *font = get_font(info->font_size);
    if (font == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    uint32_t x = info->x;
    uint32_t y = info->y;
    uint16_t font_w = font->width;
    uint16_t font_h = font->height;
    uint16_t c_size = font->height * ((font->width + 7) / 8);

    if (x + font_w > text_overlay->rgn.width || y + font_h > text_overlay->rgn.height) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }

    uint16_t *dst = (uint16_t *)(text_overlay->frame.data) + (y * text_overlay->rgn.width + x);
    uint16_t *pixel = dst;

    while (*str) {
        if (*str == '\n' || x + font_w > text_overlay->rgn.width) {
            y += font_h;
            if (y + font_h > text_overlay->rgn.height) {
                break;
            }
            x = info->x;
            dst = (uint16_t *)(text_overlay->frame.data) + (y * text_overlay->rgn.width + x);
            str++;
            continue;
        }
        pixel = dst;
        uint16_t c_offset = (*str - ' ') * c_size;
        const uint8_t *p_c = &font->bitmap[c_offset];
        uint16_t *cur = pixel;
        int x0 = 0;
        for (int i = 0; i < c_size; i++) {
            uint8_t temp = p_c[i];
            for (int j = 0; j < 8; j++) {
                if (temp & 0x80) {
                    cur[x0] = info->color;
                }
                temp <<= 1;
                x0++;
                if (x0 == font_w) {
                    x0 = 0;
                    pixel += text_overlay->rgn.width;
                    cur = pixel;
                    break;
                }
            }
        }
        dst += font_w;
        x += font_w;
        str++;
    }
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_text_overlay_draw_text_fmt(esp_capture_overlay_if_t *h, esp_capture_text_overlay_draw_info_t *info,
                                                         const char *fmt, ...)
{
    if (h == NULL || info == NULL || fmt == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    if (text_overlay->opened == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    char buffer[CONFIG_ESP_PAINTER_FORMAT_SIZE_MAX];
    va_list args;
    int ret;
    va_start(args, fmt);
    ret = vsnprintf(buffer, CONFIG_ESP_PAINTER_FORMAT_SIZE_MAX, fmt, args);
    va_end(args);
    if (ret < 0) {
        return ESP_CAPTURE_ERR_NOT_ENOUGH;
    }
    return esp_capture_text_overlay_draw_text(h, info, buffer);
}

esp_capture_err_t esp_capture_text_overlay_draw_finished(esp_capture_overlay_if_t *h)
{
    if (h == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    if (text_overlay->opened == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    capture_mutex_unlock(text_overlay->mutex);
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t text_overlay_set_alpha(esp_capture_overlay_if_t *h, uint8_t alpha)
{
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    text_overlay->alpha = alpha;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t text_overlay_get_alpha(esp_capture_overlay_if_t *h, uint8_t *alpha)
{
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    *alpha = text_overlay->alpha;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t text_overlay_close(esp_capture_overlay_if_t *h)
{
    text_overlay_t *text_overlay = (text_overlay_t *)h;
    if (text_overlay->mutex) {
        capture_mutex_lock(text_overlay->mutex, CAPTURE_MAX_LOCK_TIME);
    }
    if (text_overlay->frame.data) {
        capture_free(text_overlay->frame.data);
        text_overlay->frame.data = NULL;
    }
    if (text_overlay->mutex) {
        capture_mutex_unlock(text_overlay->mutex);
        capture_mutex_destroy(text_overlay->mutex);
        text_overlay->mutex = NULL;
    }
    text_overlay->opened = false;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_overlay_if_t *esp_capture_new_text_overlay(esp_capture_rgn_t *rgn)
{
    text_overlay_t *text_overlay = capture_calloc(1, sizeof(text_overlay_t));
    if (text_overlay == NULL) {
        return NULL;
    }
    text_overlay->base.open = text_overlay_open;
    text_overlay->base.get_overlay_region = text_overlay_get_region;
    text_overlay->base.set_alpha = text_overlay_set_alpha;
    text_overlay->base.get_alpha = text_overlay_get_alpha;
    text_overlay->base.acquire_frame = text_overlay_get_frame;
    text_overlay->base.release_frame = text_overlay_release_frame;
    text_overlay->base.close = text_overlay_close;
    text_overlay->rgn = *rgn;
    return &text_overlay->base;
}
