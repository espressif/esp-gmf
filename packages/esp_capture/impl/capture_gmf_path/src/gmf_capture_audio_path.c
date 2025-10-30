/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_capture_path_mngr.h"
#include "capture_gmf_mngr.h"
#include "data_queue.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_caps_def.h"
#include "esp_log.h"
#include "capture_audio_src_el.h"

#include "capture_pipeline_utils.h"
#include "gmf_capture_path_mngr.h"

#define TAG "GMF_CAPTURE_APATH"

typedef struct gmf_audio_path_t gmf_audio_path_t;

typedef struct {
    gmf_capture_path_res_t     base;
    data_q_t                  *audio_q;
    esp_gmf_element_handle_t   aenc_el;
    esp_gmf_port_handle_t      sink_port;
    esp_capture_sync_handle_t  sync_handle;
    uint32_t                   bitrate;
} audio_path_res_t;

struct gmf_audio_path_t {
    esp_capture_audio_path_mngr_if_t  base;
    gmf_capture_path_mngr_t           mngr;
};

static esp_capture_err_t get_audio_encoder(gmf_capture_path_mngr_t *mngr, uint8_t idx)
{
    audio_path_res_t *res = (audio_path_res_t *)gmf_capture_path_mngr_get_path(mngr, idx);
    uint8_t path_mask = (1 << res->base.path);
    for (int i = 0; i < mngr->pipeline_num; i++) {
        esp_capture_gmf_pipeline_t *pipeline = &mngr->pipeline[i];
        if ((pipeline->path_mask & path_mask) == 0) {
            continue;
        }
        if (capture_pipeline_is_sink(pipeline->pipeline)) {
            esp_gmf_element_handle_t aenc_handle = capture_get_element_by_caps(pipeline->pipeline, ESP_GMF_CAPS_AUDIO_ENCODER);
            if (aenc_handle) {
                res->aenc_el = aenc_handle;
                return ESP_CAPTURE_ERR_OK;
            }
        }
    }
    return ESP_CAPTURE_ERR_NOT_FOUND;
}

static esp_gmf_element_handle_t get_sink_tail_element(gmf_capture_path_mngr_t *mngr, audio_path_res_t *res)
{
    uint8_t path_mask = (1 << res->base.path);
    for (int i = 0; i < mngr->pipeline_num; i++) {
        esp_capture_gmf_pipeline_t *pipeline = &mngr->pipeline[i];
        if ((pipeline->path_mask & path_mask) == 0) {
            continue;
        }
        if (capture_pipeline_is_sink(pipeline->pipeline)) {
            return ESP_GMF_PIPELINE_GET_LAST_ELEMENT(((esp_gmf_pipeline_handle_t)pipeline->pipeline));
        }
    }
    return NULL;
}

static esp_capture_err_t set_audio_source_sync_handle(gmf_capture_path_mngr_t *mngr, esp_capture_sync_handle_t sync_handle)
{
    for (int i = 0; i < mngr->pipeline_num; i++) {
        esp_capture_gmf_pipeline_t *pipeline = &mngr->pipeline[i];
        if (capture_pipeline_is_src(pipeline->pipeline, mngr->pipeline, mngr->pipeline_num)) {
            esp_gmf_element_handle_t aud_src = NULL;
            esp_gmf_pipeline_get_el_by_name(pipeline->pipeline, "aud_src", &aud_src);
            if (aud_src) {
                capture_audio_src_el_set_sync_handle(aud_src, sync_handle);
            }
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_path_apply_setting(gmf_capture_path_mngr_t *mngr, uint8_t idx)
{
    audio_path_res_t *res = (audio_path_res_t *)gmf_capture_path_mngr_get_idx(mngr, idx);
    if (res->bitrate && res->aenc_el) {
        esp_gmf_audio_enc_set_bitrate(res->aenc_el, res->bitrate);
    }
    if (res->sync_handle) {
        set_audio_source_sync_handle(mngr, res->sync_handle);
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_path_prepare_all(gmf_capture_path_mngr_t *mngr)
{
    for (int i = 0; i < mngr->path_num; i++) {
        get_audio_encoder(mngr, i);
        audio_path_apply_setting(mngr, i);
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_gmf_err_io_t audio_sink_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    audio_path_res_t *res = (audio_path_res_t *)handle;
    data_q_t *q = res->audio_q;
    int size = sizeof(esp_capture_stream_frame_t) + wanted_size;
    esp_capture_stream_frame_t *aud_frame = (esp_capture_stream_frame_t *)data_q_get_buffer(q, size);
    if (aud_frame == NULL) {
        return ESP_GMF_IO_FAIL;
    }

    aud_frame->stream_type = ESP_CAPTURE_STREAM_TYPE_AUDIO;
    aud_frame->data = ((void *)aud_frame) + sizeof(esp_capture_stream_frame_t);
    if (load->buf) {
        // In bypass case copy data directly
        memcpy(aud_frame->data, load->buf, load->valid_size);
    } else {
        load->buf = aud_frame->data;
        load->buf_length = wanted_size;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t audio_sink_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    audio_path_res_t *res = (audio_path_res_t *)handle;
    data_q_t *q = res->audio_q;
    gmf_capture_path_res_t *mngr_res = &res->base;
    void *data = data_q_get_write_data(q);
    if (data) {
        esp_capture_stream_frame_t *aud_frame = (esp_capture_stream_frame_t *)data;
        aud_frame->pts = (uint32_t)load->pts;
        aud_frame->size = load->valid_size;
        int ret = gmf_capture_path_mngr_frame_reached(mngr_res, aud_frame);
        if (ret == ESP_CAPTURE_ERR_OK) {
            int size = sizeof(esp_capture_stream_frame_t) + load->valid_size;
            data_q_send_buffer(q, size);
        } else {
            ESP_LOGI(TAG, "Drop for disable");
            data_q_send_buffer(q, 0);
        }
        if (load->buf == aud_frame->data) {
            // Clear buf when not bypass case
            load->buf = NULL;
        }
    }
    return ESP_GMF_IO_OK;
}

static esp_capture_err_t audio_path_prepare(gmf_capture_path_res_t *mngr_res)
{
    audio_path_res_t *res = (audio_path_res_t *)mngr_res;
    int queue_size = 0;  // capture_aenc_el_get_out_queue_size(res->aenc_el);
    if (queue_size == 0) {
        queue_size = 10 * 1024;
    }
    res->audio_q = data_q_init(queue_size);
    if (res->audio_q == NULL) {
        return ESP_CAPTURE_ERR_NO_MEM;
    }
    if (res->sink_port == NULL) {
        res->sink_port = NEW_ESP_GMF_PORT_OUT_BLOCK(audio_sink_acquire, audio_sink_release, NULL, res, 0, ESP_GMF_MAX_DELAY);
        if (res->audio_q == NULL) {
            return ESP_CAPTURE_ERR_NO_MEM;
        }
        if (res->aenc_el) {
            esp_gmf_element_register_out_port(res->aenc_el, res->sink_port);
        } else {
            esp_gmf_element_register_out_port(get_sink_tail_element(mngr_res->parent, res), res->sink_port);
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_path_stop(gmf_capture_path_res_t *mngr_res)
{
    audio_path_res_t *res = (audio_path_res_t *)mngr_res;
    if (res->audio_q) {
        data_q_consume_all(res->audio_q);
    }
    return ESP_CAPTURE_ERR_OK;
}

static esp_capture_err_t audio_path_release(gmf_capture_path_res_t *mngr_res)
{
    audio_path_res_t *res = (audio_path_res_t *)mngr_res;
    if (res->audio_q) {
        data_q_deinit(res->audio_q);
        res->audio_q = NULL;
    }
    if (res->sink_port) {
        esp_gmf_element_unregister_out_port(res->aenc_el, res->sink_port);
        res->sink_port = NULL;
    }
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t gmf_audio_path_open(esp_capture_path_mngr_if_t *p, esp_capture_path_cfg_t *cfg)
{
    if (p == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    gmf_audio_path_t *audio_path = (gmf_audio_path_t *)p;
    return gmf_capture_path_mngr_open(&audio_path->mngr, ESP_CAPTURE_STREAM_TYPE_AUDIO, cfg, sizeof(audio_path_res_t));
}

esp_capture_err_t gmf_audio_path_add_path(esp_capture_path_mngr_if_t *p, uint8_t path, esp_capture_stream_info_t *sink_cfg)
{
    if (p == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    gmf_audio_path_t *audio_path = (gmf_audio_path_t *)p;
    return gmf_capture_path_mngr_add_path(&audio_path->mngr, path, sink_cfg);
}

esp_capture_err_t gmf_audio_path_enable_path(esp_capture_path_mngr_if_t *p, uint8_t path, bool enable)
{
    gmf_audio_path_t *audio_path = (gmf_audio_path_t *)p;
    return gmf_capture_path_mngr_enable_path(&audio_path->mngr, path, enable,
                                             audio_path_prepare,
                                             audio_path_stop,
                                             audio_path_release);
}

esp_capture_err_t gmf_audio_path_start(esp_capture_path_mngr_if_t *p)
{
    gmf_audio_path_t *audio_path = (gmf_audio_path_t *)p;
    return gmf_capture_path_mngr_start(&audio_path->mngr, audio_path_prepare_all, audio_path_prepare);
}

esp_capture_err_t gmf_audio_path_set(esp_capture_path_mngr_if_t *p, uint8_t path,
                                     esp_capture_path_set_type_t type, void *cfg, int cfg_size)
{
    // Iterate pipelines to get
    gmf_audio_path_t *audio_path = (gmf_audio_path_t *)p;
    if (p == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    int ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    audio_path_res_t *res = (audio_path_res_t *)gmf_capture_path_mngr_get_path(&audio_path->mngr, path);
    if (res == NULL && (type != ESP_CAPTURE_PATH_SET_TYPE_REGISTER_ELEMENT)) {
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    esp_capture_pipeline_builder_if_t *builder = audio_path->mngr.pipeline_builder;
    if (type == ESP_CAPTURE_PATH_SET_TYPE_SYNC_HANDLE) {
        res->sync_handle = *(esp_capture_sync_handle_t *)cfg;
    } else if (type == ESP_CAPTURE_PATH_SET_TYPE_AUDIO_BITRATE) {
        res->bitrate = *(uint32_t *)cfg;
        ret = ESP_CAPTURE_ERR_OK;
        if (res->aenc_el && cfg_size == sizeof(uint32_t)) {
            ret = esp_gmf_audio_enc_set_bitrate(res->aenc_el, res->bitrate);
        }
    } else if (type == ESP_CAPTURE_PATH_SET_TYPE_REGISTER_ELEMENT) {
        esp_gmf_element_handle_t *el = (esp_gmf_element_handle_t *)cfg;
        if (builder->reg_element) {
            ret = builder->reg_element(builder, *el);
        }
    } else if (type == ESP_CAPTURE_PATH_SET_TYPE_BUILD_PIPELINE) {
        esp_capture_path_build_pipeline_cfg_t *path_cfg = (esp_capture_path_build_pipeline_cfg_t *)cfg;
        esp_capture_gmf_pipeline_cfg_t build_cfg = {
            .element_tags = path_cfg->element_tags,
            .element_num = path_cfg->element_num,
        };
        if (builder->build_pipeline) {
            ret = builder->build_pipeline(builder, path, &build_cfg);
        }
    }
    return ret;
}

esp_capture_err_t gmf_audio_path_get(esp_capture_path_mngr_if_t *p, uint8_t path, esp_capture_path_get_type_t type,
                                     void *cfg, int cfg_size)
{
    gmf_audio_path_t *audio_path = (gmf_audio_path_t *)p;
    if (p == NULL || path >= audio_path->mngr.path_num) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    int ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
    esp_capture_pipeline_builder_if_t *builder = audio_path->mngr.pipeline_builder;
    if (type == ESP_CAPTURE_PATH_GET_ELEMENT) {
        esp_capture_path_element_get_info_t *info = (esp_capture_path_element_get_info_t *)cfg;
        if (builder->get_element) {
            ret = builder->get_element(builder, path, info->element_tag,
                                       (esp_gmf_element_handle_t *)&info->element_hd);
        }
    }
    return ret;
}

esp_capture_err_t gmf_audio_path_return_frame(esp_capture_path_mngr_if_t *p, uint8_t path, esp_capture_stream_frame_t *frame)
{
    gmf_audio_path_t *audio_path = (gmf_audio_path_t *)p;
    if (p == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    audio_path_res_t *res = (audio_path_res_t *)gmf_capture_path_mngr_get_path(&audio_path->mngr, path);
    if (res == NULL) {
        return ESP_CAPTURE_ERR_NOT_FOUND;
    }
    int ret = ESP_CAPTURE_ERR_OK;
    esp_capture_stream_frame_t *read_frame = NULL;
    int read_size = 0;
    if (data_q_have_data(res->audio_q)) {
        data_q_read_lock(res->audio_q, (void **)&read_frame, &read_size);
        ESP_LOGD(TAG, "simple return audio data:%x frame:%x\n", frame->data[0], read_frame->data[0]);
        ret = data_q_read_unlock(res->audio_q);
    }
    return ret == 0 ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_FOUND;
}

esp_capture_err_t gmf_audio_path_stop(esp_capture_path_mngr_if_t *p)
{
    gmf_audio_path_t *audio_path = (gmf_audio_path_t *)p;
    return gmf_capture_path_mngr_stop(&audio_path->mngr, audio_path_stop, audio_path_release);
}

esp_capture_err_t gmf_audio_path_close(esp_capture_path_mngr_if_t *p)
{
    int ret = gmf_audio_path_stop(p);
    gmf_audio_path_t *audio_path = (gmf_audio_path_t *)p;
    gmf_capture_path_mngr_close(&audio_path->mngr);
    // Do extra clean up
    return ret;
}

esp_capture_audio_path_mngr_if_t *esp_capture_new_gmf_audio_mngr(esp_capture_audio_path_mngr_cfg_t *cfg)
{
    if (cfg == NULL || cfg->pipeline_builder == NULL) {
        return NULL;
    }
    gmf_audio_path_t *aud_path = (gmf_audio_path_t *)calloc(1, sizeof(gmf_audio_path_t));
    if (aud_path == NULL) {
        return NULL;
    }
    esp_capture_path_mngr_if_t *base_path = &aud_path->base.base;
    base_path->open = gmf_audio_path_open;
    base_path->add_path = gmf_audio_path_add_path;
    base_path->enable_path = gmf_audio_path_enable_path;
    base_path->start = gmf_audio_path_start;
    base_path->set = gmf_audio_path_set;
    base_path->get = gmf_audio_path_get;
    base_path->return_frame = gmf_audio_path_return_frame;
    base_path->stop = gmf_audio_path_stop;
    base_path->close = gmf_audio_path_close;
    aud_path->mngr.pipeline_builder = cfg->pipeline_builder;
    return &aud_path->base;
}
