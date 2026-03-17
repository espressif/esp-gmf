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
#include "esp_gmf_mbc.h"
#include "esp_gmf_args_desc.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"
#include "esp_ae_mbc.h"

typedef struct {
    esp_gmf_audio_element_t parent;
    esp_ae_mbc_handle_t     mbc_hd;
    uint8_t                 bytes_per_sample;
    bool                    need_reopen : 1;
    bool                    solo_state[ESP_AE_MBC_BAND_IDX_MAX];
    bool                    bypass_state[ESP_AE_MBC_BAND_IDX_MAX];
} esp_gmf_mbc_t;

static const char *TAG = "ESP_GMF_MBC";

static inline esp_gmf_err_t dupl_esp_ae_mbc_cfg(esp_ae_mbc_config_t *config, esp_ae_mbc_config_t **new_config)
{
    ESP_GMF_NULL_CHECK(TAG, new_config, {return ESP_GMF_ERR_INVALID_ARG;});
    *new_config = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, *new_config, {return ESP_GMF_ERR_MEMORY_LACK;}, "mbc configuration", sizeof(*config));
    memcpy(*new_config, config, sizeof(*config));
    return ESP_GMF_ERR_OK;
}

static inline void free_esp_ae_mbc_cfg(esp_ae_mbc_config_t *config)
{
    if (config) {
        esp_gmf_oal_free(config);
    }
}

static esp_gmf_err_t __mbc_set_para(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t idx = *(uint8_t *)(buf);
    esp_ae_mbc_para_t *para = (esp_ae_mbc_para_t *)(buf + arg_desc->next->offset);
    return esp_gmf_mbc_set_para(handle, idx, para);
}

static esp_gmf_err_t __mbc_get_para(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t idx = *(uint8_t *)(buf);
    esp_ae_mbc_para_t *para = (esp_ae_mbc_para_t *)(buf + arg_desc->next->offset);
    return esp_gmf_mbc_get_para(handle, idx, para);
}

static esp_gmf_err_t __mbc_set_fc(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                  uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t idx = *(uint8_t *)(buf);
    uint32_t fc = *(uint32_t *)(buf + arg_desc->next->offset);
    return esp_gmf_mbc_set_fc(handle, idx, fc);
}

static esp_gmf_err_t __mbc_get_fc(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                  uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t idx = *(uint8_t *)(buf);
    uint32_t *fc = (uint32_t *)(buf + arg_desc->next->offset);
    return esp_gmf_mbc_get_fc(handle, idx, fc);
}

static esp_gmf_err_t __mbc_set_solo(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t idx = *(uint8_t *)(buf);
    bool en = *(bool *)(buf + arg_desc->next->offset);
    return esp_gmf_mbc_set_solo(handle, idx, en);
}

static esp_gmf_err_t __mbc_get_solo(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t idx = *(uint8_t *)(buf);
    bool *en = (bool *)(buf + arg_desc->next->offset);
    return esp_gmf_mbc_get_solo(handle, idx, en);
}

static esp_gmf_err_t __mbc_set_bypass(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t idx = *(uint8_t *)(buf);
    bool en = *(bool *)(buf + arg_desc->next->offset);
    return esp_gmf_mbc_set_bypass(handle, idx, en);
}

static esp_gmf_err_t __mbc_get_bypass(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t idx = *(uint8_t *)(buf);
    bool *en = (bool *)(buf + arg_desc->next->offset);
    return esp_gmf_mbc_get_bypass(handle, idx, en);
}

static esp_gmf_err_t gmf_mbc_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_mbc_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t gmf_mbc_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_mbc_t *el = (esp_gmf_mbc_t *)self;
    esp_ae_mbc_config_t *config = (esp_ae_mbc_config_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, {return ESP_GMF_JOB_ERR_FAIL;});
    el->bytes_per_sample = (config->bits_per_sample >> 3) * config->channel;
    esp_ae_err_t ret = esp_ae_mbc_open(config, &el->mbc_hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_JOB_ERR_FAIL;}, "Failed to create mbc handle %d", ret);
    GMF_AUDIO_UPDATE_SND_INFO(self, config->sample_rate, config->bits_per_sample, config->channel);
    for (int i = 0; i < ESP_AE_MBC_BAND_IDX_MAX; i++) {
        ret = esp_ae_mbc_set_solo(el->mbc_hd, i, el->solo_state[i]);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_ae_mbc_close(el->mbc_hd); el->mbc_hd = NULL; return ESP_GMF_JOB_ERR_FAIL;},
                             "Failed to restore solo state %d", ret);
        ret = esp_ae_mbc_set_bypass(el->mbc_hd, i, el->bypass_state[i]);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_ae_mbc_close(el->mbc_hd); el->mbc_hd = NULL; return ESP_GMF_JOB_ERR_FAIL;},
                             "Failed to restore bypass state %d", ret);
    }
    el->need_reopen = false;
    ESP_LOGD(TAG, "Open, %p", self);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t gmf_mbc_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_mbc_t *el = (esp_gmf_mbc_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    if (el->mbc_hd) {
        esp_ae_mbc_close(el->mbc_hd);
        el->mbc_hd = NULL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t gmf_mbc_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_mbc_t *el = (esp_gmf_mbc_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (el->need_reopen) {
        gmf_mbc_close(self, NULL);
        out_len = gmf_mbc_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "MBC reopen failed");
            return out_len;
        }
    }
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int samples_num = ESP_GMF_ELEMENT_GET(self)->in_attr.data_size / (el->bytes_per_sample);
    int bytes = samples_num * el->bytes_per_sample;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    samples_num = in_load->valid_size / (el->bytes_per_sample);
    bytes = samples_num * el->bytes_per_sample;
    if ((bytes != in_load->valid_size) || (load_ret < ESP_GMF_IO_OK)) {
        if (load_ret == ESP_GMF_IO_ABORT) {
            out_len = ESP_GMF_JOB_ERR_ABORT;
            goto __release;
        }
        ESP_LOGE(TAG, "Invalid in load size %d, ret %d", in_load->valid_size, load_ret);
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __release;
    }
    if (in_port->is_shared == 1) {
        out_load = in_load;
    }
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, samples_num ? bytes : in_load->buf_length, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, goto __release);
    if (samples_num > 0) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)self)->lock);
        esp_ae_err_t ret = esp_ae_mbc_process(el->mbc_hd, samples_num, in_load->buf, out_load->buf);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)self)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __release;}, "MBC process error %d", ret);
    }
    ESP_LOGV(TAG, "Samples: %d, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d",
             samples_num, in_load, in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done,
             out_load, out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    out_load->valid_size = samples_num * el->bytes_per_sample;
    out_load->is_done = in_load->is_done;
    out_load->pts = in_load->pts;
    if (out_load->valid_size > 0) {
        esp_gmf_audio_el_update_file_pos(self, out_load->valid_size);
    }
    if (in_load->is_done) {
        out_len = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "MBC done, out len: %d", out_load->valid_size);
    }
__release:
    if (out_load) {
        load_ret = esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "OUT port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    if (in_load) {
        load_ret = esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "IN port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    return out_len;
}

static esp_gmf_err_t mbc_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_ae_mbc_config_t *config = (esp_ae_mbc_config_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_mbc_t *mbc = (esp_gmf_mbc_t *)self;
    mbc->need_reopen = (config->sample_rate != info->sample_rates) || (info->channels != config->channel) || (config->bits_per_sample != info->bits);
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

static esp_gmf_err_t _load_mbc_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t cap = {0};
    cap.cap_eightcc = ESP_GMF_CAPS_AUDIO_MBC;
    cap.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &cap);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_mbc_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_args_desc_t *get_args = NULL;
    esp_gmf_args_desc_t *para_args = NULL;
    int ret;

    ret = esp_gmf_args_desc_append(&para_args, AMETHOD_ARG(MBC, SET_PARA, PARA_THRESHOLD), ESP_GMF_ARGS_TYPE_FLOAT,
                                   sizeof(float), offsetof(esp_ae_mbc_para_t, threshold));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append THRESHOLD");
    ret = esp_gmf_args_desc_append(&para_args, AMETHOD_ARG(MBC, SET_PARA, PARA_RATIO), ESP_GMF_ARGS_TYPE_FLOAT,
                                   sizeof(float), offsetof(esp_ae_mbc_para_t, ratio));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append RATIO");
    ret = esp_gmf_args_desc_append(&para_args, AMETHOD_ARG(MBC, SET_PARA, PARA_MAKEUP), ESP_GMF_ARGS_TYPE_FLOAT,
                                   sizeof(float), offsetof(esp_ae_mbc_para_t, makeup_gain));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append MAKEUP");
    ret = esp_gmf_args_desc_append(&para_args, AMETHOD_ARG(MBC, SET_PARA, PARA_ATTACK), ESP_GMF_ARGS_TYPE_UINT16,
                                   sizeof(uint16_t), offsetof(esp_ae_mbc_para_t, attack_time));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append ATTACK");
    ret = esp_gmf_args_desc_append(&para_args, AMETHOD_ARG(MBC, SET_PARA, PARA_RELEASE), ESP_GMF_ARGS_TYPE_UINT16,
                                   sizeof(uint16_t), offsetof(esp_ae_mbc_para_t, release_time));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append RELEASE");
    ret = esp_gmf_args_desc_append(&para_args, AMETHOD_ARG(MBC, SET_PARA, PARA_HOLD), ESP_GMF_ARGS_TYPE_UINT16,
                                   sizeof(uint16_t), offsetof(esp_ae_mbc_para_t, hold_time));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append HOLD");
    ret = esp_gmf_args_desc_append(&para_args, AMETHOD_ARG(MBC, SET_PARA, PARA_KNEE), ESP_GMF_ARGS_TYPE_FLOAT,
                                   sizeof(float), offsetof(esp_ae_mbc_para_t, knee_width));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append KNEE");

    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MBC, SET_PARA, IDX), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append IDX");
    ret = esp_gmf_args_desc_append_array(&set_args, AMETHOD_ARG(MBC, SET_PARA, PARA), para_args,
                                         sizeof(esp_ae_mbc_para_t), sizeof(uint8_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append PARA");
    ret = esp_gmf_method_append(&method, AMETHOD(MBC, SET_PARA), __mbc_set_para, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(MBC, SET_PARA));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy PARA args");
    ret = esp_gmf_method_append(&method, AMETHOD(MBC, GET_PARA), __mbc_get_para, get_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(MBC, GET_PARA));

    set_args = NULL;
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MBC, SET_FC, IDX), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append IDX");
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MBC, SET_FC, FC), ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), sizeof(uint8_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append FC");
    ret = esp_gmf_method_append(&method, AMETHOD(MBC, SET_FC), __mbc_set_fc, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(MBC, SET_FC));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy FC args");
    ret = esp_gmf_method_append(&method, AMETHOD(MBC, GET_FC), __mbc_get_fc, get_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(MBC, GET_FC));

    set_args = NULL;
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MBC, SET_SOLO, IDX), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append IDX");
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MBC, SET_SOLO, ENABLE), ESP_GMF_ARGS_TYPE_UINT8,
                                   sizeof(uint8_t), sizeof(uint8_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append ENABLE");
    ret = esp_gmf_method_append(&method, AMETHOD(MBC, SET_SOLO), __mbc_set_solo, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(MBC, SET_SOLO));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy SOLO args");
    ret = esp_gmf_method_append(&method, AMETHOD(MBC, GET_SOLO), __mbc_get_solo, get_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(MBC, GET_SOLO));

    set_args = NULL;
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MBC, SET_BYPASS, IDX), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append IDX");
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MBC, SET_BYPASS, ENABLE), ESP_GMF_ARGS_TYPE_UINT8,
                                   sizeof(uint8_t), sizeof(uint8_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append ENABLE");
    ret = esp_gmf_method_append(&method, AMETHOD(MBC, SET_BYPASS), __mbc_set_bypass, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(MBC, SET_BYPASS));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy BYPASS args");
    ret = esp_gmf_method_append(&method, AMETHOD(MBC, GET_BYPASS), __mbc_get_bypass, get_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(MBC, GET_BYPASS));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_mbc_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_mbc_t *mbc = (esp_gmf_mbc_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    free_esp_ae_mbc_cfg((esp_ae_mbc_config_t *)OBJ_GET_CFG(self));
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(mbc);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mbc_set_para(esp_gmf_element_handle_t handle, uint8_t idx, esp_ae_mbc_para_t *para)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, para, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_mbc_t *mbc = (esp_gmf_mbc_t *)handle;
    esp_ae_mbc_config_t *cfg = (esp_ae_mbc_config_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    ESP_GMF_CHECK(TAG, idx < ESP_AE_MBC_BAND_IDX_MAX, {return ESP_GMF_ERR_INVALID_ARG;}, "Invalid MBC band index");
    if (mbc->mbc_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_mbc_set_para(mbc->mbc_hd, idx, para);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "MBC set para error %d", ret);
    }
    cfg->mbc_para[idx] = *para;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mbc_get_para(esp_gmf_element_handle_t handle, uint8_t idx, esp_ae_mbc_para_t *para)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, para, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_mbc_t *mbc = (esp_gmf_mbc_t *)handle;
    esp_ae_mbc_config_t *cfg = (esp_ae_mbc_config_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    ESP_GMF_CHECK(TAG, idx < ESP_AE_MBC_BAND_IDX_MAX, {return ESP_GMF_ERR_INVALID_ARG;}, "Invalid MBC band index");
    if (mbc->mbc_hd) {
        esp_ae_err_t ret = esp_ae_mbc_get_para(mbc->mbc_hd, idx, para);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "MBC get para error %d", ret);
        cfg->mbc_para[idx] = *para;
    } else {
        *para = cfg->mbc_para[idx];
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mbc_set_fc(esp_gmf_element_handle_t handle, uint8_t fc_idx, uint32_t fc)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_mbc_t *mbc = (esp_gmf_mbc_t *)handle;
    esp_ae_mbc_config_t *cfg = (esp_ae_mbc_config_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    ESP_GMF_CHECK(TAG, fc_idx < ESP_AE_MBC_FC_IDX_MAX, {return ESP_GMF_ERR_INVALID_ARG;}, "Invalid MBC fc index");
    if (mbc->mbc_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_mbc_set_fc(mbc->mbc_hd, fc_idx, fc);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "MBC set fc error %d", ret);
    }
    cfg->fc[fc_idx] = fc;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mbc_get_fc(esp_gmf_element_handle_t handle, uint8_t fc_idx, uint32_t *fc)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, fc, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_mbc_t *mbc = (esp_gmf_mbc_t *)handle;
    esp_ae_mbc_config_t *cfg = (esp_ae_mbc_config_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    ESP_GMF_CHECK(TAG, fc_idx < ESP_AE_MBC_FC_IDX_MAX, {return ESP_GMF_ERR_INVALID_ARG;}, "Invalid MBC fc index");
    if (mbc->mbc_hd) {
        esp_ae_err_t ret = esp_ae_mbc_get_fc(mbc->mbc_hd, fc_idx, fc);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "MBC get fc error %d", ret);
        cfg->fc[fc_idx] = *fc;
    } else {
        *fc = cfg->fc[fc_idx];
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mbc_set_solo(esp_gmf_element_handle_t handle, uint8_t idx, bool enable_solo)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_mbc_t *mbc = (esp_gmf_mbc_t *)handle;
    ESP_GMF_CHECK(TAG, idx < ESP_AE_MBC_BAND_IDX_MAX, {return ESP_GMF_ERR_INVALID_ARG;}, "Invalid MBC band index");
    if (mbc->mbc_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_mbc_set_solo(mbc->mbc_hd, idx, enable_solo);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "MBC set solo error %d", ret);
    }
    mbc->solo_state[idx] = enable_solo;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mbc_get_solo(esp_gmf_element_handle_t handle, uint8_t idx, bool *enable_solo)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, enable_solo, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_mbc_t *mbc = (esp_gmf_mbc_t *)handle;
    ESP_GMF_CHECK(TAG, idx < ESP_AE_MBC_BAND_IDX_MAX, {return ESP_GMF_ERR_INVALID_ARG;}, "Invalid MBC band index");
    if (mbc->mbc_hd) {
        esp_ae_err_t ret = esp_ae_mbc_get_solo(mbc->mbc_hd, idx, enable_solo);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "MBC get solo error %d", ret);
        mbc->solo_state[idx] = *enable_solo;
    } else {
        *enable_solo = mbc->solo_state[idx];
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mbc_set_bypass(esp_gmf_element_handle_t handle, uint8_t idx, bool enable_bypass)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_mbc_t *mbc = (esp_gmf_mbc_t *)handle;
    ESP_GMF_CHECK(TAG, idx < ESP_AE_MBC_BAND_IDX_MAX, {return ESP_GMF_ERR_INVALID_ARG;}, "Invalid MBC band index");
    if (mbc->mbc_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_mbc_set_bypass(mbc->mbc_hd, idx, enable_bypass);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "MBC set bypass error %d", ret);
    }
    mbc->bypass_state[idx] = enable_bypass;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mbc_get_bypass(esp_gmf_element_handle_t handle, uint8_t idx, bool *enable_bypass)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, enable_bypass, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_mbc_t *mbc = (esp_gmf_mbc_t *)handle;
    ESP_GMF_CHECK(TAG, idx < ESP_AE_MBC_BAND_IDX_MAX, {return ESP_GMF_ERR_INVALID_ARG;}, "Invalid MBC band index");
    if (mbc->mbc_hd) {
        esp_ae_err_t ret = esp_ae_mbc_get_bypass(mbc->mbc_hd, idx, enable_bypass);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "MBC get bypass error %d", ret);
        mbc->bypass_state[idx] = *enable_bypass;
    } else {
        *enable_bypass = mbc->bypass_state[idx];
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_mbc_reset(esp_gmf_element_handle_t handle, void *para)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_mbc_t *mbc = (esp_gmf_mbc_t *)handle;
    if (mbc->mbc_hd) {
        esp_ae_err_t ret = esp_ae_mbc_reset(mbc->mbc_hd);
        if (ret != ESP_AE_ERR_OK) {
            return ESP_GMF_ERR_FAIL;
        }
    }
    ESP_LOGD(TAG, "MBC reset");
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mbc_init(esp_ae_mbc_config_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_mbc_t *mbc = esp_gmf_oal_calloc(1, sizeof(esp_gmf_mbc_t));
    ESP_GMF_MEM_VERIFY(TAG, mbc, {return ESP_GMF_ERR_MEMORY_LACK;}, "mbc", sizeof(esp_gmf_mbc_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)mbc;
    obj->new_obj = gmf_mbc_new;
    obj->del_obj = esp_gmf_mbc_destroy;
    esp_ae_mbc_config_t *cfg = NULL;
    if (config) {
        ret = dupl_esp_ae_mbc_cfg(config, &cfg);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto MBC_INIT_FAIL, "Failed to duplicate mbc configuration");
    } else {
        esp_ae_mbc_config_t dcfg = DEFAULT_ESP_GMF_MBC_CONFIG();
        ret = dupl_esp_ae_mbc_cfg(&dcfg, &cfg);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto MBC_INIT_FAIL, "Failed to duplicate default mbc configuration");
    }
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_mbc_config_t));
    for (int i = 0; i < ESP_AE_MBC_BAND_IDX_MAX; i++) {
        mbc->solo_state[i] = false;
        mbc->bypass_state[i] = false;
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_mbc");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto MBC_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(mbc, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto MBC_INIT_FAIL, "Failed to initialize mbc element");
    ESP_GMF_ELEMENT_GET(mbc)->ops.open = gmf_mbc_open;
    ESP_GMF_ELEMENT_GET(mbc)->ops.process = gmf_mbc_process;
    ESP_GMF_ELEMENT_GET(mbc)->ops.close = gmf_mbc_close;
    ESP_GMF_ELEMENT_GET(mbc)->ops.event_receiver = mbc_received_event_handler;
    ESP_GMF_ELEMENT_GET(mbc)->ops.load_caps = _load_mbc_caps_func;
    ESP_GMF_ELEMENT_GET(mbc)->ops.load_methods = _load_mbc_methods_func;
    ESP_GMF_ELEMENT_GET(mbc)->ops.reset = esp_gmf_mbc_reset;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
MBC_INIT_FAIL:
    esp_gmf_mbc_destroy(obj);
    return ret;
}
