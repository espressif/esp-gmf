/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_node.h"
#include "esp_gmf_cache.h"
#include "gmf_audio_common.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"
#include "esp_gmf_howl.h"

/**
 * @brief  Audio howling-suppression context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t  parent;            /*!< GMF audio element base */
    esp_ae_howl_handle_t     howl_hd;           /*!< esp_audio_effects HOWL handle */
    esp_gmf_cache_t         *cached_payload;    /*!< Concatenate variable upstream chunks to fixed HOWL frames */
    esp_gmf_payload_t       *origin_in_load;    /*!< Current payload from acquire_in (released after cache consumes it) */
    uint32_t                 frame_bytes;       /*!< Bytes per HOWL frame (esp_ae_howl_get_frame_size) */
    uint8_t                  bytes_per_sample;  /*!< Bytes per sample period: channel * (bits_per_sample / 8) */
    int64_t                  cur_pts;           /*!< Output PTS for the next frame */
    bool                     need_reopen : 1;   /*!< Reopen handle after upstream sound info change */
} esp_gmf_howl_t;

static const char *TAG = "ESP_GMF_HOWL";

static esp_gmf_err_t esp_gmf_howl_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_howl_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t gmf_howl_acquire_in(esp_gmf_howl_t *howl, esp_gmf_port_handle_t in_port, esp_gmf_payload_t **in_load)
{
    bool needed_load = false;
    esp_gmf_job_err_t job_ret = ESP_GMF_JOB_ERR_OK;
    esp_gmf_cache_ready_for_load(howl->cached_payload, &needed_load);
    if (needed_load) {
        esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &howl->origin_in_load,
                                                            ESP_GMF_ELEMENT_GET(howl)->in_attr.data_size, in_port->wait_ticks);
        ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, load_ret, job_ret, return job_ret);
        int cache_size = 0;
        esp_gmf_cache_get_cached_size(howl->cached_payload, &cache_size);
        uint32_t cache_dur = GMF_AUDIO_CALC_PTS(cache_size, howl->parent.snd_info.sample_rates,
                                                howl->parent.snd_info.channels, howl->parent.snd_info.bits);
        if (howl->origin_in_load->pts > (int64_t)cache_dur) {
            howl->cur_pts = howl->origin_in_load->pts - cache_dur;
        } else {
            howl->cur_pts = 0;
        }
        esp_gmf_cache_load(howl->cached_payload, howl->origin_in_load);
    }
    esp_gmf_err_t ret = esp_gmf_cache_acquire(howl->cached_payload, howl->frame_bytes, in_load);
    if (ret != ESP_GMF_ERR_OK) {
        job_ret = ((ret == ESP_GMF_ERR_NOT_ENOUGH) ? (ESP_GMF_JOB_ERR_CONTINUE) : (ESP_GMF_JOB_ERR_FAIL));
    }
    return job_ret;
}

static esp_gmf_job_err_t esp_gmf_howl_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_howl_t *howl = (esp_gmf_howl_t *)self;
    esp_ae_howl_cfg_t *config = (esp_ae_howl_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_CHECK(TAG, config, return ESP_GMF_JOB_ERR_FAIL, "There is no HOWL configuration");
    howl->bytes_per_sample = (uint8_t)((config->bits_per_sample >> 3) * config->channel);
    esp_ae_err_t ret = esp_ae_howl_open(config, &howl->howl_hd);
    ESP_GMF_CHECK(TAG, howl->howl_hd, return ESP_GMF_JOB_ERR_FAIL, "Failed to create HOWL handle");
    ret = esp_ae_howl_get_frame_size(howl->howl_hd, &howl->frame_bytes);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_JOB_ERR_FAIL, "Failed to get HOWL frame size");
    ESP_GMF_ELEMENT_GET(howl)->in_attr.data_size = (int)howl->frame_bytes;
    ESP_GMF_ELEMENT_GET(howl)->out_attr.data_size = (int)howl->frame_bytes;
    esp_gmf_port_enable_payload_share(ESP_GMF_ELEMENT_GET(self)->in, false);
    esp_gmf_cache_new(howl->frame_bytes, &howl->cached_payload);
    ESP_GMF_CHECK(TAG, howl->cached_payload, return ESP_GMF_JOB_ERR_FAIL, "Failed to new a cached payload on open");
    howl->need_reopen = false;
    GMF_AUDIO_UPDATE_SND_INFO(self, config->sample_rate, config->bits_per_sample, config->channel);
    ESP_LOGI(TAG, "Howl opened, %p, frame_bytes %u", self, (unsigned)howl->frame_bytes);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_howl_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_howl_t *howl = (esp_gmf_howl_t *)self;
    ESP_LOGD(TAG, "Howl closed, %p", self);
    if (howl->cached_payload != NULL) {
        esp_gmf_cache_delete(howl->cached_payload);
        howl->cached_payload = NULL;
    }
    if (howl->howl_hd != NULL) {
        esp_ae_howl_close(howl->howl_hd);
        howl->howl_hd = NULL;
    }
    howl->cur_pts = 0;
    howl->frame_bytes = 0;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_howl_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_howl_t *howl = (esp_gmf_howl_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (howl->need_reopen) {
        esp_gmf_howl_close(self, NULL);
        out_len = esp_gmf_howl_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "HOWL reopen failed");
            return out_len;
        }
    }
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    esp_gmf_err_io_t load_ret = 0;
    bool needed_load = false;
    out_len = gmf_howl_acquire_in(howl, in_port, &in_load);
    if (out_len != ESP_GMF_JOB_ERR_OK) {
        goto __howl_release;
    }
    ESP_LOGD(TAG, "Acq cache, buf:%p, vld:%d, len:%d, done:%d", in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done);
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, ESP_GMF_ELEMENT_GET(howl)->out_attr.data_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, goto __howl_release);
    if (out_load->buf_length < (int)howl->frame_bytes) {
        ESP_LOGE(TAG, "The out payload valid size(%d) is smaller than wanted size(%d)",
                 out_load->buf_length, ESP_GMF_ELEMENT_GET(howl)->out_attr.data_size);
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __howl_release;
    }
    if (in_load->valid_size != ESP_GMF_ELEMENT_GET(howl)->in_attr.data_size) {
        if (in_load->is_done) {
            out_len = ESP_GMF_JOB_ERR_DONE;
            out_load->valid_size = 0;
            out_load->is_done = in_load->is_done;
            ESP_LOGD(TAG, "Return done, line:%d", __LINE__);
            goto __howl_release;
        } else {
            out_len = ESP_GMF_JOB_ERR_CONTINUE;
            ESP_LOGD(TAG, "Return Continue, line:%d", __LINE__);
            goto __howl_release;
        }
    }
    esp_ae_err_t ae_ret = esp_ae_howl_process(howl->howl_hd, (esp_ae_sample_t)in_load->buf, (esp_ae_sample_t)out_load->buf);
    ESP_GMF_RET_ON_ERROR(TAG, ae_ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __howl_release;}, "HOWL process error %d", ae_ret);

    ESP_LOGV(TAG, "Frame bytes: %u, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d",
             (unsigned)howl->frame_bytes, in_load, in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done,
             out_load, out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    out_load->valid_size = (int)howl->frame_bytes;
    out_load->is_done = in_load->is_done;
    out_load->pts = howl->cur_pts;
    howl->cur_pts += GMF_AUDIO_CALC_PTS(howl->frame_bytes, howl->parent.snd_info.sample_rates,
                                        howl->parent.snd_info.channels, howl->parent.snd_info.bits);
    esp_gmf_audio_el_update_file_pos(self, out_load->valid_size);
    if (in_load->is_done) {
        ESP_LOGD(TAG, "HOWL done, out len: %d", out_load->valid_size);
        out_len = ESP_GMF_JOB_ERR_DONE;
    }
    esp_gmf_cache_ready_for_load(howl->cached_payload, &needed_load);
    if (needed_load == false) {
        out_len = ESP_GMF_JOB_ERR_TRUNCATE;
    }
__howl_release:
    esp_gmf_cache_release(howl->cached_payload, in_load);
    if (out_load != NULL) {
        load_ret = esp_gmf_port_release_out(out_port, out_load, out_port->wait_ticks);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "OUT port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    if ((howl->origin_in_load != NULL) && (out_len != ESP_GMF_JOB_ERR_TRUNCATE)) {
        load_ret = esp_gmf_port_release_in(in_port, howl->origin_in_load, in_port->wait_ticks);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "IN port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
        howl->origin_in_load = NULL;
    }
    return out_len;
}

static esp_gmf_err_t howl_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, ctx, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, evt, return ESP_GMF_ERR_INVALID_ARG);
    if ((evt->type != ESP_GMF_EVT_TYPE_REPORT_INFO)
        || (evt->sub != ESP_GMF_INFO_SOUND)
        || (evt->payload == NULL)) {
        return ESP_GMF_ERR_OK;
    }
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t)ctx;
    esp_gmf_element_handle_t el = evt->from;
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_NONE;
    esp_gmf_element_get_state(self, &state);
    esp_gmf_info_sound_t *info = (esp_gmf_info_sound_t *)evt->payload;
    esp_ae_howl_cfg_t *config = (esp_ae_howl_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_howl_t *howl = (esp_gmf_howl_t *)self;
    howl->need_reopen = (config->sample_rate != info->sample_rates) || (info->channels != config->channel)
        || (config->bits_per_sample != info->bits);
    config->sample_rate = info->sample_rates;
    config->channel = info->channels;
    config->bits_per_sample = info->bits;
    ESP_LOGD(TAG, "RECV element info, from: %s-%p, next: %p, self: %s-%p, type: %x, state: %s, rate: %d, ch: %d, bits: %d",
             OBJ_GET_TAG(el), el, esp_gmf_node_for_next((esp_gmf_node_t *)el), OBJ_GET_TAG(self), self, evt->type,
             esp_gmf_event_get_state_str(state), info->sample_rates, info->channels, info->bits);
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_howl_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_howl_t *howl = (esp_gmf_howl_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(howl);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_howl_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t cap = {0};
    cap.cap_eightcc = ESP_GMF_CAPS_AUDIO_HOWL;
    cap.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &cap);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_howl_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = NULL;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_howl_reset(esp_gmf_element_handle_t handle, void *para)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_howl_t *howl = (esp_gmf_howl_t *)handle;
    esp_gmf_port_t *in_port = ESP_GMF_ELEMENT_GET(handle)->in;
    if (howl->howl_hd) {
        esp_ae_err_t ret = esp_ae_howl_reset(howl->howl_hd);
        if (ret != ESP_AE_ERR_OK) {
            return ESP_GMF_JOB_ERR_FAIL;
        }
    }
    if (howl->cached_payload != NULL) {
        esp_gmf_cache_reset(howl->cached_payload);
    }
    howl->cur_pts = 0;
    if (howl->origin_in_load != NULL && in_port != NULL) {
        esp_gmf_err_io_t load_ret = esp_gmf_port_release_in(in_port, howl->origin_in_load, ESP_GMF_MAX_DELAY);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGW(TAG, "Failed to release input port during reset, ret: %d", load_ret);
        }
        howl->origin_in_load = NULL;
    }
    ESP_LOGD(TAG, "HOWL reset");
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_howl_init(esp_ae_howl_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_howl_t *howl = esp_gmf_oal_calloc(1, sizeof(esp_gmf_howl_t));
    ESP_GMF_MEM_VERIFY(TAG, howl, return ESP_GMF_ERR_MEMORY_LACK, "HOWL", sizeof(esp_gmf_howl_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)howl;
    obj->new_obj = esp_gmf_howl_new;
    obj->del_obj = esp_gmf_howl_destroy;
    esp_ae_howl_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(esp_ae_howl_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto HOWL_INIT_FAIL;}, "howl configuration", sizeof(esp_ae_howl_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_howl_cfg_t));
    if (config) {
        memcpy(cfg, config, sizeof(esp_ae_howl_cfg_t));
    } else {
        esp_ae_howl_cfg_t dcfg = DEFAULT_ESP_GMF_HOWL_CONFIG();
        memcpy(cfg, &dcfg, sizeof(esp_ae_howl_cfg_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_howl");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto HOWL_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                      ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(howl, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto HOWL_INIT_FAIL, "Failed to initialize howl element");
    ESP_GMF_ELEMENT_GET(howl)->ops.open = esp_gmf_howl_open;
    ESP_GMF_ELEMENT_GET(howl)->ops.process = esp_gmf_howl_process;
    ESP_GMF_ELEMENT_GET(howl)->ops.close = esp_gmf_howl_close;
    ESP_GMF_ELEMENT_GET(howl)->ops.event_receiver = howl_received_event_handler;
    ESP_GMF_ELEMENT_GET(howl)->ops.load_caps = _load_howl_caps_func;
    ESP_GMF_ELEMENT_GET(howl)->ops.load_methods = _load_howl_methods_func;
    ESP_GMF_ELEMENT_GET(howl)->ops.reset = esp_gmf_howl_reset;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
HOWL_INIT_FAIL:
    esp_gmf_howl_destroy(obj);
    return ret;
}
