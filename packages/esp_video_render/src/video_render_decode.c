/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>
#include "esp_video_render.h"
#include "video_render_proc.h"
#include "video_render_utils.h"
#include "esp_log.h"

#define TAG  "VIDEO_RENDER_DEC"

static int image_decode_cb(esp_video_render_frame_t *frame, void *ctx)
{
    esp_video_render_frame_t *out_frame = (esp_video_render_frame_t *)ctx;
    *out_frame = *frame;
    out_frame->data = NULL;
    if (frame->data == NULL || frame->size == 0) {
        return ESP_VIDEO_RENDER_ERR_FAIL;
    }
    out_frame->data = video_render_malloc_align(frame->size, video_render_get_default_alignment());
    if (out_frame->data == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    memcpy(out_frame->data, frame->data, frame->size);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_video_render_decode_image(void *pool, esp_video_render_img_t *img,
                                                     esp_video_render_format_t prefer_fmt,
                                                     esp_video_render_frame_t *frame)
{
    video_render_proc_handle_t proc_handle = NULL;
    int ret = ESP_VIDEO_RENDER_ERR_OK;
    do {
        ret = video_render_proc_create(pool, &proc_handle);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create proc ret %d", ret);
            break;
        }
        frame->data = NULL;
        video_render_proc_set_writer(proc_handle, image_decode_cb, frame);
        esp_video_render_proc_type_t proc_type = ESP_VIDEO_RENDER_PROC_DEC;
        ret = video_render_proc_add(proc_handle, &proc_type, 1);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Fail to find decode ret %d", ret);
            break;
        }
        esp_video_render_frame_info_t dst_info = img->info;
        dst_info.format = prefer_fmt;
        ret = video_render_proc_open(proc_handle, &img->info, &dst_info);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open proc ret %d", ret);
            break;
        }
        esp_video_render_frame_t in_frame = {
            .format = img->info.format,
            .width = img->info.width,
            .height = img->info.height,
            .data = img->data,
            .size = img->size};
        ret = video_render_proc_write(proc_handle, &in_frame);
        if (ret != ESP_VIDEO_RENDER_ERR_OK || frame->data == NULL) {
            ESP_LOGE(TAG, "Failed to decode image ret %d", ret);
            ret = ESP_VIDEO_RENDER_ERR_FAIL;
            break;
        }
    } while (0);
    if (proc_handle) {
        video_render_proc_close(proc_handle);
        video_render_proc_destroy(proc_handle);
    }
    if (ret != ESP_VIDEO_RENDER_ERR_OK && frame->data) {
        free(frame->data);
        frame->data = NULL;
    }
    return ret;
}
