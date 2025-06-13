/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_node.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_ch_cvt.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"

/**
 * @brief  Audio channel conversion context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t parent;                /*!< The GMF channel cvt handle */
    esp_ae_ch_cvt_handle_t  ch_hd;                 /*!< The audio effects channel cvt handle */
    uint8_t                 in_bytes_per_sample;   /*!< Source bytes number of per sampling point */
    uint8_t                 out_bytes_per_sample;  /*!< Dest bytes number of per sampling point */
    bool                    need_reopen : 1;       /*!< Whether need to reopen.
                                                        True: Execute the close function first, then execute the open function
                                                        False: Do nothing */
    bool                    bypass : 1;            /*!< Whether bypass. True: need bypass. False: needn't bypass */
} esp_gmf_ch_cvt_t;

static const char *TAG = "ESP_GMF_CH_CVT";

static inline esp_gmf_err_t dupl_esp_ae_ch_cvt_cfg(esp_ae_ch_cvt_cfg_t *config, esp_ae_ch_cvt_cfg_t **new_config)
{
    void *sub_cfg = NULL;
    *new_config = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, *new_config, {return ESP_GMF_ERR_MEMORY_LACK;}, "channel conversion configuration", sizeof(*config));
    memcpy(*new_config, config, sizeof(*config));
    if (config->weight && (config->weight_len > 0)) {
        sub_cfg = esp_gmf_oal_calloc(1, config->weight_len);
        ESP_GMF_MEM_VERIFY(TAG, sub_cfg, {esp_gmf_oal_free(*new_config); return ESP_GMF_ERR_MEMORY_LACK;},
                           "weight array", (int)config->weight_len);
        memcpy(sub_cfg, config->weight, config->weight_len);
        (*new_config)->weight = sub_cfg;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static inline void free_esp_ae_ch_cvt_cfg(esp_ae_ch_cvt_cfg_t *config)
{
    if (config) {
        if (config->weight) {
            esp_gmf_oal_free(config->weight);
            config->weight = NULL;
            config->weight_len = 0;
        }
        esp_gmf_oal_free(config);
    }
}

static esp_gmf_err_t __set_dest_ch(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                   uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t dest_ch = (uint8_t)(*buf);
    return esp_gmf_ch_cvt_set_dest_channel(handle, dest_ch);
}

static esp_gmf_err_t esp_gmf_ch_cvt_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_ch_cvt_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_ch_cvt_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_ch_cvt_t *ch_cvt = (esp_gmf_ch_cvt_t *)self;
    esp_ae_ch_cvt_cfg_t *ch_info = (esp_ae_ch_cvt_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, ch_info, {return ESP_GMF_JOB_ERR_FAIL;});
    esp_ae_ch_cvt_open(ch_info, &ch_cvt->ch_hd);
    ESP_GMF_CHECK(TAG, ch_cvt->ch_hd, {return ESP_GMF_JOB_ERR_FAIL;}, "Failed to create channel conversion handle");
    ch_cvt->in_bytes_per_sample = (ch_info->bits_per_sample >> 3) * ch_info->src_ch;
    ch_cvt->out_bytes_per_sample = (ch_info->bits_per_sample >> 3) * ch_info->dest_ch;
    GMF_AUDIO_UPDATE_SND_INFO(self, ch_info->sample_rate, ch_info->bits_per_sample, ch_info->dest_ch);
    ESP_LOGD(TAG, "Open, rate: %ld, bits: %d, src_channel: %d, dest_channel: %d",
             ch_info->sample_rate, ch_info->bits_per_sample, ch_info->src_ch, ch_info->dest_ch);
    ch_cvt->need_reopen = false;
    ch_cvt->bypass = ch_info->src_ch == ch_info->dest_ch;
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_ch_cvt_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_ch_cvt_t *ch_cvt = (esp_gmf_ch_cvt_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    if (ch_cvt->ch_hd != NULL) {
        esp_ae_ch_cvt_close(ch_cvt->ch_hd);
        ch_cvt->ch_hd = NULL;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_ch_cvt_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_ch_cvt_t *ch_cvt = (esp_gmf_ch_cvt_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (ch_cvt->need_reopen) {
        esp_gmf_ch_cvt_close(self, NULL);
        out_len = esp_gmf_ch_cvt_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "Channel conversion reopen failed");
            return out_len;
        }
    }
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int samples_num = ESP_GMF_ELEMENT_GET(ch_cvt)->in_attr.data_size / (ch_cvt->in_bytes_per_sample);
    int bytes = samples_num * ch_cvt->in_bytes_per_sample;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    samples_num = in_load->valid_size / (ch_cvt->in_bytes_per_sample);
    bytes = samples_num * ch_cvt->out_bytes_per_sample;
    if ((ch_cvt->in_bytes_per_sample * samples_num != in_load->valid_size) || (load_ret < ESP_GMF_IO_OK)) {
        ESP_LOGE(TAG, "Invalid in load size %d, ret %d", in_load->valid_size, load_ret);
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __ch_release;
    }
    if (ch_cvt->bypass && (in_port->is_shared == true)) {
        // This case channel conversion is do bypass
        out_load = in_load;
    }
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, samples_num ? bytes : in_load->buf_length, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, { goto __ch_release;});
    if (samples_num) {
        esp_ae_err_t ret = esp_ae_ch_cvt_process(ch_cvt->ch_hd, samples_num, (unsigned char *)in_load->buf, (unsigned char *)out_load->buf);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __ch_release;}, "Channel conversion process error, ret: %d", ret);
    }
    out_load->valid_size = samples_num * ch_cvt->out_bytes_per_sample;
    out_load->pts = in_load->pts;
    out_load->is_done = in_load->is_done;
    ESP_LOGV(TAG, "Samples: %d, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d",
             samples_num, in_load, in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done,
             out_load, out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    if (out_load->valid_size > 0) {
        esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, out_load->valid_size);
    }
    if (in_load->is_done) {
        out_len = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "The channel cvt done, out len: %d", out_load->valid_size);
    }
__ch_release:
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

static esp_gmf_err_t ch_cvt_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_ae_ch_cvt_cfg_t *config = (esp_ae_ch_cvt_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_ch_cvt_t *ch_cvt = (esp_gmf_ch_cvt_t *)self;
    ch_cvt->need_reopen = (config->sample_rate != info->sample_rates) || (info->channels != config->src_ch) || (config->bits_per_sample != info->bits);
    config->sample_rate = info->sample_rates;
    config->src_ch = info->channels;
    config->bits_per_sample = info->bits;
    ESP_LOGD(TAG, "RECV info, from: %s-%p, next: %p, self: %s-%p, type: %x, state: %s, rate: %d, ch: %d, bits: %d",
             OBJ_GET_TAG(el), el, esp_gmf_node_for_next((esp_gmf_node_t *)el), OBJ_GET_TAG(self), self, evt->type,
             esp_gmf_event_get_state_str(state), info->sample_rates, info->channels, info->bits);
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_ch_cvt_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_ch_cvt_t *ch_cvt = (esp_gmf_ch_cvt_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    free_esp_ae_ch_cvt_cfg(OBJ_GET_CFG(self));
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(ch_cvt);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_channel_cvt_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_CHANNEL_CONVERT;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_channel_cvt_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_err_t ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(CH_CVT, SET_DEST_CH, CH), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append argument");
    ret = esp_gmf_method_append(&method, AMETHOD(CH_CVT, SET_DEST_CH), __set_dest_ch, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(CH_CVT, SET_DEST_CH));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_ch_cvt_set_dest_channel(esp_gmf_element_handle_t handle, uint8_t dest_ch)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_ch_cvt_cfg_t *cfg = (esp_ae_ch_cvt_cfg_t *)OBJ_GET_CFG(handle);
    if (cfg) {
        if (cfg->dest_ch == dest_ch) {
            return ESP_GMF_ERR_OK;
        }
        cfg->dest_ch = dest_ch;
        esp_gmf_ch_cvt_t *ch_cvt = (esp_gmf_ch_cvt_t *)handle;
        ch_cvt->need_reopen = true;
        return ESP_GMF_ERR_OK;
    }
    ESP_LOGE(TAG, "Failed to set dest channel, cfg is NULL");
    return ESP_GMF_ERR_FAIL;
}

esp_gmf_err_t esp_gmf_ch_cvt_init(esp_ae_ch_cvt_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_ch_cvt_t *ch_cvt = esp_gmf_oal_calloc(1, sizeof(esp_gmf_ch_cvt_t));
    ESP_GMF_MEM_VERIFY(TAG, ch_cvt, {return ESP_GMF_ERR_MEMORY_LACK;}, "channel conversion", sizeof(esp_gmf_ch_cvt_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)ch_cvt;
    obj->new_obj = esp_gmf_ch_cvt_new;
    obj->del_obj = esp_gmf_ch_cvt_destroy;
    esp_ae_ch_cvt_cfg_t *cfg = NULL;
    if (config) {
        dupl_esp_ae_ch_cvt_cfg(config, &cfg);
    } else {
        esp_ae_ch_cvt_cfg_t dcfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
        dupl_esp_ae_ch_cvt_cfg(&dcfg, &cfg);
    }
    ESP_GMF_CHECK(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto CH_CVT_INIT_FAIL;}, "Failed to allocate channel conversion configuration");
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_ch_cvt_cfg_t));
    ret = esp_gmf_obj_set_tag(obj, "aud_ch_cvt");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto CH_CVT_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(ch_cvt, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto CH_CVT_INIT_FAIL, "Failed to initialize channel conversion element");
    ESP_GMF_ELEMENT_GET(ch_cvt)->ops.open = esp_gmf_ch_cvt_open;
    ESP_GMF_ELEMENT_GET(ch_cvt)->ops.process = esp_gmf_ch_cvt_process;
    ESP_GMF_ELEMENT_GET(ch_cvt)->ops.close = esp_gmf_ch_cvt_close;
    ESP_GMF_ELEMENT_GET(ch_cvt)->ops.event_receiver = ch_cvt_received_event_handler;
    ESP_GMF_ELEMENT_GET(ch_cvt)->ops.load_caps = _load_channel_cvt_caps_func;
    ESP_GMF_ELEMENT_GET(ch_cvt)->ops.load_methods = _load_channel_cvt_methods_func;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
CH_CVT_INIT_FAIL:
    esp_gmf_ch_cvt_destroy(obj);
    return ret;
}
