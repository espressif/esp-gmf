/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include <stdlib.h>
#include <string.h>
#include "esp_capture_audio_src_if.h"
#include "capture_fake_aud_src.h"

typedef struct {
    esp_capture_audio_src_if_t  base;
    esp_capture_audio_info_t    info;
    int                         frame_num;
    uint64_t                    frames;
    bool                        use_fixed_caps;
    uint8_t                     start : 1;
    uint8_t                     open  : 1;
} fake_aud_src_t;

static esp_capture_err_t fake_aud_src_open(esp_capture_audio_src_if_t *h)
{
    fake_aud_src_t *src = (fake_aud_src_t *)h;
    src->frame_num = 0;
    src->open = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_aud_src_get_support_codecs(esp_capture_audio_src_if_t *src, const esp_capture_format_id_t **codecs, uint8_t *num)
{
    static esp_capture_format_id_t support_codecs[] = {ESP_CAPTURE_FMT_ID_PCM};
    *codecs = support_codecs;
    *num = 1;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_aud_src_set_fixed_caps(esp_capture_audio_src_if_t *h, const esp_capture_audio_info_t *fixed_caps)
{
    if (h == NULL || fixed_caps == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    fake_aud_src_t *src = (fake_aud_src_t *)h;
    if (src->start) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    src->info = *fixed_caps;
    src->use_fixed_caps = (fixed_caps->format_id == ESP_CAPTURE_FMT_ID_PCM);
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_aud_src_negotiate_caps(esp_capture_audio_src_if_t *h, esp_capture_audio_info_t *in_cap, esp_capture_audio_info_t *out_caps)
{
    fake_aud_src_t *src = (fake_aud_src_t *)h;
    if (src->use_fixed_caps) {
        if (in_cap->format_id != src->info.format_id) {
            return ESP_CAPTURE_ERR_NOT_SUPPORTED;
        }
        memcpy(out_caps, &src->info, sizeof(esp_capture_audio_info_t));
        return ESP_CAPTURE_ERR_OK;
    }
    const esp_capture_format_id_t *codecs = NULL;
    uint8_t num = 0;
    fake_aud_src_get_support_codecs(h, &codecs, &num);
    for (int i = 0; i < num; i++) {
        if (codecs[i] == in_cap->format_id) {
            memcpy(out_caps, in_cap, sizeof(esp_capture_audio_info_t));
            src->info = *in_cap;
            return ESP_CAPTURE_ERR_OK;
        }
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t fake_aud_src_start(esp_capture_audio_src_if_t *h)
{
    fake_aud_src_t *src = (fake_aud_src_t *)h;
    if (src->info.sample_rate == 0) {
        return ESP_CAPTURE_ERR_INVALID_STATE;
    }
    src->start = true;
    src->frame_num = 0;
    src->frames = 0;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_aud_src_read_frame(esp_capture_audio_src_if_t *h, esp_capture_stream_frame_t *frame)
{
    fake_aud_src_t *src = (fake_aud_src_t *)h;
    if (src->start == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    int samples = frame->size / (src->info.bits_per_sample / 8 * src->info.channel);
    if (src->info.bits_per_sample == 16) {
        // Gen triangle wav for fake audio frame
        int16_t *dst = (int16_t *)frame->data;
        int limit = 65536;
        int half = limit / 2;
        int v = src->frame_num % limit;
        for (int i = 0; i < samples; i++) {
            int16_t wav_value = (int16_t)(v > half ? limit - v : v);
            for (int j = 0; j < src->info.channel; j++) {
                *(dst++) = wav_value;
            }
            v++;
            if (v >= limit) {
                v = 0;
            }
        }
        int sample_duration = samples * 1000 / src->info.sample_rate;
        vTaskDelay(sample_duration / portTICK_PERIOD_MS);
    }
    frame->pts = src->frame_num * samples * 1000 / src->info.sample_rate;
    src->frame_num++;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_aud_src_stop(esp_capture_audio_src_if_t *h)
{
    fake_aud_src_t *src = (fake_aud_src_t *)h;
    src->start = false;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t fake_aud_src_close(esp_capture_audio_src_if_t *h)
{
    fake_aud_src_t *src = (fake_aud_src_t *)h;
    src->open = false;
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_audio_src_if_t *esp_capture_new_audio_fake_src(void)
{
    fake_aud_src_t *src = (fake_aud_src_t *)calloc(1, sizeof(fake_aud_src_t));
    if (src == NULL) {
        return NULL;
    }
    src->base.open = fake_aud_src_open;
    src->base.get_support_codecs = fake_aud_src_get_support_codecs;
    src->base.set_fixed_caps = fake_aud_src_set_fixed_caps;
    src->base.negotiate_caps = fake_aud_src_negotiate_caps;
    src->base.start = fake_aud_src_start;
    src->base.read_frame = fake_aud_src_read_frame;
    src->base.stop = fake_aud_src_stop;
    src->base.close = fake_aud_src_close;
    return &src->base;
}
