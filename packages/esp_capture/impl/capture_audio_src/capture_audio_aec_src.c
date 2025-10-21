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
#include "capture_utils.h"
#include "esp_afe_sr_iface.h"
#include "esp_vadn_iface.h"
#include "esp_afe_sr_models.h"
#include "esp_vad.h"
#include "msg_q.h"

#define TAG "AUD_AEC_SRC"

#define VAD_CACHE_BLOCK  (3)
#define VAD_SILENT_BLOCK (20)
#define DUMP_STOP_IDX    (2)
#define AFE_RUN_STACK    (8192)
#define VALID_ON_VAD      // Turn on to not send data if VAD not active or else send silent data
// #define DUMP_AFE_DATA  // Enable this define to allow dump AFE input and output to file
#define WAIT_STATE_TIMEOUT(state)                            \
    do {                                                     \
        int _wait_time_out = 1000;                           \
        while (state) {                                      \
            capture_sleep(10);                               \
            _wait_time_out -= 10;                            \
            if (_wait_time_out == 0) {                       \
                ESP_LOGE(TAG, "Wait for"#state "timeout");   \
                break;                                       \
            }                                                \
        }                                                    \
    } while (0);


typedef enum {
    VAD_CHECKING_DETECTING,
    VAD_CHECKING_STARTED,
    VAD_CHECKING_ENDED,
} vad_checking_state_t;

typedef struct {
    esp_vadn_iface_t           *vadnet;
    model_iface_data_t         *vad_model;
    uint8_t                    *vad_working_buf;
    uint8_t                     vad_channel;
    uint8_t                     vad_filled_block;
    uint8_t                     silent_block;
    data_q_t                   *in_q;
    msg_q_handle_t              vad_q;
    uint8_t                     vad_duration;
    vad_checking_state_t        vad_state;
    bool                        dev_src_running;
} audio_aec_vad_res_t;

typedef struct {
    esp_capture_audio_src_if_t  base;
    const char                 *mic_layout;
    uint8_t                     channel;
    uint8_t                     channel_mask;
    bool                        data_on_vad;
    esp_codec_dev_handle_t      handle;
    esp_capture_audio_info_t    info;
    uint64_t                    samples;
    uint8_t                    *cached_frame;
    int                         cached_read_pos;
    int                         cache_size;
    int                         cache_fill;
    uint8_t                     start        : 1;
    uint8_t                     open         : 1;
    uint8_t                     in_quit      : 1;
    uint8_t                     in_error     : 1;
    uint8_t                     stopping     : 1;
    bool                        wait_feeding : 1;
    const esp_afe_sr_iface_t   *afe_handle;
    esp_afe_sr_data_t          *afe_data;
    srmodel_list_t             *models;
    audio_aec_vad_res_t        *vad_res;
} audio_aec_src_t;

static int open_afe_in_ram(void *arg)
{
    audio_aec_src_t *src = (audio_aec_src_t *)arg;
    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    do {
        src->models = esp_srmodel_init("model");
        if (src->models == NULL) {
            ESP_LOGW(TAG, "No model to load");
        }
        afe_config_t *afe_config = afe_config_init(src->mic_layout, src->models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
        // When data_on_vad turn on VAD process before AFE disable it
        if (src->data_on_vad) {
            afe_config->vad_init = false;
        }
        src->afe_handle = esp_afe_handle_from_config(afe_config);
        if (src->afe_handle == NULL) {
            ESP_LOGE(TAG, "Failed to create AFE handle");
            ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
            break;
        }
        src->afe_data = src->afe_handle->create_from_config(afe_config);
        if (src->afe_data == NULL) {
            ESP_LOGE(TAG, "Failed to create AFE data");
            ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
            break;
        }
    } while (0);
    return ret;
}

static esp_capture_err_t open_afe(audio_aec_src_t *src)
{
    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    CAPTURE_RUN_SYNC_IN_RAM("afe_open", open_afe_in_ram, src, ret, AFE_RUN_STACK);
    return ret;
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

static uint8_t get_src_channel(audio_aec_src_t *src)
{
    uint8_t ch = src->channel;
    if (src->channel_mask) {
        ch = __builtin_popcount(src->channel_mask);
    }
    return ch;
}

static void dump_data(uint8_t type, void *data, int size)
{
#ifdef DUMP_AFE_DATA
    static FILE *fp[DUMP_STOP_IDX];
    static uint8_t dump_count = 0;
    if (type == DUMP_STOP_IDX) {
        for (int i = 0; i < DUMP_STOP_IDX; i++) {
            if (fp[i]) {
                fclose(fp[i]);
                fp[i] = NULL;
            }
        }
        dump_count++;
        if (dump_count > 9) {
            dump_count = 0;
        }
        return;
    }
    if (size == 0) {
        return;
    }
    if (fp[type] == NULL) {
        char *pre_name[] = { "feed", "fetch"};
        char file_name[20];
        snprintf(file_name, sizeof(file_name), "/sdcard/%s%d.bin", pre_name[type], dump_count);
        fp[type] = fopen(file_name, "wb");
        if (fp[type]) {
            ESP_LOGI(TAG, "dump to %s", file_name);
        }
    }
    if (fp[type]) {
        fwrite(data, size, 1, fp[type]);
    }
#endif
}

static inline void audio_aec_fill_vad_working_buf(audio_aec_src_t *src, uint8_t *feed_data, int feed_size)
{
    audio_aec_vad_res_t *vad_res = src->vad_res;
    uint8_t src_channel = get_src_channel(src);
    int16_t *src_pcm = (int16_t*) feed_data;
    int16_t *dst_pcm = (int16_t*) vad_res->vad_working_buf;
    int16_t *end =  (int16_t*) (feed_data + feed_size);
    src_pcm += vad_res->vad_channel;
    while (src_pcm < end) {
        *(dst_pcm++) = *src_pcm;
        src_pcm += src_channel;
    }
}

static int audio_aec_feed_data(audio_aec_src_t *src, uint8_t *feed_data, int feed_size)
{
    int ret = src->afe_handle->feed(src->afe_data, (int16_t *)feed_data);
    dump_data(0, feed_data, feed_size);
    return ret;
}

static int audio_aec_read_by_vad(audio_aec_src_t *src, int read_size)
{
    audio_aec_vad_res_t *vad_res = src->vad_res;
    int ret = 0;
    uint8_t *feed_data = NULL;
    if (vad_res->vad_state == VAD_CHECKING_STARTED) {
        if (vad_res->vad_filled_block > 0) {
            // Send vad detection cache firstly
            data_q_read_lock(vad_res->in_q, (void**)&feed_data, &read_size);
            if (feed_data == NULL) {
                ESP_LOGE(TAG, "Fail to get from dev src queue on %d", __LINE__);
                return -1;
            }
            ret = audio_aec_feed_data(src, feed_data, read_size);
            if (ret < 0) {
                ESP_LOGE(TAG, "Fail to feed data %d on %d", ret, __LINE__);
            }
            vad_res->vad_filled_block--;
            data_q_read_unlock(vad_res->in_q);
            return 0;
        }
    }
    data_q_read_lock(vad_res->in_q, (void**)&feed_data, &read_size);
    if (feed_data == NULL) {
        ESP_LOGE(TAG, "Fail to get from dev src queue on %d", __LINE__);
        return -1;
    }
    // Fill working buffer and do detection
    audio_aec_fill_vad_working_buf(src, feed_data, read_size);
    vad_state_t vad_state = vad_res->vadnet->detect(vad_res->vad_model, (int16_t*)vad_res->vad_working_buf);
    switch (vad_res->vad_state) {
        case VAD_CHECKING_STARTED:
            ret = audio_aec_feed_data(src, feed_data, read_size);
            data_q_read_unlock(vad_res->in_q);
            if (ret < 0) {
                ESP_LOGE(TAG, "Fail to feed data %d on %d", ret, __LINE__);
                break;
            }
            if (vad_state == VAD_SILENCE) {
                if (vad_res->silent_block == 0) {
                    ESP_LOGI(TAG, "VAD ended");
                }
                vad_res->silent_block++;
                if (vad_res->silent_block >= VAD_SILENT_BLOCK) {
                    vad_res->vad_state = VAD_CHECKING_ENDED;
                }
            }
            break;
        case VAD_CHECKING_ENDED:
            if (src->wait_feeding) {
                ret = audio_aec_feed_data(src, feed_data, read_size);
                data_q_read_unlock(vad_res->in_q);
                if (ret < 0) {
                    ESP_LOGE(TAG, "Fail to feed data %d on %d", ret, __LINE__);
                }
                break;
            }
            int v = 0;
            while (msg_q_recv(vad_res->vad_q, &v, sizeof(v), true) == 0);
            vad_res->vad_state = VAD_CHECKING_DETECTING;
            vad_res->vad_filled_block = 0;
            ESP_LOGI(TAG, "VAD Detecting");
            // fallthrough
        case VAD_CHECKING_DETECTING:
            if (vad_res->vad_filled_block < VAD_CACHE_BLOCK) {
                vad_res->vad_filled_block++;
            }
            data_q_read_unlock(vad_res->in_q);
            if (vad_state == VAD_SPEECH) {
                ESP_LOGI(TAG, "VAD started");
                vad_res->vad_state = VAD_CHECKING_STARTED;
                vad_res->silent_block = 0;
                // Rewind and resend again
                data_q_rewind(vad_res->in_q, VAD_CACHE_BLOCK);
            }
            v = msg_q_number(vad_res->vad_q);
            if (v < VAD_CACHE_BLOCK) {
                msg_q_send(vad_res->vad_q, &v, sizeof(v));
            }
            ret = 0;
            break;
        default:
            break;
    }
    return ret > 0 ? 0 : ret;
}

static void codec_dev_read_thread(void *arg)
{
    audio_aec_src_t *src = (audio_aec_src_t *)arg;
    audio_aec_vad_res_t *vad_res = src->vad_res;
    int read_size = src->cache_size * get_src_channel(src);
    bool err = false;
    while (!src->stopping) {
        uint8_t *data = (uint8_t*)data_q_get_buffer(vad_res->in_q, read_size);
        if (data == NULL) {
            break;
        }
        int ret = esp_codec_dev_read(src->handle, data, read_size);
        if (ret != 0) {
            ESP_LOGE(TAG, "Fail to read data %d", ret);
            data_q_send_buffer(vad_res->in_q, 0);
            err = true;
            break;
        }
        data_q_send_buffer(vad_res->in_q, read_size);
    }
    if (err) {
        data_q_wakeup(vad_res->in_q);
    }
    vad_res->dev_src_running = false;
    ESP_LOGI(TAG, "Codec src in exited");
    capture_thread_destroy(NULL);
}

static inline int audio_aec_src_read_from_vad(audio_aec_src_t *src)
{
    int ret = 0;
    int read_size = src->cache_size * get_src_channel(src);
    audio_aec_vad_res_t *vad_res = src->vad_res;
    do {
        vad_res->dev_src_running = true;
        capture_thread_handle_t thread = NULL;
        capture_thread_create_from_scheduler(&thread, "codec_dev_src", codec_dev_read_thread, src);
        if (thread == NULL) {
            ret = -1;
            vad_res->dev_src_running = false;
            break;
        }
        while (!src->stopping) {
            // Handle read by vad
            ret = audio_aec_read_by_vad(src, read_size);
            if (ret != 0) {
                ESP_LOGE(TAG, "Fail to read data ddd %d", ret);
                break;
            }
        }
    } while (0);
    if (vad_res->in_q) {
        data_q_wakeup(vad_res->in_q);
    }
    if (ret) {
        src->in_error = true;
        int v = msg_q_number(vad_res->vad_q);
        if (v < VAD_CACHE_BLOCK) {
            msg_q_send(vad_res->vad_q, &v, sizeof(v));
        }
    }
    // Wait for codec source exited
    WAIT_STATE_TIMEOUT(vad_res->dev_src_running);
    return ret;
}

static inline int audio_aec_src_read_directly(audio_aec_src_t *src)
{
    int read_size = src->cache_size * get_src_channel(src);
    int ret = 0;
    uint32_t *feed_data = NULL;
    do {
        feed_data = malloc(read_size);
        if (feed_data == NULL) {
            ret = -1;
            break;
        }
        while (!src->stopping) {
            ret = esp_codec_dev_read(src->handle, feed_data, read_size);
            if (ret != 0) {
                ESP_LOGE(TAG, "Fail to read data %d", ret);
                break;
            }
            ret = src->afe_handle->feed(src->afe_data, (int16_t *)feed_data);
            if (ret < 0) {
                ESP_LOGE(TAG, "Fail to feed data %d", ret);
                break;
            }
        }
    } while (0);
    if (ret < 0) {
        src->in_error = true;
    }
    if (feed_data) {
        capture_free(feed_data);
    }
    return ret;
}

static void audio_aec_src_buffer_in_thread(void *arg)
{
    audio_aec_src_t *src = (audio_aec_src_t *)arg;
    if (src->vad_res) {
        audio_aec_src_read_from_vad(src);
    } else {
        audio_aec_src_read_directly(src);
    }
    src->in_quit = true;
    ESP_LOGI(TAG, "Buffer in exited");
    capture_thread_destroy(NULL);
}

static void release_vad(audio_aec_src_t *src)
{
    if (src->vad_res == NULL) {
        return;
    }
    audio_aec_vad_res_t *vad_res = src->vad_res;
    if (vad_res->vadnet) {
        vad_res->vadnet->destroy(vad_res->vad_model);
        vad_res->vadnet = NULL;
    }
    if (vad_res->in_q) {
        data_q_deinit(vad_res->in_q);
        vad_res->in_q = NULL;
    }
    if (vad_res->vad_working_buf) {
        capture_free(vad_res->vad_working_buf);
        vad_res->vad_working_buf = NULL;
    }
    if (vad_res->vad_q) {
        msg_q_destroy(vad_res->vad_q);
        vad_res->vad_q = NULL;
    }
    vad_res->vad_filled_block = 0;
    capture_free(src->vad_res);
    src->vad_res = NULL;
}

static esp_capture_err_t prepare_vad(audio_aec_src_t *src, int audio_chunksize)
{
    if (src->models == NULL || src->data_on_vad == false) {
        return ESP_CAPTURE_ERR_OK;
    }
    char *model_name = esp_srmodel_filter(src->models, ESP_VADN_PREFIX, NULL);
    esp_vadn_iface_t *vadnet = (esp_vadn_iface_t*)esp_vadn_handle_from_name(model_name);
    if (vadnet == NULL) {
        ESP_LOGW(TAG, "VAD model not found");
        return ESP_CAPTURE_ERR_NOT_FOUND;
    }
    src->vad_res = capture_calloc(1, sizeof(audio_aec_vad_res_t));
    if (src->vad_res == NULL) {
        ESP_LOGE(TAG, "Failed to allocate vad res");
        return ESP_CAPTURE_ERR_NO_MEM;
    }
    audio_aec_vad_res_t *vad_res = src->vad_res;
    do {
        vad_res->vad_channel = (uint8_t) (strchr(src->mic_layout, 'M') - src->mic_layout);
        vad_res->vadnet = vadnet;
        vad_res->vad_model = vadnet->create(model_name, VAD_MODE_0, 1, 32, 64);
        if (vad_res->vad_model == NULL) {
            ESP_LOGE(TAG, "Failed to create vad model");
            break;
        }
        int cache_size = (VAD_CACHE_BLOCK * 3) * (src->cache_size * get_src_channel(src) + 16);
        vad_res->in_q = data_q_init(cache_size);
        if (vad_res->in_q == NULL) {
            ESP_LOGE(TAG, "Failed to create vad cache");
            break;
        }
        // Only one channel data for vad
        vad_res->vad_working_buf = capture_calloc(1, src->cache_size);
        if (vad_res->vad_working_buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate vad cache");
            break;
        }
        vad_res->vad_q = msg_q_create(VAD_CACHE_BLOCK, sizeof(int));
        if (vad_res->vad_q == NULL) {
            ESP_LOGE(TAG, "Failed to create vad queue");
            break;
        }
        vad_res->vad_state = VAD_CHECKING_DETECTING;
        return ESP_CAPTURE_ERR_OK;
    } while (0);
    release_vad(src);
    return ESP_CAPTURE_ERR_NO_MEM;
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
    ret = open_afe(src);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to open AFE");
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    int audio_chunksize = src->afe_handle->get_feed_chunksize(src->afe_data);
    src->cache_size = audio_chunksize * (16 / 8);
    if (src->data_on_vad) {
        ret = prepare_vad(src, audio_chunksize);
        if (ret != ESP_CAPTURE_ERR_OK) {
            return ret;
        }
    }
    src->cached_frame = capture_calloc(1, src->cache_size);
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
        if (src->in_quit || src->in_error) {
            return ESP_CAPTURE_ERR_INTERNAL;
        }
        src->cache_fill = 0;
        src->cached_read_pos = 0;
        bool use_silent = false;
        audio_aec_vad_res_t *vad_res = src->vad_res;
        if (vad_res && vad_res->vad_state != VAD_CHECKING_STARTED) {
            // Receive from queue
            int v = 0;
            msg_q_recv(vad_res->vad_q, &v, sizeof(v), false);
#ifdef VALID_ON_VAD
            frame->size = 0;
            return ESP_CAPTURE_ERR_OK;
#endif
            memset(src->cached_frame, 0, src->cache_size);
            src->cache_fill = src->cache_size;
            use_silent = true;
        }
        if (use_silent == false) {
            src->wait_feeding = true;
            afe_fetch_result_t *res = src->afe_handle->fetch(src->afe_data);
            src->wait_feeding = false;
            if (res->ret_value != ESP_OK) {
                ESP_LOGE(TAG, "Fail to read from AEC ret %d", res->ret_value);
                // When feed fetch not match may return error ignore it currently
            }
            dump_data(1, res->data, res->data_size);
            if (res->data_size <= src->cache_size) {
                memcpy(src->cached_frame, res->data, res->data_size);
                src->cache_fill = res->data_size;
            } else {
                ESP_LOGE(TAG, "Why so huge %d", res->data_size);
            }
        }
    }
    src->samples += frame->size / 2;
    return ESP_CAPTURE_ERR_OK;
}

static int close_afe_in_ram(void *arg)
{
    audio_aec_src_t *src = (audio_aec_src_t *)arg;
    if (src->models) {
        esp_srmodel_deinit(src->models);
        src->models = NULL;
    }
    if (src->afe_data) {
        src->afe_handle->destroy(src->afe_data);
        src->afe_data = NULL;
    }
    return 0;
}

static esp_capture_err_t audio_aec_src_stop(esp_capture_audio_src_if_t *h)
{
    audio_aec_src_t *src = (audio_aec_src_t *)h;
    esp_capture_err_t ret = ESP_CAPTURE_ERR_OK;
    if (src->in_quit == false) {
        // fetch once
        if (src->vad_res && src->vad_res->vad_state != VAD_CHECKING_STARTED) {
        } else{
            src->afe_handle->fetch(src->afe_data);
        }
        src->stopping = true;
        WAIT_STATE_TIMEOUT(src->in_quit == false);
    }
    release_vad(src);

    CAPTURE_RUN_SYNC_IN_RAM("afe_close", close_afe_in_ram, src, ret, AFE_RUN_STACK);

    if (src->cached_frame) {
        capture_free(src->cached_frame);
        src->cached_frame = NULL;
    }
    if (src->handle) {
        esp_codec_dev_close(src->handle);
    }
    dump_data(DUMP_STOP_IDX, NULL, 0);
    src->in_error = false;
    src->start = false;
    return ret;
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
    src->data_on_vad = cfg->data_on_vad;
    if (cfg->mic_layout == NULL) {
        src->mic_layout = "MR";
    } else {
        src->mic_layout = cfg->mic_layout;
    }
    return &src->base;
}

#endif  /* CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 */
