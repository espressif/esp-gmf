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
#include "esp_gmf_alc.h"
#include "esp_gmf_args_desc.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"

#define GMF_ALC_DEFAULT_MAX_CHANNEL 2
/**
 * @brief  Audio ALC context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t parent;            /*!< The GMF alc handle */
    esp_ae_alc_handle_t     alc_hd;            /*!< The audio effects alc handle */
    uint8_t                 bytes_per_sample;  /*!< Bytes number of per sampling point */
    int8_t                 *gain;              /*!< The modified gain value of a certain channel number */
    int8_t                  max_ch;            /*!< The maximum channel number */
    bool                    need_reopen : 1;   /*!< Whether need to reopen.
                                                    True: Execute the close function first, then execute the open function
                                                    False: Do nothing */
} esp_gmf_alc_t;

static const char *TAG = "ESP_GMF_ALC";

static esp_gmf_err_t __alc_set_gain(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_args_desc_t *alc_desc = arg_desc;
    uint8_t idx = (uint8_t)(*buf);
    alc_desc = alc_desc->next;
    int8_t gain = (int8_t)(*(buf + alc_desc->offset));
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    if (idx == 0xFF) {
        // Apply gain to all channels
        esp_ae_alc_cfg_t *config = (esp_ae_alc_cfg_t *)OBJ_GET_CFG(handle);
        if (config) {
            for (uint8_t i = 0; i < config->channel; i++) {
                ret |= esp_gmf_alc_set_gain(handle, i, gain);
            }
        }
    } else {
        ret = esp_gmf_alc_set_gain(handle, idx, gain);
    }
    return ret;
}

static esp_gmf_err_t __alc_get_gain(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_args_desc_t *alc_desc = arg_desc;
    uint8_t idx = (uint8_t)(*buf);
    alc_desc = alc_desc->next;
    int8_t *gain = (int8_t *)(buf + alc_desc->offset);
    return esp_gmf_alc_get_gain(handle, idx, gain);
}

static esp_gmf_err_t esp_gmf_alc_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_alc_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_alc_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_alc_t *alc = (esp_gmf_alc_t *)self;
    esp_ae_alc_cfg_t *config = (esp_ae_alc_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, { return ESP_GMF_JOB_ERR_FAIL;});
    alc->bytes_per_sample = (config->bits_per_sample >> 3) * config->channel;
    esp_ae_alc_open(config, &alc->alc_hd);
    ESP_GMF_CHECK(TAG, alc->alc_hd, { return ESP_GMF_JOB_ERR_FAIL;}, "Failed to create alc handle");
    GMF_AUDIO_UPDATE_SND_INFO(self, config->sample_rate, config->bits_per_sample, config->channel);
    for (size_t i = 0; i < config->channel; i++) {
        esp_ae_err_t ret = esp_ae_alc_set_gain(alc->alc_hd, i, alc->gain[i]);
        if (ret != ESP_AE_ERR_OK) {
            return ESP_GMF_JOB_ERR_FAIL;
        }
    }
    alc->need_reopen = false;
    ESP_LOGD(TAG, "Open, %p", self);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_alc_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_alc_t *alc = (esp_gmf_alc_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    if (alc->alc_hd != NULL) {
        esp_ae_alc_close(alc->alc_hd);
        alc->alc_hd = NULL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_alc_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_alc_t *alc = (esp_gmf_alc_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (alc->need_reopen) {
        esp_gmf_alc_close(self, NULL);
        out_len = esp_gmf_alc_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "ALC reopen failed");
            return out_len;
        }
    }
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int samples_num = ESP_GMF_ELEMENT_GET(alc)->in_attr.data_size / (alc->bytes_per_sample);
    int bytes = samples_num * alc->bytes_per_sample;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    samples_num = in_load->valid_size / (alc->bytes_per_sample);
    bytes = samples_num * alc->bytes_per_sample;
    if ((bytes != in_load->valid_size) || (load_ret < ESP_GMF_IO_OK)) {
        ESP_LOGE(TAG, "Invalid in load size %d, ret %d", in_load->valid_size, load_ret);
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __alc_release;
    }
    if (in_port->is_shared == 1) {
        out_load = in_load;
    }
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, samples_num ? bytes : in_load->buf_length, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, { goto __alc_release;});
    if (samples_num) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)self)->lock);
        esp_ae_err_t ret = esp_ae_alc_process(alc->alc_hd, samples_num, in_load->buf, out_load->buf);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)self)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __alc_release;}, "ALC process error %d", ret);
    }
    ESP_LOGV(TAG, "Samples: %d, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d",
             samples_num, in_load, in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done,
             out_load, out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    out_load->valid_size = samples_num * alc->bytes_per_sample;
    out_load->is_done = in_load->is_done;
    out_load->pts = in_load->pts;
    esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, out_load->valid_size);
    if (in_load->is_done) {
        out_len = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "ALC done, out len: %d", out_load->valid_size);
    }
__alc_release:
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

static esp_gmf_err_t alc_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_ae_alc_cfg_t *config = (esp_ae_alc_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_alc_t *alc = (esp_gmf_alc_t *)self;
    if (info->channels > alc->max_ch) {
        alc->gain = esp_gmf_oal_realloc(alc->gain, config->channel * sizeof(*alc->gain));
        ESP_GMF_MEM_VERIFY(TAG, alc->gain, return ESP_GMF_ERR_MEMORY_LACK, "alc gain", config->channel * sizeof(*alc->gain));
        alc->max_ch = info->channels;
    }
    alc->need_reopen = (config->sample_rate != info->sample_rates) || (info->channels != config->channel) || (config->bits_per_sample != info->bits);
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

static esp_gmf_err_t esp_gmf_alc_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_alc_t *alc = (esp_gmf_alc_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    if (alc->gain) {
        esp_gmf_oal_free(alc->gain);
        alc->gain = NULL;
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(alc);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_alc_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_ALC;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_alc_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_args_desc_t *get_args = NULL;
    esp_gmf_err_t ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(ALC, SET_GAIN, IDX),
                                                 ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append index argument");
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(ALC, SET_GAIN, GAIN), ESP_GMF_ARGS_TYPE_INT8,
                                   sizeof(int8_t), sizeof(uint8_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append gain argument");
    ret = esp_gmf_method_append(&method, AMETHOD(ALC, SET_GAIN), __alc_set_gain, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(ALC, SET_GAIN));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy argument");
    ret = esp_gmf_method_append(&method, AMETHOD(ALC, GET_GAIN), __alc_get_gain, get_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(ALC, GET_GAIN));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_alc_set_gain(esp_gmf_element_handle_t handle, uint8_t idx, int8_t gain)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_alc_t *alc = (esp_gmf_alc_t *)handle;
    if (idx >= alc->max_ch) {
        ESP_LOGE(TAG, "Gain index %d is out of range", idx);
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (alc->alc_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_alc_set_gain(alc->alc_hd, idx, gain);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        if (ret != ESP_AE_ERR_OK) {
            return ESP_GMF_JOB_ERR_FAIL;
        }
    }
    alc->gain[idx] = gain;
    return ESP_GMF_JOB_ERR_OK;
}

esp_gmf_err_t esp_gmf_alc_get_gain(esp_gmf_element_handle_t handle, uint8_t idx, int8_t *gain)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, gain, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_alc_t *alc = (esp_gmf_alc_t *)handle;
    if(idx > alc->max_ch) {
        ESP_LOGE(TAG, "Gain index %d is out of range", idx);
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (alc->alc_hd) {
        esp_ae_err_t ret = esp_ae_alc_get_gain(alc->alc_hd, idx, gain);
        if (ret != ESP_AE_ERR_OK) {
            return ESP_GMF_JOB_ERR_FAIL;
        }
        return ESP_GMF_JOB_ERR_OK;
    }
    *gain = alc->gain[idx];
    return ESP_GMF_JOB_ERR_OK;
}

esp_gmf_err_t esp_gmf_alc_init(esp_ae_alc_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_alc_t *alc = esp_gmf_oal_calloc(1, sizeof(esp_gmf_alc_t));
    ESP_GMF_MEM_VERIFY(TAG, alc, {return ESP_GMF_ERR_MEMORY_LACK;}, "ALC", sizeof(esp_gmf_alc_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)alc;
    obj->new_obj = esp_gmf_alc_new;
    obj->del_obj = esp_gmf_alc_destroy;
    alc->max_ch = GMF_ALC_DEFAULT_MAX_CHANNEL;
    esp_ae_alc_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(esp_ae_alc_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, ret = ESP_GMF_ERR_MEMORY_LACK; goto ALC_INIT_FAIL;, "alc configuration", sizeof(esp_ae_alc_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_alc_cfg_t));
    if (config) {
        alc->max_ch = config->channel > 0 ? config->channel : GMF_ALC_DEFAULT_MAX_CHANNEL;
        memcpy(cfg, config, sizeof(esp_ae_alc_cfg_t));
    } else {
        esp_ae_alc_cfg_t dcfg = DEFAULT_ESP_GMF_ALC_CONFIG();
        alc->max_ch = dcfg.channel;
        memcpy(cfg, &dcfg, sizeof(esp_ae_alc_cfg_t));
    }
    alc->gain = esp_gmf_oal_calloc(1, alc->max_ch * sizeof(int8_t));
    ESP_GMF_MEM_VERIFY(TAG, alc->gain, goto ALC_INIT_FAIL, "alc gain", alc->max_ch * sizeof(int8_t));
    ret = esp_gmf_obj_set_tag(obj, "aud_alc");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ALC_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                    ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                    ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(alc, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ALC_INIT_FAIL, "Failed to initialize alc element");
    ESP_GMF_ELEMENT_GET(alc)->ops.open = esp_gmf_alc_open;
    ESP_GMF_ELEMENT_GET(alc)->ops.process = esp_gmf_alc_process;
    ESP_GMF_ELEMENT_GET(alc)->ops.close = esp_gmf_alc_close;
    ESP_GMF_ELEMENT_GET(alc)->ops.event_receiver = alc_received_event_handler;
    ESP_GMF_ELEMENT_GET(alc)->ops.load_caps = _load_alc_caps_func;
    ESP_GMF_ELEMENT_GET(alc)->ops.load_methods = _load_alc_methods_func;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
ALC_INIT_FAIL:
    esp_gmf_alc_destroy(obj);
    return ret;
}
