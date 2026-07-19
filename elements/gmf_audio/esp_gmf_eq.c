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
#include "esp_gmf_eq.h"
#include "esp_gmf_args_desc.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"

/**
 * @brief  Audio equalizer context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t parent;             /*!< The GMF eq handle */
    esp_ae_eq_handle_t      eq_hd;              /*!< The audio effects eq handle */
    uint8_t                 bytes_per_sample;   /*!< Bytes number of per sampling point */
    bool                   *is_filter_enabled;  /*!< The flag of whether eq filter is enabled */
    bool                    need_reopen : 1;    /*!< Whether need to reopen.
                                                     True: Execute the close function first, then execute the open function
                                                     False: Do nothing */
} esp_gmf_eq_t;

static const char *TAG = "ESP_GMF_EQ";

static const esp_gmf_arg_constraint_t s_eq_index_constraint = {
    .index_count_config_path = "filter_num",
};

static const esp_gmf_arg_constraint_t s_eq_filter_type_constraint = {
    .minimum.u64 = ESP_AE_EQ_FILTER_HIGH_PASS,
    .maximum.u64 = ESP_AE_EQ_FILTER_LOW_SHELF,
    .step.u64 = 1,
};

static const esp_gmf_arg_constraint_t s_eq_q_constraint = {
    .minimum.f64 = 0.1,
    .maximum.f64 = 20.0,
    .step.f64 = 0.1,
};

static const esp_gmf_arg_constraint_t s_eq_gain_constraint = {
    .minimum.f64 = -15.0,
    .maximum.f64 = 15.0,
    .step.f64 = 0.1,
};

static const esp_gmf_arg_constraint_t s_eq_enable_constraint = {
    .minimum.u64 = 0,
    .maximum.u64 = 1,
    .step.u64 = 1,
};

static const esp_gmf_arg_constraint_t s_eq_filter_num_constraint = {
    .minimum.u64 = 1,
    .maximum.u64 = 16,
    .step.u64 = 1,
};

static esp_gmf_err_t eq_map_ae_error(esp_ae_err_t ret)
{
    switch (ret) {
        case ESP_AE_ERR_OK:
            return ESP_GMF_ERR_OK;
        case ESP_AE_ERR_INVALID_PARAMETER:
            return ESP_GMF_ERR_INVALID_ARG;
        case ESP_AE_ERR_MEM_LACK:
            return ESP_GMF_ERR_MEMORY_LACK;
        case ESP_AE_ERR_NOT_SUPPORT:
            return ESP_GMF_ERR_NOT_SUPPORT;
        default:
            return ESP_GMF_ERR_FAIL;
    }
}

const esp_ae_eq_filter_para_t esp_gmf_default_eq_paras[10] = {
    {ESP_AE_EQ_FILTER_PEAK, 31, 1.0, 0.0},
    {ESP_AE_EQ_FILTER_PEAK, 62, 1.0, 0.0},
    {ESP_AE_EQ_FILTER_PEAK, 125, 1.0, 0.0},
    {ESP_AE_EQ_FILTER_PEAK, 250, 1.0, 1.0},
    {ESP_AE_EQ_FILTER_PEAK, 500, 1.0, 2.0},
    {ESP_AE_EQ_FILTER_PEAK, 1000, 1.0, 3.0},
    {ESP_AE_EQ_FILTER_PEAK, 2000, 1.0, 3.0},
    {ESP_AE_EQ_FILTER_PEAK, 4000, 1.0, 2.0},
    {ESP_AE_EQ_FILTER_PEAK, 8000, 1.0, 1.0},
    {ESP_AE_EQ_FILTER_PEAK, 16000, 1.0, 0.0},
};

static inline esp_gmf_err_t dupl_esp_ae_eq_cfg(esp_ae_eq_cfg_t *config, esp_ae_eq_cfg_t **new_config)
{
    void *sub_cfg = NULL;
    *new_config = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, *new_config, {return ESP_GMF_ERR_MEMORY_LACK;}, "eq configuration", sizeof(*config));
    memcpy(*new_config, config, sizeof(*config));
    if (config->para && (config->filter_num > 0)) {
        sub_cfg = esp_gmf_oal_calloc(1, config->filter_num * sizeof(esp_ae_eq_filter_para_t));
        ESP_GMF_MEM_VERIFY(TAG, sub_cfg, {esp_gmf_oal_free(*new_config); return ESP_GMF_ERR_MEMORY_LACK;},
                           "filter parameter", config->filter_num * sizeof(esp_ae_eq_filter_para_t));
        memcpy(sub_cfg, config->para, config->filter_num * sizeof(esp_ae_eq_filter_para_t));
        (*new_config)->para = sub_cfg;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static inline void free_esp_ae_eq_cfg(esp_ae_eq_cfg_t *config)
{
    if (config) {
        if (config->para && (config->para != esp_gmf_default_eq_paras)) {
            esp_gmf_oal_free(config->para);
        }
        esp_gmf_oal_free(config);
    }
}

static inline void eq_change_src_info(esp_gmf_element_handle_t self, uint32_t src_rate, uint8_t src_ch, uint8_t src_bits)
{
    esp_ae_eq_cfg_t *eq_info = (esp_ae_eq_cfg_t *)OBJ_GET_CFG(self);
    eq_info->channel = src_ch;
    eq_info->sample_rate = src_rate;
    eq_info->bits_per_sample = src_bits;
}

static esp_gmf_err_t __eq_set_para(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                   uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t idx = (uint8_t)(*buf);
    return esp_gmf_eq_set_para(handle, idx, (esp_ae_eq_filter_para_t *)(buf + arg_desc->next->offset));
}

static esp_gmf_err_t __eq_get_para(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                   uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t idx = (uint8_t)(*buf);
    esp_ae_eq_filter_para_t *para = (esp_ae_eq_filter_para_t *)(buf + arg_desc->next->offset);
    return esp_gmf_eq_get_para(handle, idx, para);
}

static esp_gmf_err_t __eq_enable_filter(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                        uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_args_desc_t *filter_desc = arg_desc;
    uint8_t idx = (uint8_t)(*buf);
    filter_desc = filter_desc->next;
    uint8_t is_enable = (uint8_t)(*(buf + filter_desc->offset));
    return esp_gmf_eq_enable_filter(handle, idx,is_enable);
}

static esp_gmf_err_t __eq_get_enable_filter(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                            uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_args_desc_t *enable_desc = arg_desc->next;
    ESP_GMF_NULL_CHECK(TAG, enable_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t idx = (uint8_t)(*buf);
    bool is_enable = false;
    esp_gmf_err_t ret = esp_gmf_eq_get_filter_enabled(handle, idx, &is_enable);
    if (ret == ESP_GMF_ERR_OK) {
        *(buf + enable_desc->offset) = (uint8_t)(is_enable ? 1 : 0);
    }
    return ret;
}

static esp_gmf_err_t __eq_set_filter_num(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                         uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_eq_set_filter_num(handle, *((uint8_t *)buf));
}

static esp_gmf_err_t __eq_get_filter_num(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                         uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_eq_get_filter_num(handle, (uint8_t *)buf);
}

static esp_gmf_err_t esp_gmf_eq_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_eq_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_eq_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_eq_t *eq = (esp_gmf_eq_t *)self;
    esp_ae_eq_cfg_t *eq_info = (esp_ae_eq_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, eq_info, {return ESP_GMF_JOB_ERR_FAIL;});
    esp_gmf_job_err_t job_ret = ESP_GMF_JOB_ERR_OK;
    eq->bytes_per_sample = (eq_info->bits_per_sample >> 3) * eq_info->channel;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
    esp_ae_eq_open(eq_info, &eq->eq_hd);
    ESP_GMF_CHECK(TAG, eq->eq_hd, {job_ret = ESP_GMF_JOB_ERR_FAIL; goto __eq_open_exit;}, "Failed to create eq handle");
    for (int i = 0; i < eq_info->filter_num; i++) {
        if (eq->is_filter_enabled[i]) {
            esp_ae_eq_enable_filter(eq->eq_hd, i);
        } else {
            esp_ae_eq_disable_filter(eq->eq_hd, i);
        }
    }
    eq->need_reopen = false;
__eq_open_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
    if (job_ret != ESP_GMF_JOB_ERR_OK) {
        return job_ret;
    }
    GMF_AUDIO_UPDATE_SND_INFO(self, eq_info->sample_rate, eq_info->bits_per_sample, eq_info->channel);
    ESP_LOGD(TAG, "Open, %p", eq);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_eq_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_eq_t *eq = (esp_gmf_eq_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
    if (eq->eq_hd != NULL) {
        esp_ae_eq_close(eq->eq_hd);
        eq->eq_hd = NULL;
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_eq_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_eq_t *eq = (esp_gmf_eq_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (eq->need_reopen) {
        esp_gmf_eq_close(self, NULL);
        out_len = esp_gmf_eq_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "EQ reopen failed");
            return out_len;
        }
    }
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int samples_num = ESP_GMF_ELEMENT_GET(eq)->in_attr.data_size / (eq->bytes_per_sample);
    int bytes = samples_num * eq->bytes_per_sample;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    samples_num = in_load->valid_size / (eq->bytes_per_sample);
    bytes = samples_num * eq->bytes_per_sample;
    if ((bytes != in_load->valid_size) || (load_ret < ESP_GMF_IO_OK)) {
        if (load_ret == ESP_GMF_IO_ABORT) {
            out_len = ESP_GMF_JOB_ERR_ABORT;
            goto __eq_release;
        }
        ESP_LOGE(TAG, "Invalid in load size %d, ret %d", in_load->valid_size, load_ret);
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __eq_release;
    }
    if (in_port->is_shared == 1) {
        out_load = in_load;
    }
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, samples_num ? bytes : in_load->buf_length, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, { goto __eq_release;});
    if (samples_num > 0) {
        esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
        esp_ae_err_t ret = esp_ae_eq_process(eq->eq_hd, samples_num, in_load->buf, out_load->buf);
        esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __eq_release;}, "Equalize process error %d", ret);
    }
    ESP_LOGV(TAG, "Samples: %d, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d",
             samples_num, in_load, in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done,
             out_load, out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    out_load->valid_size = samples_num * eq->bytes_per_sample;
    out_load->is_done = in_load->is_done;
    out_load->pts = in_load->pts;
    if (out_load->valid_size > 0) {
        esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, out_load->valid_size);
    }
    if (in_load->is_done) {
        out_len = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "Equalize done, out len: %d", out_load->valid_size);
    }
__eq_release:
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

static esp_gmf_err_t eq_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_ae_eq_cfg_t *config = (esp_ae_eq_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_eq_t *eq = (esp_gmf_eq_t *)self;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
    eq->need_reopen = eq->need_reopen
                      || (config->sample_rate != info->sample_rates)
                      || (info->channels != config->channel)
                      || (config->bits_per_sample != info->bits);
    config->sample_rate = info->sample_rates;
    config->channel = info->channels;
    config->bits_per_sample = info->bits;
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
    ESP_LOGD(TAG, "RECV element info, from: %s-%p, next: %p, self: %s-%p, type: %x, state: %s, rate: %d, ch: %d, bits: %d",
             OBJ_GET_TAG(el), el, esp_gmf_node_for_next((esp_gmf_node_t *)el), OBJ_GET_TAG(self), self, evt->type,
             esp_gmf_event_get_state_str(state), info->sample_rates, info->channels, info->bits);
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_eq_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_eq_t *eq = (esp_gmf_eq_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    free_esp_ae_eq_cfg(OBJ_GET_CFG(self));
    if(eq->is_filter_enabled){
        esp_gmf_oal_free(eq->is_filter_enabled);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(eq);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_eq_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_EQUALIZER;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_eq_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_args_desc_t *get_args = NULL;
    esp_gmf_args_desc_t *pointer_args = NULL;
    esp_gmf_err_t ret = esp_gmf_args_desc_append_with_constraint(&pointer_args, AMETHOD_ARG(EQ, SET_PARA, PARA_FT),
                                                                 ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t),
                                                                 offsetof(esp_ae_eq_filter_para_t, filter_type),
                                                                 &s_eq_filter_type_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append FILTER argument");
    ret = esp_gmf_args_desc_append(&pointer_args, AMETHOD_ARG(EQ, SET_PARA, PARA_FC), ESP_GMF_ARGS_TYPE_UINT32,
                                   sizeof(uint32_t), offsetof(esp_ae_eq_filter_para_t, fc));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append FC argument");
    ret = esp_gmf_args_desc_append_with_constraint(&pointer_args, AMETHOD_ARG(EQ, SET_PARA, PARA_Q),
                                                   ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float),
                                                   offsetof(esp_ae_eq_filter_para_t, q), &s_eq_q_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append Q argument");
    ret = esp_gmf_args_desc_append_with_constraint(&pointer_args, AMETHOD_ARG(EQ, SET_PARA, PARA_GAIN),
                                                   ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float),
                                                   offsetof(esp_ae_eq_filter_para_t, gain), &s_eq_gain_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append GAIN argument");
    ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(EQ, SET_PARA, IDX),
                                                   ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0,
                                                   &s_eq_index_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append INDEX argument");
    ret = esp_gmf_args_desc_append_array(&set_args, AMETHOD_ARG(EQ, SET_PARA, PARA), pointer_args,
                                         sizeof(esp_ae_eq_filter_para_t), sizeof(uint8_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append PARA argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(EQ, SET_PARA), __eq_set_para,
                                          set_args, AMETHOD(EQ, GET_PARA), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(EQ, SET_PARA));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy PARA argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(EQ, GET_PARA), __eq_get_para,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(EQ, GET_PARA));

    set_args = NULL;
    get_args = NULL;
    ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(EQ, ENABLE_FILTER, IDX),
                                                   ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0,
                                                   &s_eq_index_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append INDEX argument");
    ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(EQ, ENABLE_FILTER, ENABLE),
                                                   ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), sizeof(uint8_t),
                                                   &s_eq_enable_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append PARA argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(EQ, ENABLE_FILTER), __eq_enable_filter,
                                          set_args, AMETHOD(EQ, GET_ENABLE_FILTER), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(EQ, ENABLE_FILTER));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy ENABLE argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(EQ, GET_ENABLE_FILTER), __eq_get_enable_filter,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(EQ, GET_ENABLE_FILTER));

    set_args = NULL;
    get_args = NULL;
    ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(EQ, SET_FILTER_NUM, FILTER_NUM),
                                                   ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0,
                                                   &s_eq_filter_num_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append FILTER_NUM argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(EQ, SET_FILTER_NUM), __eq_set_filter_num,
                                          set_args, AMETHOD(EQ, GET_FILTER_NUM), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(EQ, SET_FILTER_NUM));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy FILTER_NUM argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(EQ, GET_FILTER_NUM), __eq_get_filter_num,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(EQ, GET_FILTER_NUM));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_eq_set_para(esp_gmf_element_handle_t handle, uint8_t idx, esp_ae_eq_filter_para_t *para)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_eq_set_para: idx=%u, type=%u, fc=%u, q=%f, gain=%f",
             handle, idx,
             para ? (unsigned)para->filter_type : 0,
             para ? (unsigned)para->fc : 0,
             para ? para->q : 0.0f,
             para ? para->gain : 0.0f);
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, para, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_eq_t *eq = (esp_gmf_eq_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    /* Pending reopen means cfg (and filter_num) already changed; only update cfg
     * so new indices survive until the next process reopen. */
    if (eq->eq_hd && !eq->need_reopen) {
        esp_ae_err_t ae_ret = esp_ae_eq_set_filter_para(eq->eq_hd, idx, para);
        ESP_GMF_RET_ON_ERROR(TAG, ae_ret, {ret = eq_map_ae_error(ae_ret); goto __eq_set_para_exit;},
                             "Equalize set error %d", ae_ret);
    }
    esp_ae_eq_cfg_t *cfg = (esp_ae_eq_cfg_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL) {
        ret = ESP_GMF_ERR_FAIL;
        goto __eq_set_para_exit;
    }
    if (cfg->para) {
        if (idx >= cfg->filter_num) {
            ESP_LOGE(TAG, "Invalid idx %d", idx);
            ret = ESP_GMF_ERR_FAIL;
            goto __eq_set_para_exit;
        }
        memcpy(&cfg->para[idx], para, sizeof(esp_ae_eq_filter_para_t));
        goto __eq_set_para_exit;
    }
    ESP_LOGE(TAG, "Failed to set EQ para, no para allocated");
    ret = ESP_GMF_ERR_FAIL;
__eq_set_para_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_eq_get_para(esp_gmf_element_handle_t handle, uint8_t idx, esp_ae_eq_filter_para_t *para)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_eq_get_para: idx=%u", handle, idx);
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_eq_t *eq = (esp_gmf_eq_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (eq->eq_hd) {
        esp_ae_err_t ae_ret = esp_ae_eq_get_filter_para(eq->eq_hd, idx, para);
        ESP_GMF_RET_ON_ERROR(TAG, ae_ret, {ret = eq_map_ae_error(ae_ret); goto __eq_get_para_exit;},
                             "Equalize get error %d", ae_ret);
    } else {
        esp_ae_eq_cfg_t *cfg = (esp_ae_eq_cfg_t *)OBJ_GET_CFG(handle);
        if (cfg == NULL) {
            ret = ESP_GMF_ERR_FAIL;
            goto __eq_get_para_exit;
        }
        if (cfg->para) {
            if (idx >= cfg->filter_num) {
                ESP_LOGE(TAG, "Invalid idx %d", idx);
                ret = ESP_GMF_ERR_FAIL;
                goto __eq_get_para_exit;
            }
            memcpy(para, &cfg->para[idx], sizeof(esp_ae_eq_filter_para_t));
        }
    }
__eq_get_para_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_eq_enable_filter(esp_gmf_element_handle_t handle, uint8_t idx, bool is_enable)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_eq_enable_filter: idx=%u, enable=%d", handle, idx, is_enable);
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_eq_t *eq = (esp_gmf_eq_t *)handle;
    esp_ae_eq_cfg_t *cfg = (esp_ae_eq_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (idx >= cfg->filter_num) {
        ESP_LOGE(TAG, "Filter index %d overlimit %d hd:%p", idx, cfg->filter_num, eq);
        ret = ESP_GMF_ERR_INVALID_ARG;
        goto __eq_enable_filter_exit;
    }
    if (eq->eq_hd && !eq->need_reopen) {
        esp_ae_err_t ae_ret = 0;
        if (is_enable) {
            ae_ret = esp_ae_eq_enable_filter(eq->eq_hd, idx);
        } else {
            ae_ret = esp_ae_eq_disable_filter(eq->eq_hd, idx);
        }
        ESP_GMF_RET_ON_ERROR(TAG, ae_ret, {ret = eq_map_ae_error(ae_ret); goto __eq_enable_filter_exit;},
                             "Equalize enable error %d", ae_ret);
    }
    eq->is_filter_enabled[idx] = is_enable;
__eq_enable_filter_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_eq_get_filter_enabled(esp_gmf_element_handle_t handle, uint8_t idx, bool *is_enable)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, is_enable, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_eq_t *eq = (esp_gmf_eq_t *)handle;
    esp_ae_eq_cfg_t *cfg = (esp_ae_eq_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (idx >= cfg->filter_num || eq->is_filter_enabled == NULL) {
        ESP_LOGE(TAG, "Filter index %d overlimit %d hd:%p", idx, cfg->filter_num, eq);
        ret = ESP_GMF_ERR_INVALID_ARG;
    } else {
        *is_enable = eq->is_filter_enabled[idx];
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_eq_set_filter_num(esp_gmf_element_handle_t handle, uint8_t filter_num)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_eq_set_filter_num: filter_num=%u", handle, filter_num);
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    if (filter_num == 0 || filter_num > 16) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_eq_t *eq = (esp_gmf_eq_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    esp_ae_eq_cfg_t *cfg = (esp_ae_eq_cfg_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL) {
        ret = ESP_GMF_ERR_FAIL;
        goto __eq_set_filter_num_exit;
    }
    if (cfg->filter_num == filter_num) {
        goto __eq_set_filter_num_exit;
    }

    esp_ae_eq_filter_para_t *new_para =
        esp_gmf_oal_calloc(filter_num, sizeof(esp_ae_eq_filter_para_t));
    ESP_GMF_MEM_VERIFY(TAG, new_para, {ret = ESP_GMF_ERR_MEMORY_LACK; goto __eq_set_filter_num_exit;},
                       "eq filter para", filter_num * sizeof(esp_ae_eq_filter_para_t));
    bool *new_enabled = esp_gmf_oal_calloc(filter_num, sizeof(bool));
    ESP_GMF_MEM_VERIFY(TAG, new_enabled, {
        esp_gmf_oal_free(new_para);
        ret = ESP_GMF_ERR_MEMORY_LACK;
        goto __eq_set_filter_num_exit;
    }, "eq filter enable", filter_num * sizeof(bool));

    uint8_t copy_num = cfg->filter_num < filter_num ? cfg->filter_num : filter_num;
    if (cfg->para && copy_num > 0) {
        memcpy(new_para, cfg->para, copy_num * sizeof(esp_ae_eq_filter_para_t));
    }
    if (eq->is_filter_enabled && copy_num > 0) {
        memcpy(new_enabled, eq->is_filter_enabled, copy_num * sizeof(bool));
    }
    for (uint8_t i = copy_num; i < filter_num; i++) {
        new_para[i] = (esp_ae_eq_filter_para_t){
            .filter_type = ESP_AE_EQ_FILTER_PEAK,
            .fc = 1000,
            .q = 1.0f,
            .gain = 0.0f,
        };
        new_enabled[i] = true;
    }

    if (cfg->para && cfg->para != esp_gmf_default_eq_paras) {
        esp_gmf_oal_free(cfg->para);
    }
    if (eq->is_filter_enabled) {
        esp_gmf_oal_free(eq->is_filter_enabled);
    }
    cfg->para = new_para;
    cfg->filter_num = filter_num;
    eq->is_filter_enabled = new_enabled;
    /* Filter bank size is fixed at open; resize while running needs reopen. */
    if (eq->eq_hd != NULL) {
        eq->need_reopen = true;
    }
__eq_set_filter_num_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_eq_get_filter_num(esp_gmf_element_handle_t handle, uint8_t *filter_num)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, filter_num, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    esp_ae_eq_cfg_t *cfg = (esp_ae_eq_cfg_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL) {
        ret = ESP_GMF_ERR_FAIL;
    } else {
        *filter_num = cfg->filter_num;
        ESP_LOGI(TAG, "handle:%p esp_gmf_eq_get_filter_num: filter_num=%u", handle, *filter_num);
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

static esp_gmf_job_err_t esp_gmf_eq_reset(esp_gmf_element_handle_t handle, void *para)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_eq_t *eq = (esp_gmf_eq_t *)handle;
    esp_gmf_job_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (eq->eq_hd) {
        esp_ae_err_t ae_ret = esp_ae_eq_reset(eq->eq_hd);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_ERR_FAIL;
            goto __eq_reset_exit;
        }
    }
__eq_reset_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    ESP_LOGD(TAG, "EQ reset");
    return ret;
}

esp_gmf_err_t esp_gmf_eq_init(esp_ae_eq_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_eq_t *eq = esp_gmf_oal_calloc(1, sizeof(esp_gmf_eq_t));
    ESP_GMF_MEM_VERIFY(TAG, eq, {return ESP_GMF_ERR_MEMORY_LACK;}, "eq", sizeof(esp_gmf_eq_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)eq;
    obj->new_obj = esp_gmf_eq_new;
    obj->del_obj = esp_gmf_eq_destroy;
    esp_ae_eq_cfg_t *cfg = NULL;
    if (config) {
        if (config->para == NULL) {
            config->para = (esp_ae_eq_filter_para_t *)esp_gmf_default_eq_paras;
            config->filter_num = sizeof(esp_gmf_default_eq_paras) / sizeof(esp_ae_eq_filter_para_t);
        }
        dupl_esp_ae_eq_cfg(config, &cfg);
    } else {
        esp_ae_eq_cfg_t dcfg = DEFAULT_ESP_GMF_EQ_CONFIG();
        dcfg.para = (esp_ae_eq_filter_para_t *)esp_gmf_default_eq_paras;
        dcfg.filter_num = sizeof(esp_gmf_default_eq_paras) / sizeof(esp_ae_eq_filter_para_t);
        dupl_esp_ae_eq_cfg(&dcfg, &cfg);
    }
    ESP_GMF_CHECK(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto EQ_INI_FAIL;}, "Failed to allocate eq configuration");
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_eq_cfg_t));
    eq->is_filter_enabled = esp_gmf_oal_calloc(cfg->filter_num, sizeof(bool));
    ESP_GMF_MEM_VERIFY(TAG, eq->is_filter_enabled, ret = ESP_GMF_ERR_MEMORY_LACK; goto EQ_INI_FAIL, "Rellocation failed", cfg->filter_num);
    ret = esp_gmf_obj_set_tag(obj, "aud_eq");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto EQ_INI_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(eq, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto EQ_INI_FAIL, "Failed to initialize eq element");
    ESP_GMF_ELEMENT_GET(eq)->ops.open = esp_gmf_eq_open;
    ESP_GMF_ELEMENT_GET(eq)->ops.process = esp_gmf_eq_process;
    ESP_GMF_ELEMENT_GET(eq)->ops.close = esp_gmf_eq_close;
    ESP_GMF_ELEMENT_GET(eq)->ops.event_receiver = eq_received_event_handler;
    ESP_GMF_ELEMENT_GET(eq)->ops.load_caps = _load_eq_caps_func;
    ESP_GMF_ELEMENT_GET(eq)->ops.load_methods = _load_eq_methods_func;
    ESP_GMF_ELEMENT_GET(eq)->ops.reset = esp_gmf_eq_reset;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
EQ_INI_FAIL:
    esp_gmf_eq_destroy(obj);
    return ret;
}
