/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_capture_types.h"
#include "esp_capture_audio_src_if.h"
#include "esp_capture_audio_dev_src.h"
#include "esp_codec_dev.h"
#include "capture_os.h"
#include "esp_log.h"

#define TAG "AUD_CODEC_SRC"

typedef struct {
    esp_capture_audio_src_if_t  base;
    esp_codec_dev_handle_t      handle;
    esp_capture_audio_info_t    info;
    int                         frame_num;
    uint64_t                    frames;
    bool                        use_fixed_caps;
    uint8_t                     start : 1;
    uint8_t                     open  : 1;
} audio_dev_src_t;

static esp_capture_err_t audio_dev_src_open(esp_capture_audio_src_if_t *h)
{
    audio_dev_src_t *src = (audio_dev_src_t *)h;
    if (src->handle == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    src->frame_num = 0;
    src->open = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_dev_src_get_support_codecs(esp_capture_audio_src_if_t *src, const esp_capture_format_id_t **codecs, uint8_t *num)
{
    static esp_capture_format_id_t support_codecs[] = {ESP_CAPTURE_FMT_ID_PCM};
    *codecs = support_codecs;
    *num = 1;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_dev_src_set_fixed_caps(esp_capture_audio_src_if_t *h, const esp_capture_audio_info_t *fixed_caps)
{
    if (h == NULL || fixed_caps == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    audio_dev_src_t *src = (audio_dev_src_t *)h;
    if (src->start) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    src->info = *fixed_caps;
    src->use_fixed_caps = (fixed_caps->format_id == ESP_CAPTURE_FMT_ID_PCM);
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_dev_src_negotiate_caps(esp_capture_audio_src_if_t *h, esp_capture_audio_info_t *in_cap, esp_capture_audio_info_t *out_caps)
{
    audio_dev_src_t *src = (audio_dev_src_t *)h;
    if (src->use_fixed_caps) {
        if (in_cap->format_id != src->info.format_id) {
            return ESP_CAPTURE_ERR_NOT_SUPPORTED;
        }
        memcpy(out_caps, &src->info, sizeof(esp_capture_audio_info_t));
        return ESP_CAPTURE_ERR_OK;
    }
    const esp_capture_format_id_t *codecs = NULL;
    uint8_t num = 0;
    audio_dev_src_get_support_codecs(h, &codecs, &num);
    for (int i = 0; i < num; i++) {
        if (codecs[i] == in_cap->format_id) {
            memcpy(out_caps, in_cap, sizeof(esp_capture_audio_info_t));
            src->info = *in_cap;
            return ESP_CAPTURE_ERR_OK;
        }
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t audio_dev_src_start(esp_capture_audio_src_if_t *h)
{
    audio_dev_src_t *src = (audio_dev_src_t *)h;
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = src->info.sample_rate,
        .bits_per_sample = src->info.bits_per_sample,
        .channel = src->info.channel,
    };
    int ret = esp_codec_dev_open(src->handle, &fs);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to open codec device, ret=%d", ret);
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    src->start = true;
    src->frame_num = 0;
    src->frames = 0;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_dev_src_read_frame(esp_capture_audio_src_if_t *h, esp_capture_stream_frame_t *frame)
{
    audio_dev_src_t *src = (audio_dev_src_t *)h;
    if (src->start == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    int ret = esp_codec_dev_read(src->handle, frame->data, frame->size);
    if (ret == 0) {
        int samples = frame->size / (src->info.bits_per_sample / 8 * src->info.channel);
        frame->pts = src->frame_num * samples * 1000 / src->info.sample_rate;
        src->frame_num++;
    }
    return ret == 0 ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_INTERNAL;
}

static esp_capture_err_t audio_dev_src_stop(esp_capture_audio_src_if_t *h)
{
    audio_dev_src_t *src = (audio_dev_src_t *)h;
    if (src->handle) {
        esp_codec_dev_close(src->handle);
    }
    src->start = false;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_dev_src_close(esp_capture_audio_src_if_t *h)
{
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_audio_src_if_t *esp_capture_new_audio_dev_src(esp_capture_audio_dev_src_cfg_t *cfg)
{
    if (cfg == NULL || cfg->record_handle == NULL) {
        ESP_LOGE(TAG, "Invalid argument for cfg %p handle %p", cfg, cfg ? cfg->record_handle : NULL);
        return NULL;
    }
    audio_dev_src_t *src = capture_calloc(1, sizeof(audio_dev_src_t));
    if (src == NULL) {
        return NULL;
    }
    src->base.open = audio_dev_src_open;
    src->base.get_support_codecs = audio_dev_src_get_support_codecs;
    src->base.set_fixed_caps = audio_dev_src_set_fixed_caps;
    src->base.negotiate_caps = audio_dev_src_negotiate_caps;
    src->base.start = audio_dev_src_start;
    src->base.read_frame = audio_dev_src_read_frame;
    src->base.stop = audio_dev_src_stop;
    src->base.close = audio_dev_src_close;
    src->handle = cfg->record_handle;
    return &src->base;
}
