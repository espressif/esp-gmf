/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_vui_widget.h"
#include "esp_vui_container.h"
#include "esp_log.h"

#define TAG  "VIDEO_WIDGET"

esp_video_render_err_t esp_vui_container_get_pool(esp_vui_container_handle_t container, void **pool);
esp_video_render_err_t esp_vui_container_get_blender(esp_vui_container_handle_t container,
                                                     esp_video_render_blend_handle_t *blender);

esp_video_render_err_t esp_vui_widget_destroy(esp_vui_widget_t *widget)
{
    if (widget == NULL || widget->ops == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    // When remove from container it will auto call destroy
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    if (widget->container) {
        ret = esp_vui_container_remove_widget(widget->container, widget);
    } else {
        if (widget->ops->destroy) {
            widget->ops->destroy(widget);
        }
    }
    return ret;
}

esp_video_render_err_t esp_vui_widget_set_visible(esp_vui_widget_t *widget, bool visible)
{
    if (widget == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_vui_container_compose_lock(widget->container);
    widget->dirty = widget->rect;
    widget->visible = visible;
    ESP_LOGI(TAG, "set_visible: widget=%p visible=%d dirty %d-%d %dx%d", (void *)widget, visible,
             widget->dirty.x, widget->dirty.y, widget->dirty.width, widget->dirty.height);
    esp_video_render_err_t ret = esp_vui_container_notify_compose_changed(widget->container, &widget->dirty, visible ? true : false);
    esp_vui_container_compose_unlock(widget->container);
    return ret;
}

esp_video_render_err_t esp_vui_widget_set_pos(esp_vui_widget_t *widget, esp_video_render_pos_t *pos)
{
    if (widget == NULL || pos == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    const esp_video_render_rect_t *rect = NULL;
    esp_vui_container_get_disp_rect(widget->container, &rect);
    if (rect == NULL) {
        return ESP_VIDEO_RENDER_ERR_NOT_FOUND;
    }
    if (pos->x >= rect->width || pos->y >= rect->height) {
        ESP_LOGW(TAG, "Move out of container x:%d y:%d", pos->x, pos->y);
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    esp_vui_container_compose_lock(widget->container);
    // Remove old
    esp_vui_container_notify_compose_changed(widget->container, &widget->rect, false);
    widget->rect.x = pos->x;
    widget->rect.y = pos->y;
    widget->dirty = widget->rect;
    // Add new
    esp_vui_container_notify_compose_changed(widget->container, &widget->dirty, true);
    ESP_LOGI(TAG, "set_pos: widget=%p -> %d,%d", (void *)widget, widget->rect.x, widget->rect.y);
    esp_vui_container_compose_unlock(widget->container);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_widget_get_pool(esp_vui_widget_t *widget, void **pool)
{
    if (widget == NULL || pool == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    return esp_vui_container_get_pool(widget->container, pool);
}

esp_video_render_err_t esp_vui_widget_get_blender(esp_vui_widget_t *widget,
                                                  esp_video_render_blend_handle_t *blender)
{
    if (widget == NULL || blender == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    return esp_vui_container_get_blender(widget->container, blender);
}
