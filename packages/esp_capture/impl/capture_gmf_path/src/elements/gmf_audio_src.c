/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_info.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_err.h"
#include "esp_capture_types.h"
#include "esp_capture_audio_src_if.h"
#include "esp_gmf_info.h"
#include "capture_audio_src_el.h"
#include "capture_utils.h"
#include "esp_gmf_audio_element.h"
#include "data_queue.h"
#include "esp_capture_sync.h"
#include "capture_perf_mon.h"

#define CAPTURE_SYNC_TOLERANCE       100
#define EVENT_GROUP_AUDIO_SRC_EXITED 1

static const char *TAG = "AUD_SRC";

typedef struct {
    esp_gmf_audio_element_t      parent;
    esp_gmf_port_handle_t        in_port;
    esp_capture_sync_handle_t    sync_handle;
    uint32_t                     base_pts;
    esp_capture_audio_src_if_t  *audio_src_if;
    esp_gmf_info_sound_t         aud_info;
    data_q_t                    *audio_src_q;
    uint32_t                     audio_frame_samples;
    uint32_t                     audio_frames;
    uint32_t                     audio_frame_size;
    capture_event_grp_handle_t   event_group;
    uint8_t                      fetching_audio : 1;
    uint8_t                      is_open        : 1;
    uint8_t                      frame_reached  : 1;
} audio_src_t;

static esp_gmf_err_io_t audio_src_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    audio_src_t *audio_src = (audio_src_t *)handle;
    if (audio_src->audio_src_q) {
        void *data = NULL;
        int size;
        if (wait_ticks == 0 && data_q_have_data(audio_src->audio_src_q) == false) {
            ESP_LOGE(TAG, "No data now");
            return ESP_GMF_IO_FAIL;
        }
        int ret = data_q_read_lock(audio_src->audio_src_q, &data, &size);
        if (ret != 0 || data == NULL) {
            ESP_LOGE(TAG, "Fail to read data");
            return ESP_GMF_IO_FAIL;
        }
        esp_capture_stream_frame_t *frame = (esp_capture_stream_frame_t *)data;
        load->pts = frame->pts;
        load->buf = frame->data;
        load->buf_length = frame->size;
        load->valid_size = frame->size;
        return ESP_GMF_IO_OK;
    }
    ESP_LOGE(TAG, "Q not created yet");
    return ESP_GMF_IO_FAIL;
}

static esp_gmf_err_io_t audio_src_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    audio_src_t *audio_src = (audio_src_t *)handle;
    if (audio_src->audio_src_q) {
        data_q_read_unlock(audio_src->audio_src_q);
    }
    return ESP_GMF_IO_OK;
}

static uint32_t calc_audio_pts(audio_src_t *audio_src, uint32_t frames)
{
    if (audio_src->aud_info.sample_rates == 0) {
        return 0;
    }
    return (uint32_t)((uint64_t)frames * audio_src->audio_frame_samples * 1000 / audio_src->aud_info.sample_rates);
}

static void audio_src_thread(void *arg)
{
    audio_src_t *audio_src = (audio_src_t *)arg;
    ESP_LOGI(TAG, "Start to fetch audio src data now");
    bool err_exit = false;
    while (audio_src->fetching_audio) {
        // TODO how to calculate audio_frame_size
        int frame_size = sizeof(esp_capture_stream_frame_t) + audio_src->audio_frame_size;
        uint8_t *data = data_q_get_buffer(audio_src->audio_src_q, frame_size);
        if (data == NULL) {
            ESP_LOGE(TAG, "Failed to get buffer from audio src queue");
            break;
        }
        if (audio_src->frame_reached == false) {
            CAPTURE_PERF_MON(0, "Audio Src Frame Reached", {});
            // Get base pts
            if (audio_src->sync_handle) {
                uint32_t cur_pts = 0;
                esp_capture_sync_get_current(audio_src->sync_handle, &cur_pts);
                if (cur_pts > CAPTURE_SYNC_TOLERANCE) {
                    audio_src->base_pts = cur_pts;
                }
            }
            audio_src->frame_reached = true;
        }
        esp_capture_stream_frame_t *frame = (esp_capture_stream_frame_t *)data;
        frame->stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
        frame->data = (data + sizeof(esp_capture_stream_frame_t));
        frame->size = audio_src->audio_frame_size;
        int ret = audio_src->audio_src_if->read_frame(audio_src->audio_src_if, frame);
        frame->pts = calc_audio_pts(audio_src, audio_src->audio_frames) + audio_src->base_pts;
        if (ret != ESP_CAPTURE_ERR_OK) {
            data_q_send_buffer(audio_src->audio_src_q, 0);
            ESP_LOGE(TAG, "Failed to read audio frame ret %d", ret);
            err_exit = true;
            break;
        }
        if (audio_src->sync_handle) {
            esp_capture_sync_audio_update(audio_src->sync_handle, frame->pts);
            if (esp_capture_sync_get_mode(audio_src->sync_handle) != ESP_CAPTURE_SYNC_MODE_AUDIO) {
                uint32_t cur_pts = 0;
                esp_capture_sync_get_current(audio_src->sync_handle, &cur_pts);
                if (frame->pts > cur_pts + CAPTURE_SYNC_TOLERANCE || frame->pts + CAPTURE_SYNC_TOLERANCE < cur_pts) {
                    frame->pts = cur_pts;
                }
            }
        }
        data_q_send_buffer(audio_src->audio_src_q, frame_size);
        audio_src->audio_frames++;
    }
    audio_src->audio_frames = 0;
    // Wakeup reader if read from device failed
    if (err_exit) {
        data_q_wakeup(audio_src->audio_src_q);
    }
    ESP_LOGI(TAG, "Audio src thread exited");
    capture_event_group_set_bits(audio_src->event_group, EVENT_GROUP_AUDIO_SRC_EXITED);
    capture_thread_destroy(NULL);
}

static void get_default_frame_size(audio_src_t *audio_src)
{
    audio_src->audio_frame_samples = 10 * audio_src->aud_info.sample_rates / 1000;
    audio_src->audio_frame_size = audio_src->audio_frame_samples * audio_src->aud_info.channels * audio_src->aud_info.bits / 8;
}

static esp_gmf_job_err_t audio_src_el_open(esp_gmf_audio_element_handle_t self, void *para)
{
    audio_src_t *audio_src = (audio_src_t *)self;
    if (audio_src->audio_src_if == NULL) {
        capture_aud_src_el_cfg_t *cfg = (capture_aud_src_el_cfg_t *)OBJ_GET_CFG(self);
        if (cfg == NULL || cfg->asrc_if == NULL) {
            ESP_LOGE(TAG, "Invalid audio src config");
            return ESP_GMF_JOB_ERR_FAIL;
        }
        audio_src->audio_src_if = cfg->asrc_if;
    }
    if (audio_src->audio_src_q == NULL) {
        if (audio_src->audio_frame_size == 0) {
            get_default_frame_size(audio_src);
        }
        uint32_t queue_size = (audio_src->audio_frame_size + 32) * 3;
        audio_src->audio_src_q = data_q_init(queue_size);
        if (audio_src->audio_src_q == NULL) {
            ESP_LOGE(TAG, "Fail to allicate audio src queue");
            return ESP_GMF_JOB_ERR_FAIL;
        }
    }
    CAPTURE_PERF_MON(0, "Audio Src Start", {
        audio_src->audio_src_if->start(audio_src->audio_src_if);
    });
    audio_src->frame_reached = false;
    audio_src->fetching_audio = true;
    capture_thread_handle_t handle;
    capture_event_group_create(&audio_src->event_group);
    int ret = capture_thread_create_from_scheduler(&handle, "AUD_SRC", audio_src_thread, audio_src);
    if (ret != 0) {
        audio_src->fetching_audio = false;
        ESP_LOGE(TAG, "Failed to create audio src thread");
        return ESP_GMF_JOB_ERR_FAIL;
    }
    esp_gmf_element_notify_snd_info(self, &audio_src->aud_info);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t audio_src_el_process(esp_gmf_audio_element_handle_t self, void *para)
{
    esp_gmf_element_handle_t hd = (esp_gmf_element_handle_t)self;
    esp_gmf_port_t *in = ESP_GMF_ELEMENT_GET(hd)->in;
    esp_gmf_port_t *out = ESP_GMF_ELEMENT_GET(hd)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;

    int ret = esp_gmf_port_acquire_in(in, &in_load, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT, ESP_GMF_MAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "Acquire on in port, ret:%d", ret);
        return ret == ESP_GMF_IO_ABORT ? ESP_GMF_JOB_ERR_OK : ESP_GMF_JOB_ERR_FAIL;
    }
    if (in_load->valid_size) {
        out_load = in_load;
        ret = esp_gmf_port_acquire_out(out, &out_load, out_load->valid_size, -1);
        if (ret >= 0) {
            esp_gmf_port_release_out(out, out_load, 0);
        }
    } else {
        ret = ESP_GMF_JOB_ERR_CONTINUE;
    }
    if (in_load->is_done) {
        ret = ESP_GMF_JOB_ERR_DONE;
    }
    esp_gmf_port_release_in(in, in_load, 0);
    return ret;
}

static esp_gmf_job_err_t audio_src_el_close(esp_gmf_audio_element_handle_t self, void *para)
{
    audio_src_t *audio_src = (audio_src_t *)self;
    ESP_LOGI(TAG, "Closed, %p", self);
    if (audio_src->fetching_audio) {
        audio_src->fetching_audio = false;
        data_q_consume_all(audio_src->audio_src_q);
        capture_event_group_wait_bits(audio_src->event_group, EVENT_GROUP_AUDIO_SRC_EXITED, 1000);
        capture_event_group_clr_bits(audio_src->event_group, EVENT_GROUP_AUDIO_SRC_EXITED);
    }
    if (audio_src->audio_src_q) {
        data_q_deinit(audio_src->audio_src_q);
        audio_src->audio_src_q = NULL;
    }
    if (audio_src->event_group) {
        capture_event_group_destroy(audio_src->event_group);
        audio_src->event_group = NULL;
    }
    if (audio_src->audio_src_if) {
        audio_src->audio_src_if->stop(audio_src->audio_src_if);
        audio_src->audio_src_if->close(audio_src->audio_src_if);
        audio_src->is_open = false;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_err_t audio_src_el_destroy(esp_gmf_obj_handle_t self)
{
    audio_src_t *audio_src = (audio_src_t *)self;
    if (audio_src->is_open) {
        audio_src->audio_src_if->close(audio_src->audio_src_if);
        audio_src->is_open = false;
    }
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(self);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t audio_src_cvt_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    int ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t)ctx;
    if (evt->type != ESP_GMF_EVT_TYPE_REPORT_INFO || evt->sub != ESP_GMF_INFO_SOUND) {
        return ret;
    }
    esp_gmf_event_state_t state = -1;
    esp_gmf_element_get_state(self, &state);
    esp_gmf_info_sound_t *info = (esp_gmf_info_sound_t *)evt->payload;
    audio_src_t *audio_src = (audio_src_t *)self;
    audio_src->aud_info = *info;
    ESP_LOGI(TAG, "Get rate:%d, ch:%d, bits:%d", info->sample_rates, info->channels, info->bits);
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    return ret;
}

static esp_gmf_err_t audio_src_el_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return capture_audio_src_el_init((capture_aud_src_el_cfg_t *)cfg, (esp_gmf_obj_handle_t *)handle);
}

esp_gmf_err_t capture_audio_src_el_init(capture_aud_src_el_cfg_t *cfg, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    audio_src_t *audio_src = esp_gmf_oal_calloc(1, sizeof(audio_src_t));
    ESP_GMF_MEM_CHECK(TAG, audio_src, return ESP_GMF_ERR_MEMORY_LACK);
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)audio_src;
    int ret = ESP_GMF_ERR_MEMORY_LACK;
    obj->new_obj = audio_src_el_new;
    obj->del_obj = audio_src_el_destroy;
    if (cfg) {
        capture_aud_src_el_cfg_t *src_cfg = esp_gmf_oal_calloc(1, sizeof(capture_aud_src_el_cfg_t));
        ESP_GMF_MEM_CHECK(TAG, src_cfg, { goto ASRC_FAIL;});
        *src_cfg = *cfg;
        esp_gmf_obj_set_config(obj, cfg, sizeof(capture_aud_src_el_cfg_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_src");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ASRC_FAIL, "Failed set OBJ tag");

    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(audio_src, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ASRC_FAIL, "Failed initialize audio el");

    ESP_GMF_ELEMENT_GET(audio_src)->ops.open = audio_src_el_open;
    ESP_GMF_ELEMENT_GET(audio_src)->ops.process = audio_src_el_process;
    ESP_GMF_ELEMENT_GET(audio_src)->ops.close = audio_src_el_close;
    ESP_GMF_ELEMENT_GET(audio_src)->ops.event_receiver = audio_src_cvt_received_event_handler;
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(audio_src_acquire, audio_src_release, NULL, audio_src, 0, ESP_GMF_MAX_DELAY);
    ESP_GMF_MEM_CHECK(TAG, in_port, {ret = ESP_GMF_ERR_MEMORY_LACK; goto ASRC_FAIL;});
    *handle = obj;
    esp_gmf_element_register_in_port(*handle, in_port);
    ESP_LOGD(TAG, "Create Audio SRC, %s-%p, %p", OBJ_GET_TAG(obj), obj, *handle);
    return ESP_GMF_ERR_OK;

ASRC_FAIL:
    esp_gmf_obj_delete(obj);
    return ret;
}

esp_gmf_err_t capture_audio_src_el_set_sync_handle(esp_gmf_element_handle_t self, esp_capture_sync_handle_t sync_handle)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    audio_src_t *audio_src = (audio_src_t *)self;
    audio_src->sync_handle = sync_handle;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t capture_audio_src_el_set_src_if(esp_gmf_element_handle_t self, esp_capture_audio_src_if_t *asrc_if)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, asrc_if, return ESP_GMF_ERR_INVALID_ARG);
    audio_src_t *audio_src = (audio_src_t *)self;
    audio_src->audio_src_if = asrc_if;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t capture_audio_src_el_set_in_frame_samples(esp_gmf_element_handle_t self, int sample_size)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    audio_src_t *audio_src = (audio_src_t *)self;
    if (sample_size) {
        audio_src->audio_frame_samples = sample_size;
        audio_src->audio_frame_size = sample_size * audio_src->aud_info.channels * audio_src->aud_info.bits / 8;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t capture_audio_src_el_negotiate(esp_gmf_element_handle_t self, esp_capture_audio_info_t *nego_info, esp_capture_audio_info_t *out_info)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, nego_info, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, out_info, return ESP_GMF_ERR_INVALID_ARG);
    audio_src_t *audio_src = (audio_src_t *)self;
    if (audio_src->audio_src_if == NULL) {
        return ESP_GMF_ERR_INVALID_STATE;
    }
    if (audio_src->is_open == false) {
        CAPTURE_PERF_MON(0, "Audio Src Open", {
            audio_src->audio_src_if->open(audio_src->audio_src_if);
        });
        audio_src->is_open = true;
    }
    int ret = audio_src->audio_src_if->negotiate_caps(audio_src->audio_src_if, nego_info, out_info);
    return ret == ESP_CAPTURE_ERR_OK ? ESP_GMF_ERR_OK : ESP_GMF_ERR_FAIL;
}
