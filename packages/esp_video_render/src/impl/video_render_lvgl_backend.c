/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <sdkconfig.h>
#include <string.h>

#ifdef CONFIG_ESP_VIDEO_RENDER_LVGL_BACKEND_SUPPORT
#include "esp_video_render_backend.h"
#include "video_render_sys.h"
#include "esp_video_render_log.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "video_render_utils.h"

#define TAG  "LVGL_BACKEND"

typedef struct {
    lv_disp_t              *disp;
    lv_obj_t               *img;
    lv_img_dsc_t            dsc;
    uint8_t                *buf;
    esp_video_render_fb_t   fb;
    esp_video_render_pos_t  last_pos;
    bool                    src_bound;
} lvgl_backend_t;

static esp_video_render_err_t lvgl_backend_deinit(esp_video_render_backend_handle_t backend);

static esp_video_render_err_t lvgl_backend_ensure_fb(lvgl_backend_t *bk)
{
    if (bk->buf) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    bk->buf = (uint8_t *)video_render_malloc_align(bk->fb.size, 64);
    if (bk->buf == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    bk->fb.data = bk->buf;
    bk->dsc.data = bk->buf;
    bk->src_bound = false;  // force bind on next write_fb (under LVGL lock)
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t lvgl_backend_init(void *cfg, int cfg_size, esp_video_render_backend_handle_t *out)
{
    esp_video_render_lvgl_cfg_t *lvgl_cfg = (esp_video_render_lvgl_cfg_t *)cfg;
    if (cfg == NULL || out == NULL || cfg_size < (int)sizeof(esp_video_render_lvgl_cfg_t) || lvgl_cfg->lv_disp == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    lvgl_backend_t *bk = (lvgl_backend_t *)video_render_calloc(1, sizeof(lvgl_backend_t));
    if (bk == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    bk->disp = (lv_disp_t *)lvgl_cfg->lv_disp;
    lvgl_port_lock(0);
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    do {
        lv_obj_set_style_bg_opa(lv_disp_get_scr_act(bk->disp), LV_OPA_TRANSP, LV_PART_MAIN);
        bk->img = lv_img_create(lv_disp_get_scr_act(bk->disp));
        if (bk->img == NULL) {
            ESP_LOGE(TAG, "Failed to create image object");
            ret = ESP_VIDEO_RENDER_ERR_NO_MEM;
            break;
        }
        // Set image size to match framebuffer size
        lv_obj_set_size(bk->img, lvgl_cfg->width, lvgl_cfg->height);
        // Initially position at (0,0) - will be updated by write_fb if pos is provided
        lv_obj_set_pos(bk->img, 0, 0);
    } while (0);
    lvgl_port_unlock();
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        lvgl_backend_deinit((esp_video_render_backend_handle_t)bk);
        return ret;
    }
    bk->fb.info.format = lvgl_cfg->out_format;
    bk->fb.info.width = lvgl_cfg->width;
    bk->fb.info.height = lvgl_cfg->height;
    bk->fb.size = video_render_get_image_size(&bk->fb.info);
    // Initialize LVGL image descriptor
    bk->dsc.header.w = lvgl_cfg->width;
    bk->dsc.header.h = lvgl_cfg->height;
    // TODO currently not add alpha support yet
#if LVGL_VERSION_MAJOR >= 9
    bk->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    if (bk->fb.info.format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
        bk->dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
    }
#else
    bk->dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
#endif  /* LVGL_VERSION_MAJOR >= 9 */
    bk->dsc.data_size = bk->fb.size;
    // Lazy allocation: allocate framebuffer on first get_fb().
    bk->buf = NULL;
    bk->fb.data = NULL;
    bk->dsc.data = NULL;
    bk->src_bound = false;

    *out = (esp_video_render_backend_handle_t)bk;
    ESP_LOGI(TAG, "LVGL backend created %dx%d", lvgl_cfg->width, lvgl_cfg->height);
    return ESP_VIDEO_RENDER_ERR_OK;
}

static bool lvgl_backend_with_gram(esp_video_render_backend_handle_t backend)
{
    (void)backend;
    return true;
}

static esp_video_render_err_t lvgl_backend_get_display_info(esp_video_render_backend_handle_t backend, esp_video_render_disp_info_t *info)
{
    if (backend == NULL || info == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    lvgl_backend_t *bk = (lvgl_backend_t *)backend;
    info->format = bk->fb.info.format;
    info->width = bk->fb.info.width;
    info->height = bk->fb.info.height;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t lvgl_backend_get_fb(esp_video_render_backend_handle_t backend, esp_video_render_fb_t *fb)
{
    if (backend == NULL || fb == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    lvgl_backend_t *bk = (lvgl_backend_t *)backend;
    esp_video_render_err_t ret = lvgl_backend_ensure_fb(bk);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        return ret;
    }
    *fb = bk->fb;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t lvgl_backend_lock_fb(esp_video_render_backend_handle_t backend, esp_video_render_fb_t *fb, bool lock)
{
    (void)backend;
    (void)fb;
    // Serialize access to the framebuffer with LVGL's refresh thread.
    // When locked, LVGL won't refresh/read the buffer while the render pipeline is blending into it.
    if (lock) {
        lvgl_port_lock(0);
    } else {
        lvgl_port_unlock();
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t lvgl_backend_write_fb(esp_video_render_backend_handle_t backend,
                                                    esp_video_render_fb_t *fb,
                                                    const esp_video_render_rect_t *dirty_rect,
                                                    const esp_video_render_pos_t *pos)
{
    if (backend == NULL || fb == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    lvgl_backend_t *bk = (lvgl_backend_t *)backend;
    // Validate FB size
    if (fb->size != bk->fb.size || fb->info.width != bk->fb.info.width || fb->info.height != bk->fb.info.height) {
        ESP_LOGE(TAG, "Bad FB size");
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (fb->data == NULL) {
        // Nothing to display yet (buffer not allocated or not set)
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    lvgl_port_lock(0);
    if (fb->data != bk->fb.data) {
        ret = lvgl_backend_ensure_fb(bk);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            lvgl_port_unlock();
            return ret;
        }
        memcpy(bk->fb.data, fb->data, bk->fb.size);
    }

    // Validate position if provided
    if (pos && (pos->x != bk->last_pos.x || pos->y != bk->last_pos.y)) {
        // Get display dimensions
        uint16_t disp_width = lv_disp_get_hor_res(bk->disp);
        uint16_t disp_height = lv_disp_get_ver_res(bk->disp);
        // Check if position + width/height is within display bounds
        if (pos->x + fb->info.width > disp_width || pos->y + fb->info.height > disp_height) {
            ESP_LOGE(TAG, "Position out of bounds: pos(%d,%d) + size(%dx%d) exceeds display(%dx%d)",
                     pos->x, pos->y, fb->info.width, fb->info.height, disp_width, disp_height);
            lvgl_port_unlock();
            return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
        }
        lv_obj_set_pos(bk->img, pos->x, pos->y);
        bk->last_pos.x = pos->x;
        bk->last_pos.y = pos->y;
    }
    // Keep LVGL bound to a stable image descriptor and only invalidate the updated area.
    if (bk->src_bound == false || bk->dsc.data != bk->fb.data) {
        bk->dsc.data = bk->fb.data;
        lv_img_set_src(bk->img, &bk->dsc);
        bk->src_bound = true;
    }

#if LVGL_VERSION_MAJOR >= 8
    lv_area_t a;
    if (dirty_rect && dirty_rect->width && dirty_rect->height) {
        a.x1 = bk->last_pos.x + dirty_rect->x;
        a.y1 = bk->last_pos.y + dirty_rect->y;
        a.x2 = a.x1 + dirty_rect->width - 1;
        a.y2 = a.y1 + dirty_rect->height - 1;
        lv_obj_invalidate_area(bk->img, &a);
    } else {
        lv_obj_invalidate(bk->img);
    }
#else
    // Fallback: invalidate whole object if area-invalidate API is not available.
    lv_obj_invalidate(bk->img);
#endif  /* LVGL_VERSION_MAJOR >= 8 */
    lvgl_port_unlock();
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t lvgl_backend_deinit(esp_video_render_backend_handle_t backend)
{
    if (backend == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    lvgl_backend_t *bk = (lvgl_backend_t *)backend;
    // Delete image object
    lvgl_port_lock(0);
    if (bk->img) {
        lv_obj_del(bk->img);
        bk->img = NULL;
    }
    lvgl_port_unlock();
    if (bk->buf) {
        video_render_free(bk->buf);
        bk->buf = NULL;
    }
    video_render_free(bk);
    return ESP_VIDEO_RENDER_ERR_OK;
}

const esp_video_render_backend_ops_t *esp_video_render_get_lvgl_backend(void)
{
    static const esp_video_render_backend_ops_t ops = {
        .init = lvgl_backend_init,
        .with_gram = lvgl_backend_with_gram,
        .get_display_info = lvgl_backend_get_display_info,
        .get_fb = lvgl_backend_get_fb,
        .lock_fb = lvgl_backend_lock_fb,
        .write_fb = lvgl_backend_write_fb,
        .deinit = lvgl_backend_deinit,
    };
    return &ops;
}
#endif  /* CONFIG_ESP_VIDEO_RENDER_LVGL_BACKEND_SUPPORT */
