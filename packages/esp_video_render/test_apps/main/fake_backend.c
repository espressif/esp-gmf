/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_video_render_backend.h"
#include "esp_video_render_types.h"
#include "esp_video_render_log.h"
#include "video_render_utils.h"
#include "video_render_sys.h"
#include "esp_log.h"

#define TAG  "FAKE_BACKEND"

typedef struct {
    video_render_mutex_handle_t  lock;
    esp_video_render_fb_t        fb;
} fake_backend_t;

static esp_video_render_err_t fake_backend_deinit(esp_video_render_backend_handle_t backend);

static esp_video_render_err_t fake_backend_init(void *cfg, int cfg_size, esp_video_render_backend_handle_t *out)
{
    if (!cfg || cfg_size != (int)sizeof(esp_video_render_lcd_cfg_t) || !out) {
        ESP_LOGE(TAG, "Invalid argument cfg:%p cfg_size:%d out:%p", cfg, cfg_size, out);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_video_render_lcd_cfg_t *c = (esp_video_render_lcd_cfg_t *)cfg;
    *out = NULL;
    if (c->width == 0 || c->height == 0) {
        ESP_LOGE(TAG, "Invalid argument width:%d height:%d", c->width, c->height);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    fake_backend_t *bk = (fake_backend_t *)video_render_calloc(1, sizeof(*bk));
    do {
        if (bk == NULL) {
            return ESP_VIDEO_RENDER_ERR_NO_MEM;
        }
        bk->lock = video_render_mutex_create();
        if (bk->lock == NULL) {
            break;
        }
        bk->fb.info.format = c->out_format ? c->out_format : ESP_VIDEO_RENDER_FORMAT_RGB565;
        bk->fb.info.width = c->width;
        bk->fb.info.height = c->height;
        bk->fb.size = (uint32_t)(c->width * c->height * video_render_get_pixel_bits(bk->fb.info.format) >> 3);  // RGB565
        bk->fb.data = (uint8_t *)video_render_malloc_align(bk->fb.size, 64);
        if (bk->fb.data == NULL) {
            break;
        }
        memset(bk->fb.data, 0, bk->fb.size);
        *out = (esp_video_render_backend_handle_t)bk;
        return ESP_VIDEO_RENDER_ERR_OK;
    } while (0);
    fake_backend_deinit(bk);
    *out = NULL;
    return ESP_VIDEO_RENDER_ERR_NO_MEM;
}

static bool fake_backend_with_gram(esp_video_render_backend_handle_t backend)
{
    (void)backend;
    return true;
}

static esp_video_render_err_t fake_backend_get_display_info(esp_video_render_backend_handle_t backend,
                                                            esp_video_render_disp_info_t *info)
{
    if (!backend || !info) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    fake_backend_t *bk = (fake_backend_t *)backend;
    info->format = bk->fb.info.format;
    info->width = bk->fb.info.width;
    info->height = bk->fb.info.height;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t fake_backend_get_fb(esp_video_render_backend_handle_t backend, esp_video_render_fb_t *fb)
{
    if (!backend || !fb) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    fake_backend_t *bk = (fake_backend_t *)backend;
    *fb = bk->fb;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t fake_backend_lock_fb(esp_video_render_backend_handle_t backend,
                                                   esp_video_render_fb_t *fb, bool lock)
{
    (void)fb;
    if (!backend) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    fake_backend_t *bk = (fake_backend_t *)backend;
    if (lock) {
        video_render_mutex_lock(bk->lock, VIDEO_RENDER_MAX_LOCK_TIME);
    } else {
        video_render_mutex_unlock(bk->lock);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t fake_backend_write_fb(esp_video_render_backend_handle_t backend,
                                                    esp_video_render_fb_t *fb,
                                                    const esp_video_render_rect_t *r,
                                                    const esp_video_render_pos_t *pos)
{
    if (backend == NULL || fb == NULL || fb->data == NULL) {
        ESP_LOGE(TAG, "Invalid argument backend:%p fb:%p", backend, fb);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    fake_backend_t *bk = (fake_backend_t *)backend;
    if (fb->info.format != bk->fb.info.format) {
        ESP_LOGE(TAG, "Unsupported format: %d", (int)fb->info.format);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_video_render_rect_t rect = {
        .width = fb->info.width,
        .height = fb->info.height,
    };
    const esp_video_render_rect_t *dirty_rect = r ? r : &rect;
    if (fb->data == bk->fb.data) {
        video_render_delay(10);
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    uint16_t x = pos ? pos->x : 0;
    uint16_t y = pos ? pos->y : 0;
    if (x + dirty_rect->width > bk->fb.info.width || y + dirty_rect->height > bk->fb.info.height) {
        ESP_LOGE(TAG, "Dirty rect out of bounds: %d-%d %dx%d", x, y, dirty_rect->width, dirty_rect->height);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    uint32_t src_pitch = fb->info.width * 2;
    uint32_t dst_pitch = bk->fb.info.width * 2;
    uint8_t *src = fb->data + dirty_rect->y * src_pitch + dirty_rect->x * 2;
    uint8_t *dst = bk->fb.data + y * dst_pitch + x * 2;
    for (int i = 0; i < dirty_rect->height; i++) {
        memcpy(dst, src, dirty_rect->width * 2);
        src += src_pitch;
        dst += dst_pitch;
    }
    video_render_delay(10);
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t fake_backend_deinit(esp_video_render_backend_handle_t backend)
{
    if (backend == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    fake_backend_t *bk = (fake_backend_t *)backend;
    // Free framebuffer
    if (bk->fb.data) {
        video_render_free(bk->fb.data);
        bk->fb.data = NULL;
    }
    // Destroy mutex
    if (bk->lock) {
        video_render_mutex_destroy(bk->lock);
        bk->lock = NULL;
    }
    video_render_free(bk);
    return ESP_VIDEO_RENDER_ERR_OK;
}

const esp_video_render_backend_ops_t *video_render_get_fake_backend(void)
{
    static const esp_video_render_backend_ops_t ops = {
        .init = fake_backend_init,
        .with_gram = fake_backend_with_gram,
        .get_display_info = fake_backend_get_display_info,
        .get_fb = fake_backend_get_fb,
        .lock_fb = fake_backend_lock_fb,
        .write_fb = fake_backend_write_fb,
        .deinit = fake_backend_deinit,
    };
    return &ops;
}
