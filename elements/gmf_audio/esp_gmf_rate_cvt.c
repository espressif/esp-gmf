/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_node.h"
#include "esp_gmf_rate_cvt.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"

/**
 * @brief  Audio rate conversion context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t  parent;            /*!< The GMF rate cvt handle */
    esp_ae_rate_cvt_handle_t rate_hd;           /*!< The audio effects rate cvt handle */
    uint8_t                  bytes_per_sample;  /*!< Bytes number of per sampling point */
    bool                     need_reopen : 1;   /*!< Whether need to reopen.
                                                     True: Execute the close function first, then execute the open function
                                                     False: Do nothing */
    bool                     bypass : 1;        /*!< Whether bypass. True: need bypass. False: needn't bypass */
} esp_gmf_rate_cvt_t;

static const char *TAG = "ESP_GMF_RATE_CVT";

static esp_gmf_err_t __rate_cvt_set_dest_rate(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                              uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint32_t dest_rate = *((uint32_t *)buf);
    return esp_gmf_rate_cvt_set_dest_rate(handle, dest_rate);
}

static esp_gmf_err_t esp_gmf_rate_cvt_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_rate_cvt_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_rate_cvt_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_rate_cvt_t *rate_cvt = (esp_gmf_rate_cvt_t *)self;
    esp_ae_rate_cvt_cfg_t *rate_info = (esp_ae_rate_cvt_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, rate_info, {return ESP_GMF_JOB_ERR_FAIL;});
    rate_cvt->bytes_per_sample = (rate_info->bits_per_sample >> 3) * rate_info->channel;
    esp_ae_rate_cvt_open(rate_info, &rate_cvt->rate_hd);
    ESP_GMF_CHECK(TAG, rate_cvt->rate_hd, {return ESP_GMF_JOB_ERR_FAIL;}, "Failed to create rate conversion handle");
    GMF_AUDIO_UPDATE_SND_INFO(self, rate_info->dest_rate, rate_info->bits_per_sample, rate_info->channel);
    ESP_LOGD(TAG, "Open, src: %"PRIu32", dest: %"PRIu32", ch: %d, bits: %d",
             rate_info->src_rate, rate_info->dest_rate, rate_info->channel, rate_info->bits_per_sample);
    rate_cvt->need_reopen = false;
    rate_cvt->bypass = rate_info->src_rate == rate_info->dest_rate;
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_rate_cvt_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_rate_cvt_t *rate_cvt = (esp_gmf_rate_cvt_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    if (rate_cvt->rate_hd != NULL) {
        esp_ae_rate_cvt_close(rate_cvt->rate_hd);
        rate_cvt->rate_hd = NULL;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_rate_cvt_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_rate_cvt_t *rate_cvt = (esp_gmf_rate_cvt_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (rate_cvt->need_reopen) {
        esp_gmf_rate_cvt_close(self, NULL);
        out_len = esp_gmf_rate_cvt_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "Rate conversion reopen failed");
            return out_len;
        }
    }
    esp_ae_err_t ret = ESP_AE_ERR_OK;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int samples_num = ESP_GMF_ELEMENT_GET(rate_cvt)->in_attr.data_size / (rate_cvt->bytes_per_sample);
    int bytes = samples_num * rate_cvt->bytes_per_sample;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    samples_num = in_load->valid_size / (rate_cvt->bytes_per_sample);
    bytes = samples_num * rate_cvt->bytes_per_sample;
    if((bytes != in_load->valid_size) || (load_ret < ESP_GMF_IO_OK)) {
        ESP_LOGE(TAG, "Invalid in load size %d, ret %d", in_load->valid_size, load_ret);
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __rate_release;
    }
    uint32_t out_samples_num = 0;
    if (samples_num) {
        ret = esp_ae_rate_cvt_get_max_out_sample_num(rate_cvt->rate_hd, samples_num, &out_samples_num);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __rate_release;}, "Failed to get resample out size, ret: %d", ret);
    }
    int acq_out_size = out_samples_num == 0 ? in_load->buf_length : out_samples_num * rate_cvt->bytes_per_sample;
    if (rate_cvt->bypass && (in_port->is_shared == true)) {
        // This case rate conversion is do bypass
        out_load = in_load;
    }
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, acq_out_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, {goto __rate_release;});
    if (samples_num) {
        ret = esp_ae_rate_cvt_process(rate_cvt->rate_hd, (unsigned char *)in_load->buf, samples_num,
                                      (unsigned char *)out_load->buf, &out_samples_num);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __rate_release;}, "Rate conversion process error, ret: %d", ret);
    }
    out_load->valid_size = out_samples_num * rate_cvt->bytes_per_sample;
    out_load->pts = in_load->pts;
    out_load->is_done = in_load->is_done;
    ESP_LOGV(TAG, "Out Samples: %ld, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d", out_samples_num, in_load, in_load->buf,
             in_load->valid_size, in_load->buf_length, in_load->is_done, out_load,
             out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, out_load->valid_size);
    if (in_load->is_done) {
        out_len = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "Rate convert done, out len: %d", out_load->valid_size);
    }
__rate_release:
    // Release in and out port
    if (out_load != NULL) {
        load_ret = esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "OUT port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    if (in_load != NULL) {
        load_ret = esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "IN port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    return out_len;
}

static esp_gmf_err_t rate_cvt_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, ctx, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, evt, {return ESP_GMF_ERR_INVALID_ARG;});
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
    esp_ae_rate_cvt_cfg_t *config = (esp_ae_rate_cvt_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_rate_cvt_t *rate_cvt = (esp_gmf_rate_cvt_t *)self;
    rate_cvt->need_reopen = (config->src_rate != info->sample_rates) || (info->channels != config->channel) || (config->bits_per_sample != info->bits);
    config->src_rate = info->sample_rates;
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

static esp_gmf_err_t esp_gmf_rate_cvt_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_rate_cvt_t *rate_cvt = (esp_gmf_rate_cvt_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(rate_cvt);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_rate_cvt_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_RATE_CONVERT;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_rate_cvt_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_err_t ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(RATE_CVT, SET_DEST_RATE, RATE),
                                                 ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append RATE argument");
    ret = esp_gmf_method_append(&method, AMETHOD(RATE_CVT, SET_DEST_RATE), __rate_cvt_set_dest_rate, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(RATE_CVT, SET_DEST_RATE));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_rate_cvt_set_dest_rate(esp_gmf_element_handle_t handle, uint32_t dest_rate)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_rate_cvt_cfg_t *cfg = (esp_ae_rate_cvt_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    if (cfg->dest_rate == dest_rate) {
        return ESP_GMF_ERR_OK;
    }
    cfg->dest_rate = dest_rate;
    esp_gmf_rate_cvt_t *rate_cvt = (esp_gmf_rate_cvt_t *)handle;
    rate_cvt->need_reopen = true;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_rate_cvt_init(esp_ae_rate_cvt_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_rate_cvt_t *rate_cvt = esp_gmf_oal_calloc(1, sizeof(esp_gmf_rate_cvt_t));
    ESP_GMF_MEM_VERIFY(TAG, rate_cvt, {return ESP_GMF_ERR_MEMORY_LACK;}, "rate conversion", sizeof(esp_gmf_rate_cvt_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)rate_cvt;
    obj->new_obj = esp_gmf_rate_cvt_new;
    obj->del_obj = esp_gmf_rate_cvt_destroy;
    esp_ae_rate_cvt_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(esp_ae_rate_cvt_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto RATE_CVT_INIT_FAIL;}, "Rate conversion configuration", sizeof(esp_ae_rate_cvt_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_rate_cvt_cfg_t));
    if (config) {
        memcpy(cfg, config, sizeof(esp_ae_rate_cvt_cfg_t));
    } else {
        esp_ae_rate_cvt_cfg_t dcfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
        memcpy(cfg, &dcfg, sizeof(esp_ae_rate_cvt_cfg_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_rate_cvt");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto RATE_CVT_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(rate_cvt, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto RATE_CVT_INIT_FAIL, "Failed to initialize rate conversion element");
    ESP_GMF_ELEMENT_GET(rate_cvt)->ops.open = esp_gmf_rate_cvt_open;
    ESP_GMF_ELEMENT_GET(rate_cvt)->ops.process = esp_gmf_rate_cvt_process;
    ESP_GMF_ELEMENT_GET(rate_cvt)->ops.close = esp_gmf_rate_cvt_close;
    ESP_GMF_ELEMENT_GET(rate_cvt)->ops.event_receiver = rate_cvt_received_event_handler;
    ESP_GMF_ELEMENT_GET(rate_cvt)->ops.load_caps = _load_rate_cvt_caps_func;
    ESP_GMF_ELEMENT_GET(rate_cvt)->ops.load_methods = _load_rate_cvt_methods_func;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
RATE_CVT_INIT_FAIL:
    esp_gmf_rate_cvt_destroy(obj);
    return ret;
}
