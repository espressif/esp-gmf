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
#include "esp_gmf_reverb.h"
#include "esp_gmf_args_desc.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"

typedef struct {
    esp_gmf_audio_element_t  parent;
    esp_ae_reverb_handle_t   reverb_hd;
    uint8_t                  bytes_per_sample;
    bool                     need_reopen : 1;
} esp_gmf_reverb_t;

static const char *TAG = "ESP_GMF_REVERB";

static const esp_gmf_arg_constraint_t s_reverb_room_size_constraint = {
    .minimum.f64 = 0.0,
    .maximum.f64 = 1.0,
    .step.f64 = 0.01,
};

static const esp_gmf_arg_constraint_t s_reverb_wet_level_constraint = {
    .minimum.f64 = -96.0,
    .maximum.f64 = 0.0,
    .step.f64 = 0.1,
};

static const esp_gmf_arg_constraint_t s_reverb_damping_constraint = {
    .minimum.f64 = 0.0,
    .maximum.f64 = 1.0,
    .step.f64 = 0.01,
};

static const esp_gmf_arg_constraint_t s_reverb_dry_level_constraint = {
    .minimum.f64 = -96.0,
    .maximum.f64 = 0.0,
    .step.f64 = 0.1,
};

static const esp_gmf_arg_constraint_t s_reverb_pre_delay_constraint = {
    .minimum.u64 = 0,
    .maximum.u64 = 200,
    .step.u64 = 1,
};

static esp_gmf_err_t __reverb_set_room_size(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                            uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_reverb_set_room_size(handle, *((float *)buf));
}

static esp_gmf_err_t __reverb_get_room_size(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                            uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_reverb_get_room_size(handle, (float *)buf);
}

static esp_gmf_err_t __reverb_set_wet_level(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                            uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_reverb_set_wet_level(handle, *((float *)buf));
}

static esp_gmf_err_t __reverb_get_wet_level(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                            uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_reverb_get_wet_level(handle, (float *)buf);
}

static esp_gmf_err_t __reverb_set_damping(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                          uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_reverb_set_damping(handle, *((float *)buf));
}

static esp_gmf_err_t __reverb_get_damping(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                          uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_reverb_get_damping(handle, (float *)buf);
}

static esp_gmf_err_t __reverb_set_dry_level(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                            uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_reverb_set_dry_level(handle, *((float *)buf));
}

static esp_gmf_err_t __reverb_get_dry_level(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                            uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_reverb_get_dry_level(handle, (float *)buf);
}

static esp_gmf_err_t __reverb_set_pre_delay_ms(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                               uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_reverb_set_pre_delay_ms(handle, *((uint16_t *)buf));
}

static esp_gmf_err_t __reverb_get_pre_delay_ms(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                               uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_reverb_get_pre_delay_ms(handle, (uint16_t *)buf);
}

static esp_gmf_err_t __reverb_reset(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    return esp_gmf_reverb_reset(handle);
}

static esp_gmf_err_t esp_gmf_reverb_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_reverb_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_reverb_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_reverb_t *reverb = (esp_gmf_reverb_t *)self;
    esp_ae_reverb_cfg_t *reverb_info = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, reverb_info, {return ESP_GMF_JOB_ERR_FAIL;});
    esp_gmf_job_err_t job_ret = ESP_GMF_JOB_ERR_OK;
    reverb->bytes_per_sample = (reverb_info->bits_per_sample >> 3) * reverb_info->channel;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
    esp_ae_reverb_open(reverb_info, &reverb->reverb_hd);
    ESP_GMF_CHECK(TAG, reverb->reverb_hd, {job_ret = ESP_GMF_JOB_ERR_FAIL; goto __reverb_open_exit;}, "Failed to create reverb handle");
    reverb->need_reopen = false;
__reverb_open_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
    if (job_ret != ESP_GMF_JOB_ERR_OK) {
        return job_ret;
    }
    GMF_AUDIO_UPDATE_SND_INFO(self, reverb_info->sample_rate, reverb_info->bits_per_sample, reverb_info->channel);
    ESP_LOGD(TAG, "Open, %p", self);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t gmf_reverb_reset(esp_gmf_element_handle_t handle, void *para)
{
    return esp_gmf_reverb_reset(handle);
}

static esp_gmf_job_err_t esp_gmf_reverb_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_reverb_t *reverb = (esp_gmf_reverb_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
    if (reverb->reverb_hd != NULL) {
        esp_ae_reverb_close(reverb->reverb_hd);
        reverb->reverb_hd = NULL;
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_reverb_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_reverb_t *reverb = (esp_gmf_reverb_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (reverb->need_reopen) {
        esp_gmf_reverb_close(self, NULL);
        out_len = esp_gmf_reverb_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "Reverb reopen failed");
            return out_len;
        }
    }
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int samples_num = ESP_GMF_ELEMENT_GET(reverb)->in_attr.data_size / (reverb->bytes_per_sample);
    int bytes = samples_num * reverb->bytes_per_sample;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    samples_num = in_load->valid_size / (reverb->bytes_per_sample);
    bytes = samples_num * reverb->bytes_per_sample;
    if ((bytes != in_load->valid_size) || (load_ret < ESP_GMF_IO_OK)) {
        if (load_ret == ESP_GMF_IO_ABORT) {
            out_len = ESP_GMF_JOB_ERR_ABORT;
            goto __reverb_release;
        }
        ESP_LOGE(TAG, "Invalid in load size %d, ret %d", in_load->valid_size, load_ret);
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __reverb_release;
    }
    if (in_port->is_shared == 1) {
        out_load = in_load;
    }
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, samples_num ? bytes : in_load->buf_length, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, {goto __reverb_release;});
    if (samples_num > 0) {
        esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
        esp_ae_err_t ret = esp_ae_reverb_process(reverb->reverb_hd, samples_num, in_load->buf, out_load->buf);
        esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __reverb_release;}, "Reverb process error %d", ret);
    }
    ESP_LOGV(TAG, "Samples: %d, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d",
             samples_num, in_load, in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done,
             out_load, out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    out_load->valid_size = samples_num * reverb->bytes_per_sample;
    out_load->is_done = in_load->is_done;
    out_load->pts = in_load->pts;
    if (out_load->valid_size > 0) {
        esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, out_load->valid_size);
    }
    if (in_load->is_done) {
        out_len = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "Reverb done, out len: %d", out_load->valid_size);
    }
__reverb_release:
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

static esp_gmf_err_t reverb_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_ae_reverb_cfg_t *config = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_reverb_t *reverb = (esp_gmf_reverb_t *)self;
    reverb->need_reopen = reverb->need_reopen
                          || (config->sample_rate != info->sample_rates)
                          || (info->channels != config->channel)
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

static esp_gmf_err_t esp_gmf_reverb_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_reverb_t *reverb = (esp_gmf_reverb_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(reverb);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_reverb_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_REVERB;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_reverb_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_args_desc_t *get_args = NULL;
    esp_gmf_err_t ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(REVERB, SET_ROOM_SIZE, ROOM_SIZE),
                                                                ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0,
                                                                &s_reverb_room_size_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to append ROOM_SIZE argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(REVERB, SET_ROOM_SIZE), __reverb_set_room_size,
                                          set_args, AMETHOD(REVERB, GET_ROOM_SIZE), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(REVERB, SET_ROOM_SIZE));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    set_args = NULL;
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to copy argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(REVERB, GET_ROOM_SIZE), __reverb_get_room_size,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(REVERB, GET_ROOM_SIZE));
    get_args = NULL;

    ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(REVERB, SET_WET_LEVEL, WET_LEVEL),
                                                   ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0,
                                                   &s_reverb_wet_level_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to append WET_LEVEL argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(REVERB, SET_WET_LEVEL), __reverb_set_wet_level,
                                          set_args, AMETHOD(REVERB, GET_WET_LEVEL), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(REVERB, SET_WET_LEVEL));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    set_args = NULL;
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to copy argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(REVERB, GET_WET_LEVEL), __reverb_get_wet_level,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(REVERB, GET_WET_LEVEL));
    get_args = NULL;

    ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(REVERB, SET_DAMPING, DAMPING),
                                                   ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0,
                                                   &s_reverb_damping_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to append DAMPING argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(REVERB, SET_DAMPING), __reverb_set_damping,
                                          set_args, AMETHOD(REVERB, GET_DAMPING), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(REVERB, SET_DAMPING));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    set_args = NULL;
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to copy argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(REVERB, GET_DAMPING), __reverb_get_damping,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(REVERB, GET_DAMPING));
    get_args = NULL;

    ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(REVERB, SET_DRY_LEVEL, DRY_LEVEL),
                                                   ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0,
                                                   &s_reverb_dry_level_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to append DRY_LEVEL argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(REVERB, SET_DRY_LEVEL), __reverb_set_dry_level,
                                          set_args, AMETHOD(REVERB, GET_DRY_LEVEL), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(REVERB, SET_DRY_LEVEL));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    set_args = NULL;
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to copy argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(REVERB, GET_DRY_LEVEL), __reverb_get_dry_level,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(REVERB, GET_DRY_LEVEL));
    get_args = NULL;

    ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(REVERB, SET_PRE_DELAY, PRE_DELAY),
                                                   ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0,
                                                   &s_reverb_pre_delay_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to append PRE_DELAY argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(REVERB, SET_PRE_DELAY), __reverb_set_pre_delay_ms,
                                          set_args, AMETHOD(REVERB, GET_PRE_DELAY), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(REVERB, SET_PRE_DELAY));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    set_args = NULL;
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to copy argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(REVERB, GET_PRE_DELAY), __reverb_get_pre_delay_ms,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(REVERB, GET_PRE_DELAY));
    get_args = NULL;

    ret = esp_gmf_method_append_with_info(&method, AMETHOD(REVERB, RESET), __reverb_reset, NULL, NULL, false);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(REVERB, RESET));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
__fail:
    esp_gmf_method_destroy(method);
    if (set_args) {
        esp_gmf_args_desc_destroy(set_args);
    }
    if (get_args) {
        esp_gmf_args_desc_destroy(get_args);
    }
    return ret;
}

static esp_gmf_err_t reverb_set_float_param(esp_gmf_element_handle_t handle, esp_ae_reverb_handle_t reverb_hd,
                                            esp_ae_err_t (*set_fn)(esp_ae_reverb_handle_t, float),
                                            float value, float *cfg_field)
{
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (reverb_hd) {
        esp_ae_err_t ae_ret = set_fn(reverb_hd, value);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_ERR_FAIL;
            goto __exit;
        }
    }
    *cfg_field = value;
__exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

static esp_gmf_err_t reverb_get_float_param(esp_gmf_element_handle_t handle, esp_ae_reverb_handle_t reverb_hd,
                                            esp_ae_err_t (*get_fn)(esp_ae_reverb_handle_t, float *),
                                            float cfg_value, float *out)
{
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (reverb_hd) {
        esp_ae_err_t ae_ret = get_fn(reverb_hd, out);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_ERR_FAIL;
        }
    } else {
        *out = cfg_value;
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_reverb_set_room_size(esp_gmf_element_handle_t handle, float room_size)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_reverb_cfg_t *cfg = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    return reverb_set_float_param(handle, ((esp_gmf_reverb_t *)handle)->reverb_hd,
                                  esp_ae_reverb_set_room_size, room_size, &cfg->reverb_para.room_size);
}

esp_gmf_err_t esp_gmf_reverb_get_room_size(esp_gmf_element_handle_t handle, float *room_size)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_reverb_get_room_size", handle);
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, room_size, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_reverb_cfg_t *cfg = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    return reverb_get_float_param(handle, ((esp_gmf_reverb_t *)handle)->reverb_hd,
                                  esp_ae_reverb_get_room_size, cfg->reverb_para.room_size, room_size);
}

esp_gmf_err_t esp_gmf_reverb_set_damping(esp_gmf_element_handle_t handle, float damping)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_reverb_cfg_t *cfg = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    return reverb_set_float_param(handle, ((esp_gmf_reverb_t *)handle)->reverb_hd,
                                  esp_ae_reverb_set_damping, damping, &cfg->reverb_para.damping);
}

esp_gmf_err_t esp_gmf_reverb_get_damping(esp_gmf_element_handle_t handle, float *damping)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_reverb_get_damping", handle);
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, damping, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_reverb_cfg_t *cfg = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    return reverb_get_float_param(handle, ((esp_gmf_reverb_t *)handle)->reverb_hd,
                                  esp_ae_reverb_get_damping, cfg->reverb_para.damping, damping);
}

esp_gmf_err_t esp_gmf_reverb_set_wet_level(esp_gmf_element_handle_t handle, float wet_level)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_reverb_cfg_t *cfg = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    return reverb_set_float_param(handle, ((esp_gmf_reverb_t *)handle)->reverb_hd,
                                  esp_ae_reverb_set_wet_level, wet_level, &cfg->reverb_para.wet_level);
}

esp_gmf_err_t esp_gmf_reverb_get_wet_level(esp_gmf_element_handle_t handle, float *wet_level)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_reverb_get_wet_level", handle);
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, wet_level, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_reverb_cfg_t *cfg = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    return reverb_get_float_param(handle, ((esp_gmf_reverb_t *)handle)->reverb_hd,
                                  esp_ae_reverb_get_wet_level, cfg->reverb_para.wet_level, wet_level);
}

esp_gmf_err_t esp_gmf_reverb_set_dry_level(esp_gmf_element_handle_t handle, float dry_level)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_reverb_cfg_t *cfg = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    return reverb_set_float_param(handle, ((esp_gmf_reverb_t *)handle)->reverb_hd,
                                  esp_ae_reverb_set_dry_level, dry_level, &cfg->reverb_para.dry_level);
}

esp_gmf_err_t esp_gmf_reverb_get_dry_level(esp_gmf_element_handle_t handle, float *dry_level)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_reverb_get_dry_level", handle);
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, dry_level, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_reverb_cfg_t *cfg = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    return reverb_get_float_param(handle, ((esp_gmf_reverb_t *)handle)->reverb_hd,
                                  esp_ae_reverb_get_dry_level, cfg->reverb_para.dry_level, dry_level);
}

esp_gmf_err_t esp_gmf_reverb_set_pre_delay_ms(esp_gmf_element_handle_t handle, uint16_t pre_delay_ms)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_reverb_set_pre_delay_ms: pre_delay_ms=%u", handle, pre_delay_ms);
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    if (pre_delay_ms > 200) {
        ESP_LOGE(TAG, "Invalid pre_delay_ms(%u), range: [0, 200]", pre_delay_ms);
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_ae_reverb_cfg_t *cfg = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_reverb_t *reverb = (esp_gmf_reverb_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (cfg->reverb_para.pre_delay_ms == pre_delay_ms) {
        goto __reverb_set_pre_delay_exit;
    }
    cfg->reverb_para.pre_delay_ms = pre_delay_ms;
    /* AE allocates the pre-delay line in open(); changing it while running needs reopen. */
    if (reverb->reverb_hd != NULL) {
        reverb->need_reopen = true;
    }
__reverb_set_pre_delay_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_reverb_get_pre_delay_ms(esp_gmf_element_handle_t handle, uint16_t *pre_delay_ms)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_reverb_get_pre_delay_ms", handle);
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, pre_delay_ms, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_reverb_cfg_t *cfg = (esp_ae_reverb_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    *pre_delay_ms = cfg->reverb_para.pre_delay_ms;
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_reverb_reset(esp_gmf_element_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_reverb_t *reverb = (esp_gmf_reverb_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (reverb->reverb_hd) {
        esp_ae_err_t ae_ret = esp_ae_reverb_reset(reverb->reverb_hd);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_ERR_FAIL;
        }
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    ESP_LOGD(TAG, "Reverb reset");
    return ret;
}

esp_gmf_err_t esp_gmf_reverb_init(esp_ae_reverb_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_reverb_t *reverb = esp_gmf_oal_calloc(1, sizeof(esp_gmf_reverb_t));
    ESP_GMF_MEM_VERIFY(TAG, reverb, {return ESP_GMF_ERR_MEMORY_LACK;}, "reverb", sizeof(esp_gmf_reverb_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)reverb;
    obj->new_obj = esp_gmf_reverb_new;
    obj->del_obj = esp_gmf_reverb_destroy;
    esp_ae_reverb_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(esp_ae_reverb_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto REVERB_INIT_FAIL;}, "reverb configuration", sizeof(esp_ae_reverb_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_reverb_cfg_t));
    if (config) {
        memcpy(cfg, config, sizeof(esp_ae_reverb_cfg_t));
    } else {
        esp_ae_reverb_cfg_t dcfg = DEFAULT_ESP_GMF_REVERB_CONFIG();
        memcpy(cfg, &dcfg, sizeof(esp_ae_reverb_cfg_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_reverb");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto REVERB_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(reverb, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto REVERB_INIT_FAIL, "Failed to initialize reverb element");
    ESP_GMF_ELEMENT_GET(reverb)->ops.open = esp_gmf_reverb_open;
    ESP_GMF_ELEMENT_GET(reverb)->ops.process = esp_gmf_reverb_process;
    ESP_GMF_ELEMENT_GET(reverb)->ops.close = esp_gmf_reverb_close;
    ESP_GMF_ELEMENT_GET(reverb)->ops.event_receiver = reverb_received_event_handler;
    ESP_GMF_ELEMENT_GET(reverb)->ops.load_caps = _load_reverb_caps_func;
    ESP_GMF_ELEMENT_GET(reverb)->ops.load_methods = _load_reverb_methods_func;
    ESP_GMF_ELEMENT_GET(reverb)->ops.reset = gmf_reverb_reset;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
REVERB_INIT_FAIL:
    esp_gmf_reverb_destroy(obj);
    return ret;
}
