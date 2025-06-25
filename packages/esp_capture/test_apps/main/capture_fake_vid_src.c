/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "capture_fake_vid_src.h"
#include "esp_video_codec_utils.h"
#include "esp_log.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_cache.h"
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
#define TAG "FAKE_VID_SRC"

#define FAKE_VID_SRC_MAX_FB 3

typedef struct {
    esp_capture_video_src_if_t  base;
    esp_capture_video_info_t    vid_info;
    bool                        use_fixed_caps;
    uint8_t                    *fb[FAKE_VID_SRC_MAX_FB];
    uint32_t                    fb_size[FAKE_VID_SRC_MAX_FB];
    bool                        fb_used[FAKE_VID_SRC_MAX_FB];
    uint8_t                     cur_fb;
    uint8_t                     fb_count;
    bool                        is_open;
    bool                        is_start;
    bool                        nego_ok;
} fake_vid_src_t;

static esp_capture_err_t fake_vid_src_close(esp_capture_video_src_if_t *h)
{
    fake_vid_src_t *src = (fake_vid_src_t *)h;
    src->is_open = false;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_vid_src_open(esp_capture_video_src_if_t *h)
{
    fake_vid_src_t *src = (fake_vid_src_t *)h;
    src->is_open = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_vid_src_get_support_codecs(esp_capture_video_src_if_t *h, const esp_capture_format_id_t **codecs,
                                                         uint8_t *num)
{
    // TODO add other supported formats
    static esp_capture_format_id_t fake_vid_src_fmts[] = {
        ESP_CAPTURE_FMT_ID_RGB565,
        ESP_CAPTURE_FMT_ID_YUV422P,
        ESP_CAPTURE_FMT_ID_O_UYY_E_VYY,
        ESP_CAPTURE_FMT_ID_YUV420,
    };
    *num = sizeof(fake_vid_src_fmts) / sizeof(esp_capture_format_id_t);
    *codecs = fake_vid_src_fmts;
    return ESP_CAPTURE_ERR_OK;
}

static bool fake_vid_src_supported(fake_vid_src_t *src, esp_capture_format_id_t format)
{
    const esp_capture_format_id_t *codecs = NULL;
    uint8_t num = 0;
    fake_vid_src_get_support_codecs((esp_capture_video_src_if_t *)src, &codecs, &num);
    for (uint8_t i = 0; i < num; i++) {
        if (codecs[i] == format) {
            return true;
        }
    }
    return false;
}

static esp_capture_err_t src_set_fixed_caps(esp_capture_video_src_if_t *h, const esp_capture_video_info_t *fixed_caps)
{
    if (h == NULL || fixed_caps == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    fake_vid_src_t *src = (fake_vid_src_t *)h;
    if (src->is_start) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    if (fake_vid_src_supported(src, fixed_caps->format_id) == false) {
        ESP_LOGE(TAG, "Set fixed caps not supported format");
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    src->use_fixed_caps = (fixed_caps->format_id != ESP_CAPTURE_FMT_ID_NONE);
    src->vid_info = *fixed_caps;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_vid_src_negotiate_caps(esp_capture_video_src_if_t *h, esp_capture_video_info_t *in_cap,
                                                     esp_capture_video_info_t *out_caps)
{
    fake_vid_src_t *src = (fake_vid_src_t *)h;
    // Process fixed capability
    if (src->use_fixed_caps) {
        if (in_cap->format_id == ESP_CAPTURE_FMT_ID_ANY || in_cap->format_id == src->vid_info.format_id) {
            *out_caps = src->vid_info;
            src->nego_ok = true;
            return ESP_CAPTURE_ERR_OK;
        }
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    // Process negotiate with any format
    if (in_cap->format_id == ESP_CAPTURE_FMT_ID_ANY) {
        // TODO here just return for yuv422
        *out_caps = *in_cap;
        out_caps->format_id = ESP_CAPTURE_FMT_ID_RGB565;
        src->vid_info = *in_cap;
        src->nego_ok = true;
        return ESP_CAPTURE_ERR_OK;
    }
    if (fake_vid_src_supported(src, in_cap->format_id) == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    *out_caps = *in_cap;
    src->vid_info = *in_cap;
    src->nego_ok = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_vid_src_start(esp_capture_video_src_if_t *h)
{
    fake_vid_src_t *src = (fake_vid_src_t *)h;
    if (src->nego_ok == false) {
        ESP_LOGE(TAG, "Not nego OK when start");
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    esp_video_codec_resolution_t res = {
        .width = src->vid_info.width,
        .height = src->vid_info.height,
    };
    uint32_t image_size = esp_video_codec_get_image_size((esp_video_codec_pixel_fmt_t)src->vid_info.format_id, &res);
    if (image_size == 0) {
        ESP_LOGE(TAG, "Can not get image size");
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    // Allocate memory for fb
    src->cur_fb = 0;
    for (uint8_t i = 0; i < src->fb_count; i++) {
        uint32_t real_size;
        // Align allocate for HW process need cache aligned
        src->fb[i] = (uint8_t *)esp_video_codec_align_alloc(64, image_size, &real_size);
        if (src->fb[i] == NULL) {
            ret = ESP_CAPTURE_ERR_NO_MEM;
            break;
        }
        // Fill pattern
        memset(src->fb[i], 0xFF * (i + 1) / src->fb_count, image_size);
#if CONFIG_IDF_TARGET_ESP32P4
        esp_cache_msync(src->fb[i], real_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
        src->fb_size[i] = image_size;
        src->fb_used[i] = false;
    }
    // Clear up memory
    if (ret != ESP_CAPTURE_ERR_OK) {
        for (uint8_t i = 0; i < src->fb_count; i++) {
            if (src->fb[i] != NULL) {
                esp_video_codec_free(src->fb[i]);
                src->fb[i] = NULL;
            }
        }
    } else {
        src->is_start = true;
    }
    return ret;
}

static int get_fb_index(fake_vid_src_t *src, uint8_t *data)
{
    for (uint8_t i = 0; i < src->fb_count; i++) {
        if (src->fb[i] == data) {
            return i;
        }
    }
    return -1;
}

static esp_capture_err_t fake_vid_src_acquire_frame(esp_capture_video_src_if_t *h, esp_capture_stream_frame_t *frame)
{
    fake_vid_src_t *src = (fake_vid_src_t *)h;
    if (src->is_start == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    // Simple logic for wait FB is ready
    int retry = 10;
    while (retry > 0) {
        if (src->fb_used[src->cur_fb] == false) {
            break;
        }
        vTaskDelay(30 / portTICK_PERIOD_MS);
        retry--;
    }
    if (src->fb_used[src->cur_fb]) {
        // All FB is used
        ESP_LOGE(TAG, "All FB in used %d", src->cur_fb);
        return ESP_CAPTURE_ERR_NO_RESOURCES;
    }
    // NO PTS control here
    src->fb_used[src->cur_fb] = true;
    frame->stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO;
    frame->data = src->fb[src->cur_fb];
    frame->size = src->fb_size[src->cur_fb];
    src->cur_fb++;
    if (src->cur_fb >= src->fb_count) {
        src->cur_fb = 0;
    }
    vTaskDelay(25 / portTICK_PERIOD_MS);
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_vid_src_release_frame(esp_capture_video_src_if_t *h, esp_capture_stream_frame_t *frame)
{
    fake_vid_src_t *src = (fake_vid_src_t *)h;
    if (src->is_start == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    int fb_idx = get_fb_index(src, frame->data);
    if (fb_idx == -1 || (src->fb_used[fb_idx] == false)) {
        ESP_LOGE(TAG, "Frame not found");
        return ESP_CAPTURE_ERR_NOT_FOUND;
    }
    src->fb_used[fb_idx] = false;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_vid_src_stop(esp_capture_video_src_if_t *h)
{
    fake_vid_src_t *src = (fake_vid_src_t *)h;
    for (uint8_t i = 0; i < src->fb_count; i++) {
        if (src->fb[i] != NULL) {
            esp_video_codec_free(src->fb[i]);
            src->fb[i] = NULL;
        }
        src->fb_size[i] = 0;
        src->fb_used[i] = false;
    }
    src->is_start = false;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_video_src_if_t *esp_capture_new_video_fake_src(uint8_t frame_count)
{
    fake_vid_src_t *src = (fake_vid_src_t *)calloc(1, sizeof(fake_vid_src_t));
    if (src == NULL) {
        return NULL;
    }
    src->base.open = fake_vid_src_open;
    src->base.get_support_codecs = fake_vid_src_get_support_codecs;
    src->base.set_fixed_caps = src_set_fixed_caps;
    src->base.negotiate_caps = fake_vid_src_negotiate_caps;
    src->base.start = fake_vid_src_start;
    src->base.acquire_frame = fake_vid_src_acquire_frame;
    src->base.release_frame = fake_vid_src_release_frame;
    src->base.stop = fake_vid_src_stop;
    src->base.close = fake_vid_src_close;
    // Use default frame count
    if (frame_count == 0) {
        frame_count = 1;
    }
    src->fb_count = frame_count > FAKE_VID_SRC_MAX_FB ? FAKE_VID_SRC_MAX_FB : frame_count;
    return &src->base;
}
