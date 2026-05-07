/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_vui_widget_default.h"
#include "video_render_utils.h"
#include "esp_video_render_blender.h"
#include "esp_log.h"

#define TAG  "IMG_WIDGET"

typedef struct image_widget_ctx {
    esp_vui_widget_t        widget;
    esp_video_render_fb_t   image;
    bool                    gen_fb;
    bool                    fresh;
    bool                    use_transparent_color;
    esp_video_render_clr_t  trans_color;
} image_widget_ctx_t;

static esp_video_render_err_t image_widget_redraw(esp_vui_widget_t *self, esp_video_render_fb_t *dst_fb,
                                                  const esp_video_render_rect_t *dst_rect,
                                                  const esp_video_render_rect_t *dirty)
{
    image_widget_ctx_t *img = (image_widget_ctx_t *)self;
    if (self == NULL || dst_fb == NULL || img->image.data == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    if (dirty &&
        ((dirty->width == 0 || dirty->height == 0) ||
         (dirty->x >= img->image.info.width) || (dirty->y >= img->image.info.height))) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    esp_video_render_rect_t src;
    // Full redraw
    if (dirty == NULL) {
        src.x = 0;
        src.y = 0;
        src.width = img->image.info.width;
        src.height = img->image.info.height;
    } else {
        src.x = dirty->x;
        src.y = dirty->y;
        src.width = src.x + dirty->width > img->image.info.width ? img->image.info.width - src.x : dirty->width;
        src.height = src.y + dirty->height > img->image.info.height ? img->image.info.height - src.y : dirty->height;
    }
    esp_video_render_rect_t dst;
    dst.x = dst_rect->x;
    dst.y = dst_rect->y;
    dst.width = src.width;
    dst.height = src.height;
    ESP_LOGD(TAG, "redraw img=%d fresh=%d dirty=%p src %d-%d %dx%d -> dst %d-%d %dx%d vis=%d", img->widget.id,
             img->fresh, (void *)dirty, src.x, src.y, src.width, src.height,
             dst.x, dst.y, dst.width, dst.height, self->visible);
    if (dst.x + dst.width > dst_fb->info.width || dst.y + dst.height > dst_fb->info.height) {
        ESP_LOGE(TAG, "dst out of bounds %d-%d %dx%d > %dx%d", dst.x, dst.y, dst.width, dst.height, dst_fb->info.width, dst_fb->info.height);
        return 0;
    }
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    esp_video_render_blend_handle_t blender = NULL;
    ret = esp_vui_widget_get_blender(self, &blender);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        return ret;
    }
    if (img->use_transparent_color) {
        ret = esp_video_render_blend_transparent_color(blender, dst_fb, &img->image, &dst, &src, &img->trans_color);
    } else {
        ret = esp_video_render_blend_bitblt(blender, dst_fb, &img->image, &dst, &src);
    }
    // Fresh flag is cleared by container after re-draw
    return ret;
}

static void image_widget_destroy(esp_vui_widget_t *self)
{
    if (self == NULL) {
        return;
    }
    image_widget_ctx_t *img = (image_widget_ctx_t *)self;
    if (img->gen_fb) {
        if (img->image.data) {
            video_render_free(img->image.data);
            img->image.data = NULL;
        }
        img->gen_fb = false;
    }
    video_render_free(img);
}

esp_vui_widget_t *esp_vui_image_widget_init(esp_vui_container_handle_t container,
                                            esp_video_render_img_t *image,
                                            const esp_video_render_pos_t *pos)
{
    if (container == NULL || image == NULL || pos == NULL || image->data == NULL || image->size == 0) {
        return NULL;
    }
    image_widget_ctx_t *img = (image_widget_ctx_t *)video_render_calloc(1, sizeof(image_widget_ctx_t));
    if (img == NULL) {
        return NULL;
    }
    static const esp_vui_widget_ops_t image_ops = {
        .redraw = image_widget_redraw,
        .destroy = image_widget_destroy,
    };
    static int image_id = 0;
    img->widget.id = image_id++;
    // TODO need add decoder support afterwards
    img->image.info = image->info;
    img->image.data = image->data;
    img->image.size = image->size;
    img->widget.ops = &image_ops;

    // Initialize widget rect to match image rect at position
    img->widget.rect.x = pos->x;
    img->widget.rect.y = pos->y;
    img->widget.rect.width = img->image.info.width;
    img->widget.rect.height = img->image.info.height;
    img->widget.dirty = img->widget.rect;
    img->fresh = true;  // Mark as fresh so it renders on first redraw

    ESP_LOGI(TAG, "create img=%p size=%dx%d pos=%d,%d widget.rect %d-%d %dx%d", (void *)img,
             img->image.info.width, img->image.info.height,
             pos->x, pos->y,
             img->widget.rect.x, img->widget.rect.y, img->widget.rect.width, img->widget.rect.height);

    if (esp_vui_container_add_widget(container, &img->widget) != ESP_VIDEO_RENDER_ERR_OK) {
        video_render_free(img);
        return NULL;
    }
    return &img->widget;
}

esp_video_render_err_t esp_vui_image_widget_set_transparent_color(esp_vui_widget_t *widget,
                                                                  bool enable,
                                                                  esp_video_render_clr_t *trans_color)
{
    if (widget == NULL || widget->container == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    image_widget_ctx_t *img = (image_widget_ctx_t *)widget;
    if (enable && trans_color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_compose_lock(widget->container);
    img->use_transparent_color = enable;
    if (trans_color) {
        img->trans_color = *trans_color;
    }
    widget->dirty = widget->rect;
    esp_video_render_err_t ret = esp_vui_container_notify_compose_changed(widget->container, &widget->dirty, enable);
    esp_vui_container_compose_unlock(widget->container);
    return ret;
}
