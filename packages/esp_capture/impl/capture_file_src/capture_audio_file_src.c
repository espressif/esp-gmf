/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_capture_types.h"
#include "esp_capture_audio_src_if.h"
#include "capture_os.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#define TAG               "AUD_FILE_SRC"
#define MAX_FILE_PATH_LEN 128

typedef struct {
    esp_capture_audio_src_if_t base;
    esp_capture_audio_info_t   aud_info;
    char                       file_path[MAX_FILE_PATH_LEN];
    FILE                      *fp;
    uint8_t                    is_open  : 1;
    uint8_t                    is_start : 1;
    uint8_t                    nego_ok  : 1;
} audio_file_src_t;

static esp_capture_err_t get_aud_info_by_name(audio_file_src_t *src)
{
    char *ext = strrchr(src->file_path, '.');
    if (ext == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    if (strcmp(ext, ".pcm") == 0) {
        // TODO get info from file pattern
        src->aud_info.format_id = ESP_CAPTURE_FMT_ID_PCM;
        src->aud_info.sample_rate = 44100;
        src->aud_info.bits_per_sample = 16;
        src->aud_info.channel = 2;
    } else if (strcmp(ext, ".opus") == 0) {
        src->aud_info.format_id = ESP_CAPTURE_FMT_ID_OPUS;
        src->aud_info.sample_rate = 44100;
        src->aud_info.bits_per_sample = 16;
        src->aud_info.channel = 2;
    } else {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_file_src_close(esp_capture_audio_src_if_t *h)
{
    audio_file_src_t *src = (audio_file_src_t *)h;
    if (src->fp) {
        fclose(src->fp);
        src->fp = NULL;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_file_src_open(esp_capture_audio_src_if_t *h)
{
    audio_file_src_t *src = (audio_file_src_t *)h;
    src->fp = fopen(src->file_path, "rb");
    if (src->fp == NULL) {
        ESP_LOGE(TAG, "open file %s failed", src->file_path);
        return ESP_CAPTURE_ERR_NOT_FOUND;
    }
    int ret = get_aud_info_by_name(src);
    if (ret != ESP_CAPTURE_ERR_OK) {
        audio_file_src_close(h);
        return ret;
    }
    src->is_open = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_file_src_get_codec(esp_capture_audio_src_if_t *h, const esp_capture_format_id_t **codecs, uint8_t *num)
{
    audio_file_src_t *src = (audio_file_src_t *)h;
    if (src->is_open == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    *num = 1;
    *codecs = &src->aud_info.format_id;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_file_src_nego(esp_capture_audio_src_if_t *h, esp_capture_audio_info_t *in_cap, esp_capture_audio_info_t *out_caps)
{
    audio_file_src_t *src = (audio_file_src_t *)h;
    if (src->is_open == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    if (in_cap->format_id == src->aud_info.format_id) {
        src->nego_ok = true;
        *out_caps = src->aud_info;
        return ESP_CAPTURE_ERR_OK;
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t audio_file_src_start(esp_capture_audio_src_if_t *h)
{
    audio_file_src_t *src = (audio_file_src_t *)h;
    if (src->nego_ok == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    src->is_start = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_file_src_read_frame(esp_capture_audio_src_if_t *h, esp_capture_stream_frame_t *frame)
{
    audio_file_src_t *src = (audio_file_src_t *)h;
    if (src->is_start == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    if (src->aud_info.format_id == ESP_CAPTURE_FMT_ID_PCM) {
        int ret = fread(frame->data, 1, frame->size, src->fp);
        if (ret >= 0) {
            frame->size = ret;
            return ESP_CAPTURE_ERR_OK;
        }
    } else if (src->aud_info.format_id == ESP_CAPTURE_FMT_ID_OPUS) {
        uint32_t payload_size = 0;
        int ret = fread(&payload_size, 1, 4, src->fp);
        if (payload_size && frame->size >= payload_size) {
            ret = fread(frame->data, 1, payload_size, src->fp);
            if (ret >= 0) {
                frame->size = ret;
                return ESP_CAPTURE_ERR_OK;
            }
        }
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t audio_file_src_stop(esp_capture_audio_src_if_t *h)
{
    audio_file_src_t *src = (audio_file_src_t *)h;
    src->nego_ok = false;
    src->is_start = false;
    if (src->fp) {
        fseek(src->fp, 0, SEEK_SET);
    }
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_audio_src_if_t *esp_capture_new_audio_file_src(const char *file_name)
{
    audio_file_src_t *src = (audio_file_src_t *)capture_calloc(1, sizeof(audio_file_src_t));
    if (src == NULL) {
        return NULL;
    }
    strncpy(src->file_path, file_name, sizeof(src->file_path) - 1);
    src->base.open = audio_file_src_open;
    src->base.get_support_codecs = audio_file_src_get_codec;
    src->base.negotiate_caps = audio_file_src_nego;
    src->base.start = audio_file_src_start;
    src->base.read_frame = audio_file_src_read_frame;
    src->base.stop = audio_file_src_stop;
    src->base.close = audio_file_src_close;
    return &src->base;
}
