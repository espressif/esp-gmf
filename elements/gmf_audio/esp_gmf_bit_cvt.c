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
#include "esp_gmf_bit_cvt.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"

/**
 * @brief  Audio bit conversion context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t parent;                /*!< The GMF bit cvt handle */
    esp_ae_bit_cvt_handle_t bit_hd;                /*!< The audio effects bit cvt handle */
    uint8_t                 in_bytes_per_sample;   /*!< Source bytes number of per sampling point */
    uint8_t                 out_bytes_per_sample;  /*!< Dest bytes number of per sampling point */
    bool                    need_reopen : 1;       /*!< Whether need to reopen.
                                                        True: Execute the close function first, then execute the open function
                                                        False: Do nothing */
    bool                    bypass : 1;            /*!< Whether bypass. True: need bypass. False: needn't bypass */
} esp_gmf_bit_cvt_t;

static const char *TAG = "ESP_GMF_BIT_CVT";

static esp_gmf_err_t __set_dest_bits(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                     uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t dest_bits = (uint8_t)(*buf);
    return esp_gmf_bit_cvt_set_dest_bits(handle, dest_bits);
}

static esp_gmf_err_t esp_gmf_bit_cvt_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_bit_cvt_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_bit_cvt_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_bit_cvt_t *bit_cvt = (esp_gmf_bit_cvt_t *)self;
    esp_ae_bit_cvt_cfg_t *bit_info = (esp_ae_bit_cvt_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, bit_info, {return ESP_GMF_JOB_ERR_FAIL;});
    esp_ae_bit_cvt_open(bit_info, &bit_cvt->bit_hd);
    ESP_GMF_CHECK(TAG, bit_cvt->bit_hd, {return ESP_GMF_JOB_ERR_FAIL;}, "Failed to create bit conversion handle");
    bit_cvt->in_bytes_per_sample = (bit_info->src_bits >> 3) * bit_info->channel;
    bit_cvt->out_bytes_per_sample = (bit_info->dest_bits >> 3) * bit_info->channel;
    GMF_AUDIO_UPDATE_SND_INFO(self, bit_info->sample_rate, bit_info->dest_bits, bit_info->channel);
    ESP_LOGD(TAG, "Open, rate: %ld, channel: %d, src_bits: %d, dest_bits: %d",
             bit_info->sample_rate, bit_info->channel, bit_info->src_bits, bit_info->dest_bits);
    bit_cvt->need_reopen = false;
    bit_cvt->bypass = (bit_info->src_bits == bit_info->dest_bits);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_bit_cvt_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_bit_cvt_t *bit_cvt = (esp_gmf_bit_cvt_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    if (bit_cvt->bit_hd != NULL) {
        esp_ae_bit_cvt_close(bit_cvt->bit_hd);
        bit_cvt->bit_hd = NULL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_bit_cvt_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_bit_cvt_t *bit_cvt = (esp_gmf_bit_cvt_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (bit_cvt->need_reopen) {
        esp_gmf_bit_cvt_close(self, NULL);
        out_len = esp_gmf_bit_cvt_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "Bit conversion reopen failed");
            return out_len;
        }
    }
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int samples_num = ESP_GMF_ELEMENT_GET(bit_cvt)->in_attr.data_size / (bit_cvt->in_bytes_per_sample);
    int bytes = samples_num * bit_cvt->in_bytes_per_sample;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    samples_num = in_load->valid_size / (bit_cvt->in_bytes_per_sample);
    bytes = samples_num * bit_cvt->out_bytes_per_sample;
    if ((bit_cvt->in_bytes_per_sample * samples_num != in_load->valid_size) || (load_ret < ESP_GMF_IO_OK)) {
        ESP_LOGE(TAG, "Invalid in load size %d, ret %d", in_load->valid_size, load_ret);
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __bit_release;
    }
    if ((bit_cvt->bypass) && (in_port->is_shared == 1)) {
        // This case bit conversion is do bypass
        out_load = in_load;
    }
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, samples_num ? bytes : in_load->buf_length, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, { goto __bit_release;});
    if (samples_num) {
        esp_ae_err_t ret = esp_ae_bit_cvt_process(bit_cvt->bit_hd, samples_num, (unsigned char *)in_load->buf, (unsigned char *)out_load->buf);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __bit_release;}, "Bit conversion process error, ret: %d", ret);
    }
    ESP_LOGV(TAG, "Samples: %d, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d",
             samples_num, in_load, in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done,
             out_load, out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    out_load->valid_size = samples_num * bit_cvt->out_bytes_per_sample;
    out_load->pts = in_load->pts;
    out_load->is_done = in_load->is_done;
    if (out_load->valid_size > 0) {
        esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, out_load->valid_size);
    }
    if (in_load->is_done) {
        out_len = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "Bit conversion is done, out len: %d", out_load->valid_size);
    }
__bit_release:
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

static esp_gmf_err_t bit_cvt_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_ae_bit_cvt_cfg_t *config = (esp_ae_bit_cvt_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_bit_cvt_t *bit_cvt = (esp_gmf_bit_cvt_t *)self;
    bit_cvt->need_reopen = (config->sample_rate != info->sample_rates) || (info->channels != config->channel) || (config->src_bits != info->bits);
    config->sample_rate = info->sample_rates;
    config->channel = info->channels;
    config->src_bits = info->bits;
    ESP_LOGD(TAG, "RECV info, from: %s-%p, next: %p, self: %s-%p, type: %x, state: %s, rate: %d, ch: %d, bits: %d",
             OBJ_GET_TAG(el), el, esp_gmf_node_for_next((esp_gmf_node_t *)el), OBJ_GET_TAG(self), self, evt->type,
             esp_gmf_event_get_state_str(state), info->sample_rates, info->channels, info->bits);
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_bit_cvt_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_bit_cvt_t *bit_cvt = (esp_gmf_bit_cvt_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(bit_cvt);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_bit_cvt_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_BIT_CONVERT;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_bit_cvt_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_err_t ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(BIT_CVT, SET_DEST_BITS, BITS),
                                   ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append argument");
    ret = esp_gmf_method_append(&method, AMETHOD(BIT_CVT, SET_DEST_BITS), __set_dest_bits, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(BIT_CVT, SET_DEST_BITS));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_bit_cvt_set_dest_bits(esp_gmf_element_handle_t handle, uint8_t dest_bits)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_bit_cvt_cfg_t *cfg = (esp_ae_bit_cvt_cfg_t *)OBJ_GET_CFG(handle);
    if (cfg) {
        if (cfg->dest_bits == dest_bits) {
            return ESP_GMF_ERR_OK;
        }
        cfg->dest_bits = dest_bits;
        esp_gmf_bit_cvt_t *bit_cvt = (esp_gmf_bit_cvt_t *)handle;
        bit_cvt->need_reopen = true;
        return ESP_GMF_ERR_OK;
    }
    ESP_LOGE(TAG, "Failed to set dest bits, cfg is NULL");
    return ESP_GMF_ERR_FAIL;
}

esp_gmf_err_t esp_gmf_bit_cvt_init(esp_ae_bit_cvt_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_bit_cvt_t *bit_cvt = esp_gmf_oal_calloc(1, sizeof(esp_gmf_bit_cvt_t));
    ESP_GMF_MEM_VERIFY(TAG, bit_cvt, {return ESP_GMF_ERR_MEMORY_LACK;}, "bit conversion", sizeof(esp_gmf_bit_cvt_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)bit_cvt;
    obj->new_obj = esp_gmf_bit_cvt_new;
    obj->del_obj = esp_gmf_bit_cvt_destroy;
    esp_ae_bit_cvt_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(esp_ae_bit_cvt_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto BIT_CVT_INIT_FAIL;}, "bit conversion configuration", sizeof(esp_ae_bit_cvt_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_bit_cvt_cfg_t));
    if (config) {
        memcpy(cfg, config, sizeof(esp_ae_bit_cvt_cfg_t));
    } else {
        esp_ae_bit_cvt_cfg_t dcfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
        memcpy(cfg, &dcfg, sizeof(esp_ae_bit_cvt_cfg_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_bit_cvt");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto BIT_CVT_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(bit_cvt, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto BIT_CVT_INIT_FAIL, "Failed to initialize bit conversion element");
    ESP_GMF_ELEMENT_GET(bit_cvt)->ops.open = esp_gmf_bit_cvt_open;
    ESP_GMF_ELEMENT_GET(bit_cvt)->ops.process = esp_gmf_bit_cvt_process;
    ESP_GMF_ELEMENT_GET(bit_cvt)->ops.close = esp_gmf_bit_cvt_close;
    ESP_GMF_ELEMENT_GET(bit_cvt)->ops.event_receiver = bit_cvt_received_event_handler;
    ESP_GMF_ELEMENT_GET(bit_cvt)->ops.load_caps = _load_bit_cvt_caps_func;
    ESP_GMF_ELEMENT_GET(bit_cvt)->ops.load_methods = _load_bit_cvt_methods_func;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
BIT_CVT_INIT_FAIL:
    esp_gmf_bit_cvt_destroy(obj);
    return ret;
}
