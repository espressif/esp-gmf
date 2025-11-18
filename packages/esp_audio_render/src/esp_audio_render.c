/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <sdkconfig.h>
#include <stdatomic.h>
#include <math.h>
#include "esp_audio_render.h"
#include "audio_render_proc.h"
#include "audio_render_mem.h"
#include "esp_gmf_ringbuffer.h"
#include "esp_gmf_mixer.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_audio_param.h"
#include "esp_timer.h"
#include "esp_log.h"

#define TAG "AUD_RENDER"

#define AUDIO_RENDER_DEFAULT_SAMPLE_RATE    (48000)
#define AUDIO_RENDER_DEFAULT_BIT_PER_SAMPLE (16)
#define AUDIO_RENDER_DEFAULT_CHANNEL        (2)
#define ESP_AUDIO_RENDER_MIXED_STREAM       ESP_AUDIO_RENDER_STREAM_ID(0xFF)
#define AUDIO_RENDER_DEFAULT_BUF_ALIGN      (16)
#define AUDIO_RENDER_PERIOD                 (20)
#define AUDIO_RENDER_MIN_PERIOD             (5)
#define SAMPLE_SIZE(info)                   ((info).bits_per_sample * (info).channel / 8)
#define GET_SAMPLE_BY_PERIOD(period, info)  (int) ((uint64_t)(period) * (info).sample_rate / 1000)
#define SAMPLE_SIZE_OF_PERIOD(period, info) (GET_SAMPLE_BY_PERIOD(period, (info)) * SAMPLE_SIZE((info)))
#define MIXER_EXITED_BIT                    (1 << 0)
#define MIXER_STREAM_EXIT_BIT(stream_id)    (1 << (1 + (stream_id)))
#define IS_VALID_SAMPLE_INFO(info)          ((info) && (info)->sample_rate && (info)->bits_per_sample && (info)->channel)

#define AUDIO_RENDER_STREAM_STATE_RUNNING  (1 << 0)
#define AUDIO_RENDER_STREAM_STATE_WRITING  (1 << 1)
#define AUDIO_RENDER_STREAM_STATE_EXITING  (1 << 2)
#define AUDIO_RENDER_STREAM_STATE_FLUSHING (1 << 3)
#define AUDIO_RENDER_STREAM_STATE_PAUSE    (1 << 4)

#define AUDIO_RENDER_STREAM_SET_RUNNING(state)   atomic_fetch_or(&state, AUDIO_RENDER_STREAM_STATE_RUNNING)
#define AUDIO_RENDER_STREAM_CLR_RUNNING(state)   atomic_fetch_and(&state, ~AUDIO_RENDER_STREAM_STATE_RUNNING)
#define AUDIO_RENDER_STREAM_IS_RUNNING(state)    (atomic_load(&state) & AUDIO_RENDER_STREAM_STATE_RUNNING)

#define AUDIO_RENDER_STREAM_SET_WRITING(state)   atomic_fetch_or(&state, AUDIO_RENDER_STREAM_STATE_WRITING)
#define AUDIO_RENDER_STREAM_CLR_WRITING(state)   atomic_fetch_and(&state, ~AUDIO_RENDER_STREAM_STATE_WRITING)
#define AUDIO_RENDER_STREAM_IS_WRITING(state)    (atomic_load(&state) & AUDIO_RENDER_STREAM_STATE_WRITING)

#define AUDIO_RENDER_STREAM_SET_EXITING(state)   atomic_fetch_or(&state, AUDIO_RENDER_STREAM_STATE_EXITING)
#define AUDIO_RENDER_STREAM_CLR_EXITING(state)   atomic_fetch_and(&state, ~AUDIO_RENDER_STREAM_STATE_EXITING)
#define AUDIO_RENDER_STREAM_IS_EXITING(state)    (atomic_load(&state) & AUDIO_RENDER_STREAM_STATE_EXITING)

#define AUDIO_RENDER_STREAM_SET_FLUSHING(state)  atomic_fetch_or(&state, AUDIO_RENDER_STREAM_STATE_FLUSHING)
#define AUDIO_RENDER_STREAM_CLR_FLUSHING(state)  atomic_fetch_and(&state, ~AUDIO_RENDER_STREAM_STATE_FLUSHING)
#define AUDIO_RENDER_STREAM_IS_FLUSHING(state)   (atomic_load(&state) & AUDIO_RENDER_STREAM_STATE_FLUSHING)

#define AUDIO_RENDER_STREAM_SET_PAUSE(state)     atomic_fetch_or(&state, AUDIO_RENDER_STREAM_STATE_PAUSE)
#define AUDIO_RENDER_STREAM_CLR_PAUSE(state)     atomic_fetch_and(&state, ~AUDIO_RENDER_STREAM_STATE_PAUSE)
#define AUDIO_RENDER_STREAM_IS_PAUSE(state)      (atomic_load(&state) & AUDIO_RENDER_STREAM_STATE_PAUSE)

typedef struct audio_render_t audio_render_t;

typedef struct {
    esp_audio_render_stream_id_t   stream_id;
    audio_render_proc_handle_t     proc_handle;
    audio_render_t                *parent;
    atomic_ushort                  state;
    esp_gmf_rb_handle_t            rb;
    uint8_t                       *mixed_in_buf;
    bool                           mixer_empty;
    esp_audio_render_mixer_gain_t  mixer_gain;
} audio_render_stream_t;

struct audio_render_t {
    esp_audio_render_cfg_t           cfg;
    esp_gmf_task_config_t            task_cfg;
    esp_audio_render_event_cb_t      event_cb;
    void                            *event_ctx;
    audio_render_stream_t           *streams;
    uint8_t                          stream_num;
    bool                             running;
    esp_ae_mixer_handle_t            mixer;
    uint8_t                         *mixed_out_buf;
    uint32_t                         mixer_block_size;
    audio_render_event_grp_handle_t  event_grp;
    void                             *mutex;
    esp_audio_render_stream_id_t      solo_stream;
};

static inline audio_render_stream_t* get_stream(audio_render_t* render, esp_audio_render_stream_id_t stream_id)
{
    uint8_t idx = 0;
    if (stream_id == ESP_AUDIO_RENDER_MIXED_STREAM) {
        if (render->stream_num > 1) {
            idx = render->stream_num - 1;
        } else {
            return NULL;
        }
    } else {
        idx = stream_id;
    }
    if (idx < render->stream_num) {
        return &render->streams[idx];
    }
    return NULL;
}

static inline int write_rb(esp_gmf_rb_handle_t rb, uint8_t *data, uint32_t size)
{
    esp_gmf_data_bus_block_t blk = {
        .buf = data,
        .buf_length = size,
        .valid_size = size,
    };
    esp_gmf_err_io_t ret = esp_gmf_rb_acquire_write(rb, &blk, size, AUDIO_RENDER_PERIOD * 2);
    // TODO this logic is error
    ret = esp_gmf_rb_release_write(rb, &blk, AUDIO_RENDER_PERIOD * 2);
    if (ret == ESP_GMF_IO_TIMEOUT) {
        ESP_LOGW(TAG, "Write to mixer ringbuffer timeout, mixer may too slow");
        ret = 0;
    }
    return ret;
}

static int audio_render_post_writer(uint8_t *pcm_data, uint32_t len, void *ctx)
{
    audio_render_t *audio_render = (audio_render_t *)ctx;
    if (audio_render->cfg.out_writer) {
        return audio_render->cfg.out_writer(pcm_data, len, audio_render->cfg.out_ctx);
    }
    return 0;
}

static int audio_render_stream_writer(uint8_t *pcm_data, uint32_t len, void *ctx)
{
    audio_render_stream_t* stream = (audio_render_stream_t*)ctx;
    if (stream->stream_id == stream->parent->solo_stream) {
        // Solo play write to post directly
        audio_render_stream_t* post_stream = get_stream(stream->parent, ESP_AUDIO_RENDER_MIXED_STREAM);
        if (post_stream && post_stream->proc_handle) {
            return audio_render_proc_write(post_stream->proc_handle, pcm_data, len);
        }
        return audio_render_post_writer(pcm_data, len, stream->parent);
    }
    // Write to mixer thread
    if (stream->rb) {
        return write_rb(stream->rb, pcm_data, len);
    }
    // Not possible cases
    return 0;
}

static inline void clear_mixer_in(audio_render_t *audio_render, audio_render_stream_t *stream)
{
    // Avoid memset too often
    if (stream->mixer_empty == false) {
        memset(stream->mixed_in_buf, 0, audio_render->mixer_block_size);
        stream->mixer_empty = true;
    }
}

static void audio_render_task(void *arg)
{
    audio_render_t *audio_render = (audio_render_t *)arg;
    uint8_t actual_stream = audio_render->stream_num - 1;
    esp_ae_sample_t in_sample[actual_stream];
    for (int i = 0; i < actual_stream; i++) {
        audio_render_stream_t* stream = &audio_render->streams[i];
        in_sample[i] = (esp_ae_sample_t)stream->mixed_in_buf;
    }
    esp_ae_sample_t out_sample = (esp_ae_sample_t)audio_render->mixed_out_buf;
    int samples = GET_SAMPLE_BY_PERIOD(audio_render->cfg.process_period, audio_render->cfg.out_sample_info);
    audio_render_stream_t* post_stream = get_stream(audio_render, ESP_AUDIO_RENDER_MIXED_STREAM);
    bool consume_fast = false;
    ESP_LOGD(TAG, "Audio Mixer running");
    while (audio_render->running) {
        bool had_valid_data = false;
        uint32_t loop_start = esp_timer_get_time() / 1000;
        for (int i = 0; i < actual_stream; i++) {
            audio_render_stream_t* stream = &audio_render->streams[i];
            // Snapshot state to avoid races with closer
            atomic_ushort state = atomic_load(&stream->state);
            bool exiting = (state & AUDIO_RENDER_STREAM_STATE_EXITING) > 0;
            bool running = (state & AUDIO_RENDER_STREAM_STATE_RUNNING) > 0;
            bool writing = (state & AUDIO_RENDER_STREAM_STATE_WRITING) > 0;;
            bool flushing = (state & AUDIO_RENDER_STREAM_STATE_FLUSHING) > 0;
            bool pause = (state & AUDIO_RENDER_STREAM_STATE_PAUSE) > 0;
            esp_gmf_rb_handle_t rb = stream->rb;
            if (exiting) {
                ESP_LOGI(TAG, "Stream %d exited", stream->stream_id);
                audio_render_event_grp_set_bits(audio_render->event_grp, MIXER_STREAM_EXIT_BIT(stream->stream_id));
                AUDIO_RENDER_STREAM_CLR_EXITING(stream->state);
                clear_mixer_in(audio_render, stream);
                continue;
            }
            if (flushing) {
                esp_gmf_rb_reset(rb);
                AUDIO_RENDER_STREAM_CLR_FLUSHING(stream->state);
                writing = false;
            }
            if (running == false || writing == false || rb == NULL || pause) {
                clear_mixer_in(audio_render, stream);
                continue;
            }

            esp_gmf_data_bus_block_t blk = {
                .buf = stream->mixed_in_buf,
                .buf_length = audio_render->mixer_block_size,
            };
            uint32_t available = 0;
            esp_gmf_rb_bytes_filled(rb, &available);
            if (consume_fast && available < audio_render->mixer_block_size) {
                // Use block read for first not enough stream when post consume too fast
                esp_gmf_rb_acquire_read(rb, &blk, audio_render->mixer_block_size,
                                        AUDIO_RENDER_TIME_TO_TICKS(audio_render->cfg.process_period));
                esp_gmf_rb_release_read(rb, &blk, 0);
                consume_fast = false;
            } else if (available > 0) {
                uint32_t to_read = available > audio_render->mixer_block_size ? audio_render->mixer_block_size : available;
                esp_gmf_rb_acquire_read(rb, &blk, to_read, 0);
                esp_gmf_rb_release_read(rb, &blk, 0);
            }
            if (blk.valid_size > 0) {
                // Set left data to be 0
                if (blk.valid_size < audio_render->mixer_block_size) {
                    memset(stream->mixed_in_buf + blk.valid_size, 0, audio_render->mixer_block_size - blk.valid_size);
                }
                had_valid_data = true;
                stream->mixer_empty = false;
            } else {
                clear_mixer_in(audio_render, stream);
            }
        }
        if (had_valid_data == false ||
            audio_render->solo_stream != ESP_AUDIO_RENDER_ALL_STREAM) {
            audio_render_delay(audio_render->cfg.process_period);
            consume_fast = false;
            continue;
        }
        esp_ae_mixer_process(audio_render->mixer, samples, in_sample, out_sample);
        if (post_stream->proc_handle) {
            audio_render_proc_write(post_stream->proc_handle, out_sample, audio_render->mixer_block_size);
        } else {
            audio_render_post_writer(out_sample, audio_render->mixer_block_size, audio_render);
        }
        // Measure loop run elapse
        uint32_t loop_end = esp_timer_get_time() / 1000;
        if (loop_end < loop_start + audio_render->cfg.process_period / 2) {
            consume_fast = true;
        }
    }
    audio_render_event_grp_set_bits(audio_render->event_grp, MIXER_EXITED_BIT);
    ESP_LOGI(TAG, "Audio Mixer Exited");
    esp_gmf_oal_thread_delete(NULL);
}

static inline esp_audio_render_err_t notify_for_opened(audio_render_t *audio_render)
{
    if (audio_render->event_cb) {
        return audio_render->event_cb(ESP_AUDIO_RENDER_EVENT_TYPE_OPENED, audio_render->event_ctx);
    }
    return ESP_AUDIO_RENDER_ERR_OK;
}

static inline esp_audio_render_err_t notify_for_closed(audio_render_t *audio_render)
{
    if (audio_render->event_cb) {
        return audio_render->event_cb(ESP_AUDIO_RENDER_EVENT_TYPE_CLOSED, audio_render->event_ctx);
    }
    return ESP_AUDIO_RENDER_ERR_OK;
}

static esp_audio_render_err_t open_post_stream(audio_render_t *audio_render)
{
    // No need post stream
    if (audio_render->stream_num <= 1) {
        return ESP_AUDIO_RENDER_ERR_OK;
    }
    if (audio_render->cfg.out_writer == NULL) {
        ESP_LOGE(TAG, "Out writer must provided");
        return ESP_AUDIO_RENDER_ERR_NOT_SUPPORTED;
    }
    audio_render_stream_t* stream = get_stream(audio_render, ESP_AUDIO_RENDER_MIXED_STREAM);
    // Already running
    if (AUDIO_RENDER_STREAM_IS_RUNNING(stream->state)) {
        return ESP_AUDIO_RENDER_ERR_OK;
    }
    esp_audio_render_err_t ret = notify_for_opened(audio_render);
    if (ret != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open post device ret %d", ret);
        return ESP_AUDIO_RENDER_ERR_FAIL;
    }

    uint8_t actual_stream = audio_render->stream_num - 1;
    // Create mixer input buffer firstly
    audio_render->mixer_block_size = SAMPLE_SIZE_OF_PERIOD(audio_render->cfg.process_period, audio_render->cfg.out_sample_info);
    for (int i = 0; i < actual_stream; i++) {
        audio_render_stream_t* in_stream = &audio_render->streams[i];
        in_stream->mixed_in_buf = audio_render_malloc_align(audio_render->mixer_block_size, audio_render->cfg.process_buf_align);
        if (in_stream->mixed_in_buf == NULL) {
            ESP_LOGE(TAG, "Failed to allocate for mixer input %d", i);
            return ESP_AUDIO_RENDER_ERR_NO_RESOURCE;
        }
        memset(in_stream->mixed_in_buf, 0, audio_render->mixer_block_size);
        in_stream->mixer_empty = true;
    }

    // Create mixer output buffer
    audio_render->mixed_out_buf = audio_render_malloc_align(audio_render->mixer_block_size, audio_render->cfg.process_buf_align);
    if (audio_render->mixed_out_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate for mixer output");
        return ESP_AUDIO_RENDER_ERR_NO_RESOURCE;
    }
    memset(audio_render->mixed_out_buf, 0, audio_render->mixer_block_size);

    // Need create mixer tasks
    float mixer_weight = sqrt(1.0f / actual_stream);
    esp_ae_mixer_info_t src_info[actual_stream];
    memset(src_info, 0, sizeof(esp_ae_mixer_info_t) * actual_stream);
    for (int i = 0; i < actual_stream; i++) {
        audio_render_stream_t* in_stream = &audio_render->streams[i];
        if (in_stream->mixer_gain.target_gain > 0.0) {
            src_info[i].weight1 = in_stream->mixer_gain.initial_gain;
            src_info[i].weight2 = in_stream->mixer_gain.target_gain;
            src_info[i].transit_time = in_stream->mixer_gain.transition_time;
        } else {
            // Use default mixer gain settings
            src_info[i].weight1 = 0;
            src_info[i].weight2 = mixer_weight;
            src_info[i].transit_time = 100;
        }
    }
    esp_ae_mixer_cfg_t cfg = {
        .sample_rate = audio_render->cfg.out_sample_info.sample_rate,
        .channel = audio_render->cfg.out_sample_info.channel,
        .bits_per_sample = audio_render->cfg.out_sample_info.bits_per_sample,
        .src_num = actual_stream,
        .src_info = src_info,
    };
    esp_ae_mixer_open(&cfg, &audio_render->mixer);
    if (audio_render->mixer == NULL) {
        ESP_LOGE(TAG, "Failed to create mixer");
        return ESP_AUDIO_RENDER_ERR_NO_RESOURCE;
    }
    audio_render_event_grp_create(&audio_render->event_grp);
    if (audio_render->event_grp == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_AUDIO_RENDER_ERR_NO_RESOURCE;
    }
    // Create for post processor
    if (stream->proc_handle) {
        ret = audio_render_proc_open(stream->proc_handle, &audio_render->cfg.out_sample_info,
                                     &audio_render->cfg.out_sample_info);
        if (ret != ESP_AUDIO_RENDER_ERR_OK) {
            ESP_LOGE(TAG, "Failed to create processor for post");
            return ret;
        }
        audio_render_proc_set_writer(stream->proc_handle, audio_render_post_writer, audio_render);
    }
    audio_render->running = true;
    esp_gmf_task_config_t *task_cfg = &audio_render->task_cfg;
    if (ESP_GMF_ERR_OK != esp_gmf_oal_thread_create(NULL, "AudioRender", audio_render_task, audio_render,
                                                    task_cfg->stack,
                                                    task_cfg->prio, task_cfg->stack_in_ext,
                                                    task_cfg->core)) {
        ESP_LOGE(TAG, "Failed to create mixer thread");
        audio_render->running = false;
        return ESP_AUDIO_RENDER_ERR_NO_RESOURCE;
    }
    AUDIO_RENDER_STREAM_SET_RUNNING(stream->state);
    return ESP_AUDIO_RENDER_ERR_OK;
}

static esp_audio_render_err_t try_close_post_stream(audio_render_t *audio_render)
{
    if (audio_render->stream_num <= 1) {
        return ESP_AUDIO_RENDER_ERR_OK;
    }
    uint8_t actual_stream = audio_render->stream_num - 1;
    for (int i = 0; i < actual_stream; i++) {
        audio_render_stream_t* in_stream = &audio_render->streams[i];
        // Stream still under running
        if (AUDIO_RENDER_STREAM_IS_RUNNING(in_stream->state)) {
            return ESP_AUDIO_RENDER_ERR_OK;
        }
    }
    audio_render_stream_t* stream = get_stream(audio_render, ESP_AUDIO_RENDER_MIXED_STREAM);
    // Wait for mixer thread quit
    if (audio_render->running) {
        audio_render->running = false;
        audio_render_event_grp_wait_bits(audio_render->event_grp, MIXER_EXITED_BIT, AUDIO_RENDER_MAX_LOCK_TIME);
    }
    if (stream->proc_handle) {
        audio_render_proc_close(stream->proc_handle);
    }
    if (audio_render->mixer) {
        esp_ae_mixer_close(audio_render->mixer);
        audio_render->mixer = NULL;
    }
    notify_for_closed(audio_render);

    // Free mixer buffers
    for (int i = 0; i < actual_stream; i++) {
        audio_render_stream_t* in_stream = &audio_render->streams[i];
        if (in_stream->mixed_in_buf) {
            audio_render_free(in_stream->mixed_in_buf);
            in_stream->mixed_in_buf = NULL;
        }
    }
    if (audio_render->mixed_out_buf) {
        audio_render_free(audio_render->mixed_out_buf);
        audio_render->mixed_out_buf = NULL;
    }
    if (audio_render->event_grp) {
        audio_render_event_grp_destroy(audio_render->event_grp);
        audio_render->event_grp = NULL;
    }
    AUDIO_RENDER_STREAM_CLR_RUNNING(stream->state);
    return ESP_AUDIO_RENDER_ERR_OK;
}

static esp_audio_render_err_t close_stream(audio_render_t *audio_render, audio_render_stream_t* stream)
{
    AUDIO_RENDER_STREAM_CLR_RUNNING(stream->state);
    AUDIO_RENDER_STREAM_CLR_WRITING(stream->state);
    // Waiting for mixer thread response
    if (stream->rb) {
        AUDIO_RENDER_STREAM_SET_EXITING(stream->state);
        ESP_LOGI(TAG, "Waiting for stream %d exited", stream->stream_id);
        int bits = audio_render_event_grp_wait_bits(audio_render->event_grp,
                MIXER_STREAM_EXIT_BIT(stream->stream_id),
                AUDIO_RENDER_MAX_LOCK_TIME);
        ESP_LOGI(TAG, "Waiting for stream %d bit:%d exited ok", stream->stream_id, bits);
        audio_render_event_grp_clr_bits(audio_render->event_grp, MIXER_STREAM_EXIT_BIT(stream->stream_id));
        AUDIO_RENDER_STREAM_CLR_EXITING(stream->state);
        esp_gmf_rb_destroy(stream->rb);
        stream->rb = NULL;
    }
    if (stream->proc_handle) {
        audio_render_proc_close(stream->proc_handle);
    }
    if (audio_render->stream_num == 1) {
        notify_for_closed(audio_render);
    }
    try_close_post_stream(audio_render);
    return ESP_AUDIO_RENDER_ERR_OK;
}

static esp_audio_render_err_t open_stream(audio_render_t *audio_render, audio_render_stream_t* stream,
                                          esp_audio_render_sample_info_t *sample_info)
{
    if (AUDIO_RENDER_STREAM_IS_RUNNING(stream->state)) {
        ESP_LOGE(TAG, "Not support open during %d running", stream->stream_id);
        return ESP_AUDIO_RENDER_ERR_INVALID_STATE;
    }
    esp_audio_render_err_t ret = open_post_stream(audio_render);
    if (ret != ESP_AUDIO_RENDER_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open post stream ret %d", ret);
        try_close_post_stream(audio_render);
        return ret;
    }
    do {
        // Open device if only one stream
        if (audio_render->stream_num == 1) {
            ret = notify_for_opened(audio_render);
            if (ret != ESP_AUDIO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to open device ret %d", ret);
                break;
            }
        }
        if (SAME_SAMPLE_INFO(audio_render->cfg.out_sample_info, (*sample_info)) && stream->proc_handle == NULL) {
            // Allow direct write with out processor
        } else {
            ret = audio_render_proc_open(stream->proc_handle, sample_info, &audio_render->cfg.out_sample_info);
            if (ret != ESP_AUDIO_RENDER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to create processor for %d ret %d", stream->stream_id, ret);
                break;
            }
            // Set process writer
            if (audio_render->stream_num == 1) {
                audio_render_proc_set_writer(stream->proc_handle, audio_render_post_writer, audio_render);
            } else {
                audio_render_proc_set_writer(stream->proc_handle, audio_render_stream_writer, stream);
            }
        }
        // Create mixer related resource
        if (audio_render->mixer) {
            // TODO 1.5 or 1 which is better?
            int sample_size = audio_render->mixer_block_size * 6 / 2;
            esp_gmf_rb_create(sample_size, 1, &stream->rb);
            if (stream->rb == NULL) {
                ESP_LOGE(TAG, "Failed to create mixer input ringbuffer for %d", stream->stream_id);
                return ESP_AUDIO_RENDER_ERR_NO_RESOURCE;
            }
            esp_ae_mixer_set_mode(audio_render->mixer, (int)stream->stream_id, ESP_AE_MIXER_MODE_FADE_UPWARD);
        }
        stream->mixer_empty = false;
        AUDIO_RENDER_STREAM_SET_RUNNING(stream->state);
        return ESP_AUDIO_RENDER_ERR_OK;
    } while (0);
    close_stream(audio_render, stream);
    return ESP_AUDIO_RENDER_ERR_NO_RESOURCE;
}

esp_audio_render_err_t esp_audio_render_create(esp_audio_render_cfg_t *cfg, esp_audio_render_handle_t *render)
{
    if (cfg == NULL || render == NULL || cfg->out_writer == NULL) {
        ESP_LOGE(TAG, "cfg:%p, writer:%p, render:%p", cfg, SAFE_PTR(cfg, out_writer), render);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_t *audio_render = (audio_render_t *)audio_render_calloc(1, sizeof(audio_render_t));
    if (audio_render == NULL) {
        ESP_LOGE(TAG, "No memory for audio render");
        return ESP_AUDIO_RENDER_ERR_NO_MEM;
    }
    // Input streams + one post stream
    uint8_t stream_num = cfg->max_stream_num == 1 ? 1 : (cfg->max_stream_num + 1);
    audio_render_stream_t *streams = NULL;
    do {
        streams = (audio_render_stream_t *)audio_render_calloc(1, sizeof(audio_render_stream_t) * stream_num);
        if (streams == NULL) {
            ESP_LOGE(TAG, "No memory stream");
            break;
        }
        if (stream_num > 0) {
            audio_render->mutex = esp_gmf_oal_mutex_create();
            if (audio_render->mutex == NULL) {
                ESP_LOGE(TAG, "Failed to create mutex");
                break;
            }
        }
        // Set stream type
        int i = 0;
        for (; i < stream_num; i++) {
            if (i == stream_num - 1 && stream_num > 1) {
                streams[i].stream_id = ESP_AUDIO_RENDER_MIXED_STREAM;
            } else {
                streams[i].stream_id = i;
            }
            if (cfg->pool) {
                audio_render_proc_create(cfg->pool, &streams[i].proc_handle);
                if (streams[i].proc_handle == NULL) {
                    ESP_LOGE(TAG, "Failed to create processor");
                    break;
                }
                audio_render_proc_set_buf_align(streams[i].proc_handle, cfg->process_buf_align);
            }
            streams[i].parent = audio_render;
        }
        if (i < stream_num) {
            break;
        }
        audio_render->streams = streams;
        audio_render->stream_num = stream_num;
        audio_render->cfg = *cfg;
        if (audio_render->cfg.process_buf_align == 0) {
            audio_render->cfg.process_buf_align = AUDIO_RENDER_DEFAULT_BUF_ALIGN;
        }
        // Set default post output sample information
        if (cfg->out_sample_info.sample_rate == 0) {
            audio_render->cfg.out_sample_info.sample_rate = AUDIO_RENDER_DEFAULT_SAMPLE_RATE;
            audio_render->cfg.out_sample_info.bits_per_sample = AUDIO_RENDER_DEFAULT_BIT_PER_SAMPLE;
            audio_render->cfg.out_sample_info.channel = AUDIO_RENDER_DEFAULT_CHANNEL;
        }
        // Set default process period
        if (audio_render->cfg.process_period == 0) {
            audio_render->cfg.process_period = AUDIO_RENDER_PERIOD;
        }
        if (audio_render->cfg.process_period < AUDIO_RENDER_MIN_PERIOD) {
            ESP_LOGW(TAG, "Force to convert process period to minimum %d", AUDIO_RENDER_MIN_PERIOD);
            audio_render->cfg.process_period = AUDIO_RENDER_MIN_PERIOD;
        }
        audio_render->task_cfg.stack = CONFIG_ESP_AUDIO_RENDER_MIXER_THREAD_STACK_SIZE;
        audio_render->task_cfg.prio = CONFIG_ESP_AUDIO_RENDER_MIXER_THREAD_PRIORITY;
        audio_render->task_cfg.core = CONFIG_ESP_AUDIO_RENDER_MIXER_THREAD_CORE_ID;
        audio_render->task_cfg.stack_in_ext = true;
        audio_render->solo_stream = ESP_AUDIO_RENDER_ALL_STREAM;
        *render = audio_render;
        return ESP_AUDIO_RENDER_ERR_OK;
    } while (0);
    if (streams) {
        for (int i = 0; i < stream_num; i++) {
            if (streams[i].proc_handle) {
                audio_render_proc_destroy(streams[i].proc_handle);
                streams[i].proc_handle = NULL;
            }
        }
        audio_render_free(streams);
    }
    audio_render_free(audio_render);
    return ESP_AUDIO_RENDER_ERR_NO_MEM;
}

esp_audio_render_err_t esp_audio_render_set_event_cb(esp_audio_render_handle_t render,
                                                    esp_audio_render_event_cb_t event_cb, void *ctx)
{
    if (render == NULL || event_cb == NULL) {
        ESP_LOGE(TAG, "Invalid argument for render:%p, event_cb:%p", render, event_cb);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_t *audio_render = (audio_render_t *)render;
    audio_render->event_cb = event_cb;
    audio_render->event_ctx = ctx;
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t esp_audio_render_task_reconfigure(esp_audio_render_handle_t render, esp_gmf_task_config_t *cfg)
{
    if (render == NULL || cfg == NULL) {
        ESP_LOGE(TAG, "Invalid argument for render:%p, cfg:%p", render, cfg);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_t *audio_render = (audio_render_t *)render;
    if (audio_render->running) {
        ESP_LOGW(TAG, "Not allow to reconfigure task during running");
        return ESP_AUDIO_RENDER_ERR_INVALID_STATE;
    }
    audio_render->task_cfg = *cfg;
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t esp_audio_render_set_out_sample_info(esp_audio_render_handle_t render,
                                                            esp_audio_render_sample_info_t *sample_info)
{
    if (render == NULL || !IS_VALID_SAMPLE_INFO(sample_info)) {
        ESP_LOGE(TAG, "Invalid argument for render:%p, sample_info:%p", render, sample_info);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_t *audio_render = (audio_render_t *)render;
    if (audio_render->running) {
        ESP_LOGE(TAG, "Can not delete during running");
        return ESP_AUDIO_RENDER_ERR_INVALID_STATE;
    }
    audio_render->cfg.out_sample_info = *sample_info;
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t esp_audio_render_add_mixed_proc(esp_audio_render_handle_t render,
                                                       esp_audio_render_proc_type_t proc_type[], uint8_t proc_num)
{
    if (render == NULL) {
        ESP_LOGE(TAG, "Invalid argument for render:%p", render);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_t *audio_render = (audio_render_t *)render;
    audio_render_stream_t* stream = get_stream(audio_render, ESP_AUDIO_RENDER_MIXED_STREAM);
    if (stream == NULL) {
        ESP_LOGE(TAG, "Mixed stream not found");
        return ESP_AUDIO_RENDER_ERR_NOT_FOUND;
    }
    return audio_render_proc_add(stream->proc_handle, proc_type, proc_num);
}

esp_audio_render_err_t esp_audio_render_get_mixed_element(esp_audio_render_handle_t render,
                                                          esp_audio_render_proc_type_t proc_type,
                                                          esp_gmf_element_handle_t *element)
{
    if (render == NULL || proc_type == ESP_AUDIO_RENDER_PROC_NONE || element == NULL) {
        ESP_LOGE(TAG, "Invalid argument for render:%p element:%p", render, element);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_t *audio_render = (audio_render_t *)render;
    audio_render_stream_t* stream = get_stream(audio_render, ESP_AUDIO_RENDER_MIXED_STREAM);
    if (stream == NULL) {
        ESP_LOGE(TAG, "Mixed stream not found");
        return ESP_AUDIO_RENDER_ERR_NOT_FOUND;
    }
    *element = audio_render_proc_get_element(stream->proc_handle, proc_type);
    return *element ? ESP_AUDIO_RENDER_ERR_OK : ESP_AUDIO_RENDER_ERR_NOT_FOUND;
}

esp_audio_render_err_t esp_audio_render_stream_get(esp_audio_render_handle_t render,
                                                   esp_audio_render_stream_id_t stream_id,
                                                   esp_audio_render_stream_handle_t *stream_handle)
{
    if (render == NULL || stream_handle == NULL || stream_id == ESP_AUDIO_RENDER_MIXED_STREAM) {
        ESP_LOGE(TAG, "Invalid argument for render:%p stream_handle:%p stream_id:%d", stream_handle, render, stream_id);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_t *audio_render = (audio_render_t *)render;
    audio_render_stream_t* stream = get_stream(audio_render, stream_id);
    if (stream == NULL) {
        ESP_LOGE(TAG, "Stream %d not found", stream_id);
        return ESP_AUDIO_RENDER_ERR_NOT_FOUND;
    }
    *stream_handle = (esp_audio_render_stream_handle_t) stream;
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t esp_audio_render_stream_set_mixer_gain(esp_audio_render_stream_handle_t stream_handle,
                                                              esp_audio_render_mixer_gain_t *mixer_gain)
{
    if (stream_handle == NULL || mixer_gain == NULL || mixer_gain->target_gain < mixer_gain->initial_gain) {
        ESP_LOGE(TAG, "Invalid argument for stream:%p mixer_gain:%p", stream_handle, mixer_gain);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_stream_t* stream = (audio_render_stream_t*)stream_handle;
    audio_render_t *audio_render = stream->parent;
    if (audio_render->running) {
        ESP_LOGE(TAG, "Can not set mixer gain during running");
        return ESP_AUDIO_RENDER_ERR_INVALID_STATE;
    }
    stream->mixer_gain = *mixer_gain;
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t esp_audio_render_set_solo_stream(esp_audio_render_handle_t render,
                                                        esp_audio_render_stream_id_t stream_id)
{
    if (render == NULL) {
        ESP_LOGE(TAG, "Invalid argument for render:%p", render);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_t *audio_render = (audio_render_t *)render;
    if (stream_id >= audio_render->stream_num && stream_id != ESP_AUDIO_RENDER_MIXED_STREAM) {
        ESP_LOGE(TAG, "Invalid stream id %d", stream_id);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render->solo_stream = stream_id;
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t esp_audio_render_stream_open(esp_audio_render_stream_handle_t stream_handle,
                                                    esp_audio_render_sample_info_t *sample_info)
{
    if (stream_handle == NULL || !IS_VALID_SAMPLE_INFO(sample_info)) {
        ESP_LOGE(TAG, "Invalid argument for render:%p, sample_info:%p", stream_handle, sample_info);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_stream_t* stream = (audio_render_stream_t*)stream_handle;
    audio_render_t *audio_render = stream->parent;
    if (audio_render->mutex) {
        esp_gmf_oal_mutex_lock(audio_render->mutex);
    }
    esp_audio_render_err_t ret = open_stream(audio_render, stream, sample_info);
    if (audio_render->mutex) {
        esp_gmf_oal_mutex_unlock(audio_render->mutex);
    }
    return ret;
}

esp_audio_render_err_t esp_audio_render_stream_add_proc(esp_audio_render_stream_handle_t stream_handle,
                                                        esp_audio_render_proc_type_t proc_type[], uint8_t proc_num)
{
    if (stream_handle == NULL) {
        ESP_LOGE(TAG, "Invalid argument for stream:%p", stream_handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_stream_t* stream = (audio_render_stream_t*)stream_handle;
    if (stream->proc_handle == NULL) {
        ESP_LOGE(TAG, "Not supported for no proc handle");
        return ESP_AUDIO_RENDER_ERR_NOT_SUPPORTED;
    }
    audio_render_t *audio_render = stream->parent;
    // Do pre filter
    if (audio_render->stream_num > 1) {
        for (int i = 0; i < proc_num; i++) {
            if (proc_type[i] == ESP_AUDIO_RENDER_PROC_ENC) {
                ESP_LOGE(TAG, "Now allow add encoder into stream processor");
                return ESP_AUDIO_RENDER_ERR_NOT_SUPPORTED;
            }
        }
    }
    return audio_render_proc_add(stream->proc_handle, proc_type, proc_num);
}

esp_audio_render_err_t esp_audio_render_stream_get_element(esp_audio_render_stream_handle_t stream_handle,
                                                           esp_audio_render_proc_type_t proc_type,
                                                           esp_gmf_element_handle_t *element)
{
    if (stream_handle == NULL || proc_type == ESP_AUDIO_RENDER_PROC_NONE || element == NULL) {
        ESP_LOGE(TAG, "Invalid argument for stream:%p element:%p", stream_handle, element);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_stream_t* stream = (audio_render_stream_t*)stream_handle;
    if (stream->proc_handle == NULL) {
        ESP_LOGE(TAG, "Stream proc not existed for %p", stream_handle);
        return ESP_AUDIO_RENDER_ERR_NOT_FOUND;
    }
    *element = audio_render_proc_get_element(stream->proc_handle, proc_type);
    return *element ? ESP_AUDIO_RENDER_ERR_OK : ESP_AUDIO_RENDER_ERR_NOT_FOUND;
}

esp_audio_render_err_t esp_audio_render_stream_write(esp_audio_render_stream_handle_t stream_handle,
                                                     uint8_t *pcm_data, uint32_t pcm_size)
{
    if (stream_handle == NULL || pcm_data == NULL || pcm_size == 0) {
        ESP_LOGE(TAG, "Invalid argument for stream:%p, pcm_data:%p, pcm_size:%d", stream_handle, pcm_data, (int)pcm_size);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_stream_t* stream = (audio_render_stream_t*)stream_handle;
    audio_render_t *audio_render = stream->parent;
    if (!AUDIO_RENDER_STREAM_IS_RUNNING(stream->state)) {
        ESP_LOGW(TAG, "Write prohibited before open for %p", stream_handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_STATE;
    }
    AUDIO_RENDER_STREAM_SET_WRITING(stream->state);
    if (audio_render->solo_stream != ESP_AUDIO_RENDER_ALL_STREAM) {
        // Exclude stream which not solo play
        if (stream->stream_id != audio_render->solo_stream) {
            return ESP_AUDIO_RENDER_ERR_OK;
        }
        if (stream->proc_handle) {
            return audio_render_proc_write(stream->proc_handle, pcm_data, pcm_size);
        }
        audio_render_stream_t* post_stream = get_stream(audio_render, ESP_AUDIO_RENDER_MIXED_STREAM);
        if (post_stream && post_stream->proc_handle) {
            return audio_render_proc_write(post_stream->proc_handle, pcm_data, pcm_size);
        }
        int ret = audio_render_post_writer(pcm_data, pcm_size, audio_render);
        return ret == 0 ? ESP_AUDIO_RENDER_ERR_OK : ESP_AUDIO_RENDER_ERR_FAIL;
    }
    if (stream->proc_handle) {
        return audio_render_proc_write(stream->proc_handle, pcm_data, pcm_size);
    }
    if (stream->rb) {
        return write_rb(stream->rb, pcm_data, pcm_size);
    }
    int ret = audio_render_post_writer(pcm_data, pcm_size, audio_render);
    return ret == 0 ? ESP_AUDIO_RENDER_ERR_OK : ESP_AUDIO_RENDER_ERR_FAIL;
}

esp_audio_render_err_t esp_audio_render_stream_set_fade(esp_audio_render_stream_handle_t stream_handle,
                                                        bool fade_in)
{
    if (stream_handle == NULL) {
        ESP_LOGE(TAG, "Invalid argument for stream:%p", stream_handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_stream_t* stream = (audio_render_stream_t*)stream_handle;
    audio_render_t *audio_render = stream->parent;
    if (audio_render->running == false) {
        ESP_LOGE(TAG, "Mixer is not running yet");
        return ESP_AUDIO_RENDER_ERR_NOT_SUPPORTED;
    }
    if (fade_in) {
        esp_ae_mixer_set_mode(audio_render->mixer, (int)stream->stream_id, ESP_AE_MIXER_MODE_FADE_UPWARD);
    } else {
        esp_ae_mixer_set_mode(audio_render->mixer, (int)stream->stream_id, ESP_AE_MIXER_MODE_FADE_DOWNWARD);
    }
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t esp_audio_render_stream_pause(esp_audio_render_stream_handle_t stream_handle, bool pause)
{
    if (stream_handle == NULL) {
        ESP_LOGE(TAG, "Invalid argument for stream:%p", stream_handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_stream_t* stream = (audio_render_stream_t*)stream_handle;
    audio_render_t *audio_render = stream->parent;
    if (audio_render->mutex) {
        esp_gmf_oal_mutex_lock(audio_render->mutex);
    }
    if (pause) {
        AUDIO_RENDER_STREAM_SET_PAUSE(stream->state);
    } else {
        AUDIO_RENDER_STREAM_CLR_PAUSE(stream->state);
    }
    if (audio_render->mutex) {
        esp_gmf_oal_mutex_unlock(audio_render->mutex);
    }
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t esp_audio_render_stream_flush(esp_audio_render_stream_handle_t stream_handle)
{
    if (stream_handle == NULL) {
        ESP_LOGE(TAG, "Invalid argument for stream:%p", stream_handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_stream_t* stream = (audio_render_stream_t*)stream_handle;
    audio_render_t *audio_render = stream->parent;
    if (audio_render->mutex) {
        esp_gmf_oal_mutex_lock(audio_render->mutex);
    }
    AUDIO_RENDER_STREAM_SET_FLUSHING(stream->state);
    if (audio_render->mutex) {
        esp_gmf_oal_mutex_unlock(audio_render->mutex);
    }
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t esp_audio_render_stream_set_speed(esp_audio_render_stream_handle_t stream_handle, float speed)
{
    if (stream_handle == NULL) {
        ESP_LOGE(TAG, "Invalid argument for stream:%p", stream_handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    esp_gmf_element_handle_t sonic_element = NULL;
    esp_audio_render_stream_get_element(stream_handle, ESP_AUDIO_RENDER_PROC_SONIC, &sonic_element);
    if (sonic_element == NULL) {
        ESP_LOGE(TAG, "Sonic not existed in stream processor");
        return ESP_AUDIO_RENDER_ERR_NOT_SUPPORTED;
    }
    esp_gmf_err_t ret = esp_gmf_audio_param_set_speed(sonic_element, speed);
    return ret == ESP_GMF_ERR_OK ? ESP_AUDIO_RENDER_ERR_OK : ESP_AUDIO_RENDER_ERR_FAIL;
}

esp_audio_render_err_t esp_audio_render_stream_get_latency(esp_audio_render_stream_handle_t stream_handle,
                                                           uint32_t *latency_ms)
{
    if (stream_handle == NULL || latency_ms == NULL) {
        ESP_LOGE(TAG, "Invalid argument for stream:%p latency:%p", stream_handle, latency_ms);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_stream_t* stream = (audio_render_stream_t*)stream_handle;
    audio_render_t *audio_render = stream->parent;
    if (!AUDIO_RENDER_STREAM_IS_RUNNING(stream->state)) {
        ESP_LOGW(TAG, "Get latency prohibited before open for %p", stream_handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_STATE;
    }
    uint32_t delay = 0;
    if (stream->rb) {
        uint32_t available = 0;
        int sample_size = SAMPLE_SIZE(audio_render->cfg.out_sample_info);
        esp_gmf_rb_bytes_filled(stream->rb, &available);
        delay += available * 1000 / sample_size / audio_render->cfg.out_sample_info.sample_rate;
    }
    // TODO add process delay and output delay also
    *latency_ms = delay;
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_audio_render_err_t esp_audio_render_stream_close(esp_audio_render_stream_handle_t stream_handle)
{
    if (stream_handle == NULL) {
        ESP_LOGE(TAG, "Invalid argument for stream:%p", stream_handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_stream_t* stream = (audio_render_stream_t*)stream_handle;
    audio_render_t *audio_render = stream->parent;
    if (!AUDIO_RENDER_STREAM_IS_RUNNING(stream->state)) {
        ESP_LOGW(TAG, "Already closed for %p", stream_handle);
        return ESP_AUDIO_RENDER_ERR_INVALID_STATE;
    }
    if (audio_render->mutex) {
        esp_gmf_oal_mutex_lock(audio_render->mutex);
    }
    esp_audio_render_err_t ret = close_stream(audio_render, stream);
    if (audio_render->mutex) {
        esp_gmf_oal_mutex_unlock(audio_render->mutex);
    }
    return ret;
}

esp_audio_render_err_t esp_audio_render_destroy(esp_audio_render_handle_t render)
{
    if (render == NULL) {
        ESP_LOGE(TAG, "Invalid argument for render:%p", render);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
    }
    audio_render_t *audio_render = (audio_render_t *)render;
    uint8_t actual_stream = audio_render->stream_num > 1 ? (audio_render->stream_num - 1) : audio_render->stream_num;
    // close all streams
    if (audio_render->mutex) {
        esp_gmf_oal_mutex_lock(audio_render->mutex);
    }
    for (int i = 0; i < actual_stream; i++) {
        audio_render_stream_t* stream = &audio_render->streams[i];
        close_stream(audio_render, stream);
    }
    for (int i = 0; i < audio_render->stream_num; i++) {
        audio_render_stream_t* stream = &audio_render->streams[i];
        if (stream->proc_handle) {
            audio_render_proc_destroy(stream->proc_handle);
            stream->proc_handle = NULL;
        }
    }
    if (audio_render->mutex) {
        esp_gmf_oal_mutex_unlock(audio_render->mutex);
    }
    if (audio_render->mutex) {
        esp_gmf_oal_mutex_destroy(audio_render->mutex);
        audio_render->mutex = NULL;
    }
    audio_render_free(audio_render->streams);
    audio_render->streams = NULL;
    audio_render_free(audio_render);
    return ESP_AUDIO_RENDER_ERR_OK;
}
