/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <sdkconfig.h>
#include <string.h>
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
#include "esp_capture_types.h"
#include "esp_capture_audio_src_if.h"
#include "esp_capture_defaults.h"
#include "esp_codec_dev.h"
#include "esp_log.h"
#include "esp_aec.h"
#include "data_queue.h"
#include "capture_os.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_sr_models.h"

#define TAG "AUD_AEC_SRC"

typedef struct {
    esp_capture_audio_src_if_t  base;
    uint8_t                     channel;
    uint8_t                     channel_mask;
    esp_codec_dev_handle_t      handle;
    esp_capture_audio_info_t    info;
    uint64_t                    samples;
    data_q_t                   *in_q;
    uint8_t                    *cached_frame;
    int                         cached_read_pos;
    int                         cache_size;
    int                         cache_fill;
    uint8_t                     start    : 1;
    uint8_t                     open     : 1;
    uint8_t                     in_quit  : 1;
    uint8_t                     stopping : 1;
    const esp_afe_sr_iface_t   *aec_if;
    esp_afe_sr_data_t          *aec_data;
} audio_aec_src_t;

static int cal_frame_length(esp_capture_audio_info_t *info)
{
    // 16ms, 1channel, 16bit
    return 16 * info->sample_rate / 1000 * (16 / 8);
}

static esp_capture_err_t open_afe(audio_aec_src_t *src)
{
    afe_config_t afe_config = AFE_CONFIG_DEFAULT();
    afe_config.vad_init = false;
    afe_config.wakenet_init = false;
    afe_config.afe_perferred_core = 1;
    afe_config.afe_perferred_priority = 20;
    // afe_config.memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_INTERNAL;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 1;
    afe_config.pcm_config.total_ch_num = 1 + 1;
    afe_config.aec_init = true;
    afe_config.se_init = false;
    afe_config.pcm_config.sample_rate = src->info.sample_rate;
    afe_config.voice_communication_init = true;
    src->aec_if = &ESP_AFE_VC_HANDLE;
    src->aec_data = src->aec_if->create_from_config(&afe_config);
    if (src->aec_data == NULL) {
        return ESP_CAPTURE_ERR_NO_MEM;
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_aec_src_open(esp_capture_audio_src_if_t *h)
{
    audio_aec_src_t *src = (audio_aec_src_t *)h;
    if (src->handle == NULL) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    src->samples = 0;
    src->open = true;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_aec_src_get_support_codecs(esp_capture_audio_src_if_t *src, const esp_capture_format_id_t **codecs, uint8_t *num)
{
    static esp_capture_format_id_t support_codecs[] = {ESP_CAPTURE_FMT_ID_PCM};
    *codecs = support_codecs;
    *num = 1;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_aec_src_negotiate_caps(esp_capture_audio_src_if_t *h, esp_capture_audio_info_t *in_cap, esp_capture_audio_info_t *out_caps)
{
    audio_aec_src_t *src = (audio_aec_src_t *)h;
    // Only support 1 channel 16bits PCM
    if (in_cap->format_id != ESP_CAPTURE_FMT_ID_PCM) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    if (in_cap->sample_rate == 8000) {
        out_caps->sample_rate = 8000;
    } else {
        out_caps->sample_rate = 16000;
    }
    out_caps->channel = 1;
    out_caps->bits_per_sample = 16;
    out_caps->format_id = ESP_CAPTURE_FMT_ID_PCM;
    src->info = *out_caps;
    return ESP_CAPTURE_ERR_OK;
}

static void audio_aec_src_buffer_in_thread(void *arg)
{
    audio_aec_src_t *src = (audio_aec_src_t *)arg;
    int read_size = src->cache_size * 2;
    uint8_t *feed_data = malloc(read_size);
    if (feed_data) {
        while (!src->stopping) {
            int ret = esp_codec_dev_read(src->handle, feed_data, read_size);
            if (ret != 0) {
                ESP_LOGE(TAG, "Fail to read data %d", ret);
                break;
            }
            ret = src->aec_if->feed(src->aec_data, (int16_t *)feed_data);
            if (ret < 0) {
                ESP_LOGE(TAG, "Fail to feed data %d", ret);
                break;
            }
        }
        capture_free(feed_data);
    }
    src->in_quit = true;
    ESP_LOGI(TAG, "Buffer in exited");
    capture_thread_destroy(NULL);
}

static esp_capture_err_t audio_aec_src_start(esp_capture_audio_src_if_t *h)
{
    audio_aec_src_t *src = (audio_aec_src_t *)h;
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = src->info.sample_rate,
        .bits_per_sample = 16,
        .channel = src->channel,
        .channel_mask = src->channel_mask,
    };
    src->in_quit = true;
    int ret = esp_codec_dev_open(src->handle, &fs);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to open codec device, ret=%d", ret);
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    src->cache_size = cal_frame_length(&src->info);
    ret = open_afe(src);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to open AFE");
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    int audio_chunksize = src->aec_if->get_feed_chunksize(src->aec_data);
    src->cache_size = audio_chunksize * (16 / 8);

    src->cached_frame = capture_calloc(1, src->cache_size * 2);
    if (src->cached_frame == NULL) {
        ESP_LOGE(TAG, "Failed to allocate cache frame");
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    src->samples = 0;
    src->cached_read_pos = src->cache_fill = 0;
    src->stopping = false;

    capture_thread_handle_t thread = NULL;
    capture_thread_create_from_scheduler(&thread, "buffer_in", audio_aec_src_buffer_in_thread, src);
    src->start = true;
    src->in_quit = false;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_aec_src_read_frame(esp_capture_audio_src_if_t *h, esp_capture_stream_frame_t *frame)
{
    audio_aec_src_t *src = (audio_aec_src_t *)h;
    if (src->start == false) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    frame->pts = (uint32_t)(src->samples * 1000 / src->info.sample_rate);

    int need_size = frame->size;
    uint8_t *frame_data = frame->data;
    while (need_size > 0) {
        if (src->cached_read_pos < src->cache_fill) {
            int left = src->cache_fill - src->cached_read_pos;
            if (left > need_size) {
                left = need_size;
            }
            memcpy(frame_data, src->cached_frame + src->cached_read_pos, left);
            src->cached_read_pos += left;
            need_size -= left;
            frame_data += left;
            continue;
        }
        if (src->in_quit) {
            return ESP_CAPTURE_ERR_INTERNAL;
        }
        src->cache_fill = 0;
        src->cached_read_pos = 0;
        afe_fetch_result_t *res = src->aec_if->fetch(src->aec_data);
        if (res->ret_value != ESP_OK) {
            ESP_LOGE(TAG, "Fail to read from AEC");
            return ESP_CAPTURE_ERR_INTERNAL;
        }
        if (res->data_size <= src->cache_size * 2) {
            memcpy(src->cached_frame, res->data, res->data_size);
            src->cache_fill = res->data_size;
        } else {
            ESP_LOGE(TAG, "Why so huge %d", res->data_size);
        }
    }
    src->samples += frame->size / 2;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_aec_src_stop(esp_capture_audio_src_if_t *h)
{
    audio_aec_src_t *src = (audio_aec_src_t *)h;
    if (src->in_quit == false) {
        // fetch once
        src->aec_if->fetch(src->aec_data);
        src->stopping = true;
        while (src->in_quit == false) {
            capture_sleep(10);
        }
    }
    if (src->aec_data) {
        src->aec_if->destroy(src->aec_data);
        src->aec_data = NULL;
    }
    if (src->cached_frame) {
        capture_free(src->cached_frame);
        src->cached_frame = NULL;
    }
    if (src->handle) {
        esp_codec_dev_close(src->handle);
    }
    src->start = false;
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_aec_src_close(esp_capture_audio_src_if_t *h)
{
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_audio_src_if_t *esp_capture_new_audio_aec_src(esp_capture_audio_aec_src_cfg_t *cfg)
{
    if (cfg == NULL || cfg->record_handle == NULL) {
        return NULL;
    }
    audio_aec_src_t *src = capture_calloc(1, sizeof(audio_aec_src_t));
    src->base.open = audio_aec_src_open;
    src->base.get_support_codecs = audio_aec_src_get_support_codecs;
    src->base.negotiate_caps = audio_aec_src_negotiate_caps;
    src->base.start = audio_aec_src_start;
    src->base.read_frame = audio_aec_src_read_frame;
    src->base.stop = audio_aec_src_stop;
    src->base.close = audio_aec_src_close;
    src->handle = cfg->record_handle;
    src->channel = cfg->channel ? cfg->channel : 2;
    src->channel_mask = cfg->channel_mask;
    return &src->base;
}

#endif  /* CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 */
