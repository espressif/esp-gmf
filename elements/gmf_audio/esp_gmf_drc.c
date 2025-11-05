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
#include "esp_gmf_drc.h"
#include "esp_gmf_args_desc.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"
#include "esp_ae_drc.h"

typedef struct {
    esp_gmf_audio_element_t parent;
    esp_ae_drc_handle_t     drc_hd;
    uint8_t                 bytes_per_sample;
    bool                    need_reopen : 1;
} esp_gmf_drc_t;

static const char *TAG = "ESP_GMF_DRC";

static const esp_ae_drc_curve_point esp_gmf_default_drc_points[] = {
    {.x = 0.0f, .y = -20.0f},
    {.x = -40.0f, .y = -40.0f},
    {.x = -100.0f, .y = -100.0f},
};

static inline esp_gmf_err_t dupl_esp_ae_drc_cfg(esp_ae_drc_cfg_t *config, esp_ae_drc_cfg_t **new_config)
{
    esp_ae_drc_curve_point *points = NULL;
    *new_config = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, *new_config, {return ESP_GMF_ERR_MEMORY_LACK;}, "drc configuration", sizeof(*config));
    memcpy(*new_config, config, sizeof(*config));
    if (config->drc_para.point && (config->drc_para.point_num > 0)) {
        size_t points_size = config->drc_para.point_num * sizeof(esp_ae_drc_curve_point);
        points = esp_gmf_oal_calloc(1, points_size);
        ESP_GMF_MEM_VERIFY(TAG, points, {esp_gmf_oal_free(*new_config); return ESP_GMF_ERR_MEMORY_LACK;},
                           "drc points", (int)points_size);
        memcpy(points, config->drc_para.point, points_size);
        (*new_config)->drc_para.point = points;
    } else {
        (*new_config)->drc_para.point = NULL;
        (*new_config)->drc_para.point_num = 0;
    }
    return ESP_GMF_ERR_OK;
}

static inline void free_esp_ae_drc_cfg(esp_ae_drc_cfg_t *config)
{
    if (config) {
        if (config->drc_para.point && (config->drc_para.point != esp_gmf_default_drc_points)) {
            esp_gmf_oal_free(config->drc_para.point);
        }
        esp_gmf_oal_free(config);
    }
}

static esp_gmf_err_t __drc_set_attack(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint16_t attack = *(uint16_t *)buf;
    return esp_gmf_drc_set_attack_time(handle, attack);
}

static esp_gmf_err_t __drc_get_attack(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_drc_get_attack_time(handle, (uint16_t *)buf);
}

static esp_gmf_err_t __drc_set_release(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                       uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint16_t release = *(uint16_t *)buf;
    return esp_gmf_drc_set_release_time(handle, release);
}

static esp_gmf_err_t __drc_get_release(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                       uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_drc_get_release_time(handle, (uint16_t *)buf);
}

static esp_gmf_err_t __drc_set_hold(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint16_t hold = *(uint16_t *)buf;
    return esp_gmf_drc_set_hold_time(handle, hold);
}

static esp_gmf_err_t __drc_get_hold(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_drc_get_hold_time(handle, (uint16_t *)buf);
}

static esp_gmf_err_t __drc_set_makeup(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    float mk = *(float *)buf;
    return esp_gmf_drc_set_makeup_gain(handle, mk);
}

static esp_gmf_err_t __drc_get_makeup(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_drc_get_makeup_gain(handle, (float *)buf);
}

static esp_gmf_err_t __drc_set_knee(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    float knee = *(float *)buf;
    return esp_gmf_drc_set_knee_width(handle, knee);
}

static esp_gmf_err_t __drc_get_knee(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_drc_get_knee_width(handle, (float *)buf);
}

static esp_gmf_err_t __drc_set_points(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_drc_curve_point *points = (esp_ae_drc_curve_point *)(*((int32_t *)buf));
    uint8_t num = *(uint8_t *)(buf + arg_desc->next->offset);
    return esp_gmf_drc_set_points(handle, points, num);
}

static esp_gmf_err_t __drc_get_point_num(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                         uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_drc_get_point_num(handle, (uint8_t *)buf);
}

static esp_gmf_err_t __drc_get_points(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_drc_curve_point *points = (esp_ae_drc_curve_point *)(*((int32_t *)buf));
    return esp_gmf_drc_get_points(handle, points);
}

static esp_gmf_err_t gmf_drc_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_drc_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t gmf_drc_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_drc_t *el = (esp_gmf_drc_t *)self;
    esp_ae_drc_cfg_t *config = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, {return ESP_GMF_JOB_ERR_FAIL;});
    el->bytes_per_sample = (config->bits_per_sample >> 3) * config->channel;
    esp_ae_err_t ret = esp_ae_drc_open(config, &el->drc_hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_JOB_ERR_FAIL;}, "Failed to create drc handle %d", ret);
    GMF_AUDIO_UPDATE_SND_INFO(self, config->sample_rate, config->bits_per_sample, config->channel);
    el->need_reopen = false;
    ESP_LOGD(TAG, "Open, %p", self);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t gmf_drc_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_drc_t *el = (esp_gmf_drc_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    if (el->drc_hd) {
        esp_ae_drc_close(el->drc_hd);
        el->drc_hd = NULL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t gmf_drc_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_drc_t *el = (esp_gmf_drc_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (el->need_reopen) {
        gmf_drc_close(self, NULL);
        out_len = gmf_drc_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "DRC reopen failed");
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
        esp_ae_err_t ret = esp_ae_drc_process(el->drc_hd, samples_num, in_load->buf, out_load->buf);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)self)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __release;}, "DRC process error %d", ret);
    }
    ESP_LOGV(TAG, "Samples: %d, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d",
             samples_num, in_load, in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done,
             out_load, out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    out_load->valid_size = samples_num * el->bytes_per_sample;
    out_load->is_done = in_load->is_done;
    out_load->pts = in_load->pts;
    esp_gmf_audio_el_update_file_pos(self, out_load->valid_size);
    if (in_load->is_done) {
        out_len = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "Drc done, out len: %d", out_load->valid_size);
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

static esp_gmf_err_t drc_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_ae_drc_cfg_t *config = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)self;
    drc->need_reopen = (config->sample_rate != info->sample_rates) || (info->channels != config->channel) || (config->bits_per_sample != info->bits);
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

static esp_gmf_err_t _load_drc_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t cap = {0};
    cap.cap_eightcc = ESP_GMF_CAPS_AUDIO_DRC;
    cap.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &cap);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_drc_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *args = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;

    // set/get attack
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, SET_ATTACK, ATTACK), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append ATTACK arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, SET_ATTACK), __drc_set_attack, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, SET_ATTACK));

    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, GET_ATTACK, ATTACK), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append ATTACK arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, GET_ATTACK), __drc_get_attack, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, GET_ATTACK));

    // set/get release
    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, SET_RELEASE, RELEASE), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append RELEASE arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, SET_RELEASE), __drc_set_release, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, SET_RELEASE));

    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, GET_RELEASE, RELEASE), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append RELEASE arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, GET_RELEASE), __drc_get_release, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, GET_RELEASE));

    // set/get hold
    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, SET_HOLD, HOLD), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append HOLD arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, SET_HOLD), __drc_set_hold, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, SET_HOLD));

    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, GET_HOLD, HOLD), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append HOLD arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, GET_HOLD), __drc_get_hold, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, GET_HOLD));

    // set/get makeup
    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, SET_MAKEUP, MAKEUP), ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append MAKEUP arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, SET_MAKEUP), __drc_set_makeup, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, SET_MAKEUP));

    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, GET_MAKEUP, MAKEUP), ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append MAKEUP arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, GET_MAKEUP), __drc_get_makeup, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, GET_MAKEUP));

    // set/get knee
    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, SET_KNEE, KNEE), ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append KNEE arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, SET_KNEE), __drc_set_knee, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, SET_KNEE));

    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, GET_KNEE, KNEE), ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append KNEE arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, GET_KNEE), __drc_get_knee, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, GET_KNEE));

    // set/get curve points
    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, SET_POINTS, POINTS), ESP_GMF_ARGS_TYPE_INT32, sizeof(int32_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append POINTS arg");
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, SET_POINTS, POINT_NUM), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), sizeof(int32_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append POINT_NUM arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, SET_POINTS), __drc_set_points, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, SET_POINTS));

    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, GET_POINT_NUM, POINT_NUM), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append POINT_NUM arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, GET_POINT_NUM), __drc_get_point_num, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, GET_POINT_NUM));

    args = NULL;
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, GET_POINTS, POINTS), ESP_GMF_ARGS_TYPE_INT32, sizeof(void *), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append POINTS arg");
    ret = esp_gmf_args_desc_append(&args, AMETHOD_ARG(DRC, GET_POINTS, POINT_NUM), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), sizeof(void *));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append POINT_NUM arg");
    ret = esp_gmf_method_append(&method, AMETHOD(DRC, GET_POINTS), __drc_get_points, args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s", AMETHOD(DRC, GET_POINTS));
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_drc_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    free_esp_ae_drc_cfg((esp_ae_drc_cfg_t *)OBJ_GET_CFG(self));
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(drc);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_set_attack_time(esp_gmf_element_handle_t handle, uint16_t attack)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    if (drc->drc_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_drc_set_attack_time(drc->drc_hd, attack);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC set attack error %d", ret);
    }
    cfg->drc_para.attack_time = attack;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_get_attack_time(esp_gmf_element_handle_t handle, uint16_t *attack)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, attack, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    if (drc->drc_hd) {
        esp_ae_err_t ret = esp_ae_drc_get_attack_time(drc->drc_hd, attack);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC get attack error %d", ret);
    } else {
        esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
        ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
        *attack = cfg->drc_para.attack_time;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_set_release_time(esp_gmf_element_handle_t handle, uint16_t release)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    if (drc->drc_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_drc_set_release_time(drc->drc_hd, release);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC set release error %d", ret);
    }
    cfg->drc_para.release_time = release;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_get_release_time(esp_gmf_element_handle_t handle, uint16_t *release)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, release, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    if (drc->drc_hd) {
        esp_ae_err_t ret = esp_ae_drc_get_release_time(drc->drc_hd, release);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC get release error %d", ret);
    } else {
        esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
        ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
        *release = cfg->drc_para.release_time;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_set_hold_time(esp_gmf_element_handle_t handle, uint16_t hold)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    if (drc->drc_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_drc_set_hold_time(drc->drc_hd, hold);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC set hold error %d", ret);
    }
    cfg->drc_para.hold_time = hold;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_get_hold_time(esp_gmf_element_handle_t handle, uint16_t *hold)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, hold, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    if (drc->drc_hd) {
        esp_ae_err_t ret = esp_ae_drc_get_hold_time(drc->drc_hd, hold);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC get hold error %d", ret);
    } else {
        esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
        ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
        *hold = cfg->drc_para.hold_time;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_set_makeup_gain(esp_gmf_element_handle_t handle, float makeup)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    if (drc->drc_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_drc_set_makeup_gain(drc->drc_hd, makeup);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC set makeup error %d", ret);
    }
    cfg->drc_para.makeup_gain = makeup;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_get_makeup_gain(esp_gmf_element_handle_t handle, float *makeup)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, makeup, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    if (drc->drc_hd) {
        esp_ae_err_t ret = esp_ae_drc_get_makeup_gain(drc->drc_hd, makeup);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC get makeup error %d", ret);
    } else {
        esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
        ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
        *makeup = cfg->drc_para.makeup_gain;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_set_knee_width(esp_gmf_element_handle_t handle, float knee)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    if (drc->drc_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_drc_set_knee_width(drc->drc_hd, knee);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC set knee error %d", ret);
    }
    cfg->drc_para.knee_width = knee;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_get_knee_width(esp_gmf_element_handle_t handle, float *knee)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, knee, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    if (drc->drc_hd) {
        esp_ae_err_t ret = esp_ae_drc_get_knee_width(drc->drc_hd, knee);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC get knee error %d", ret);
    } else {
        esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
        ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
        *knee = cfg->drc_para.knee_width;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_set_points(esp_gmf_element_handle_t handle, esp_ae_drc_curve_point *points, uint8_t point_num)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, points, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_CHECK(TAG, point_num >= 2 && point_num <= 6, {return ESP_GMF_ERR_INVALID_ARG;}, "Invalid DRC point number");
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    if (drc->drc_hd) {
        esp_ae_err_t ret = esp_ae_drc_set_curve_points(drc->drc_hd, points, point_num);
        ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_ERR_FAIL, "DRC set points error %d", ret);
    }
    esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    size_t points_size = point_num * sizeof(esp_ae_drc_curve_point);
    esp_ae_drc_curve_point *dup_points = esp_gmf_oal_calloc(1, points_size);
    ESP_GMF_MEM_VERIFY(TAG, dup_points, {return ESP_GMF_ERR_MEMORY_LACK;}, "drc points", (int)points_size);
    memcpy(dup_points, points, points_size);
    if (cfg->drc_para.point && (cfg->drc_para.point != esp_gmf_default_drc_points)) {
        esp_gmf_oal_free(cfg->drc_para.point);
    }
    cfg->drc_para.point = dup_points;
    cfg->drc_para.point_num = point_num;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_get_point_num(esp_gmf_element_handle_t handle, uint8_t *point_num)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, point_num, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    if (drc->drc_hd) {
        esp_ae_err_t ret = esp_ae_drc_get_curve_point_num(drc->drc_hd, point_num);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC get point num error %d", ret);
    } else {
        esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
        ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
        *point_num = cfg->drc_para.point_num;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_get_points(esp_gmf_element_handle_t handle, esp_ae_drc_curve_point *points)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, points, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    if (drc->drc_hd) {
        esp_ae_err_t ret = esp_ae_drc_get_curve_points(drc->drc_hd, points);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {return ESP_GMF_ERR_FAIL;}, "DRC get points error %d", ret);
        return ESP_GMF_ERR_OK;
    } else {
        esp_ae_drc_cfg_t *cfg = (esp_ae_drc_cfg_t *)OBJ_GET_CFG(handle);
        ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
        if (cfg->drc_para.point && cfg->drc_para.point_num > 0) {
            memcpy(points, cfg->drc_para.point, cfg->drc_para.point_num * sizeof(esp_ae_drc_curve_point));
            return ESP_GMF_ERR_OK;
        }
        ESP_LOGE(TAG, "No DRC points configured");
        return ESP_GMF_ERR_FAIL;
    }
}

static esp_gmf_job_err_t esp_gmf_drc_reset(esp_gmf_element_handle_t handle, void *para)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_drc_t *drc = (esp_gmf_drc_t *)handle;
    if (drc->drc_hd) {
        esp_ae_err_t ret = esp_ae_drc_reset(drc->drc_hd);
        if (ret != ESP_AE_ERR_OK) {
            return ESP_GMF_ERR_FAIL;
        }
    }
    ESP_LOGD(TAG, "DRC reset");
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_drc_init(esp_ae_drc_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_drc_t *drc = esp_gmf_oal_calloc(1, sizeof(esp_gmf_drc_t));
    ESP_GMF_MEM_VERIFY(TAG, drc, {return ESP_GMF_ERR_MEMORY_LACK;}, "drc", sizeof(esp_gmf_drc_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)drc;
    obj->new_obj = gmf_drc_new;
    obj->del_obj = esp_gmf_drc_destroy;
    esp_ae_drc_cfg_t *cfg = NULL;
    if (config) {
        if (config->drc_para.point == NULL) {
            config->drc_para.point = (esp_ae_drc_curve_point *)esp_gmf_default_drc_points;
            config->drc_para.point_num = sizeof(esp_gmf_default_drc_points) / sizeof(esp_ae_drc_curve_point);
        }
        ret = dupl_esp_ae_drc_cfg(config, &cfg);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto DRC_INIT_FAIL, "Failed to duplicate drc configuration");
    } else {
        esp_ae_drc_cfg_t dcfg = DEFAULT_ESP_GMF_DRC_CONFIG();
        dcfg.drc_para.point = (esp_ae_drc_curve_point *)esp_gmf_default_drc_points;
        dcfg.drc_para.point_num = sizeof(esp_gmf_default_drc_points) / sizeof(esp_ae_drc_curve_point);
        ret = dupl_esp_ae_drc_cfg(&dcfg, &cfg);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto DRC_INIT_FAIL, "Failed to duplicate default drc configuration");
    }
    ESP_GMF_CHECK(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto DRC_INIT_FAIL;}, "Failed to allocate drc configuration");
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_drc_cfg_t));
    ret = esp_gmf_obj_set_tag(obj, "aud_drc");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto DRC_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(drc, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto DRC_INIT_FAIL, "Failed to initialize drc element");
    ESP_GMF_ELEMENT_GET(drc)->ops.open = gmf_drc_open;
    ESP_GMF_ELEMENT_GET(drc)->ops.process = gmf_drc_process;
    ESP_GMF_ELEMENT_GET(drc)->ops.close = gmf_drc_close;
    ESP_GMF_ELEMENT_GET(drc)->ops.event_receiver = drc_received_event_handler;
    ESP_GMF_ELEMENT_GET(drc)->ops.load_caps = _load_drc_caps_func;
    ESP_GMF_ELEMENT_GET(drc)->ops.load_methods = _load_drc_methods_func;
    ESP_GMF_ELEMENT_GET(drc)->ops.reset = esp_gmf_drc_reset;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
DRC_INIT_FAIL:
    esp_gmf_drc_destroy(obj);
    return ret;
}
