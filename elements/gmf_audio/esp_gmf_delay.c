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
#include "esp_gmf_delay.h"
#include "esp_gmf_args_desc.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"

typedef struct {
    esp_gmf_audio_element_t   parent;
    esp_ae_delay_handle_t     delay_hd;
    uint8_t                   bytes_per_sample;
    bool                      need_reopen : 1;
    esp_gmf_arg_constraint_t  delay_time_constraint;
} esp_gmf_delay_t;

static const char *TAG = "ESP_GMF_DELAY";

/** AE treats max_delay_ms == 0 as the library default (1000 ms). */
static uint16_t delay_effective_max_ms(uint16_t max_delay_ms)
{
    return max_delay_ms == 0 ? 1000 : max_delay_ms;
}

static void delay_update_time_constraint(esp_gmf_delay_t *delay, uint16_t max_delay_ms)
{
    delay->delay_time_constraint.minimum.u64 = 0;
    delay->delay_time_constraint.maximum.u64 = delay_effective_max_ms(max_delay_ms);
    delay->delay_time_constraint.step.u64    = 1;
}

static const esp_gmf_arg_constraint_t s_delay_mix_constraint = {
    .minimum.f64 = 0.0,
    .maximum.f64 = 1.0,
    .step.f64    = 0.01,
};

static const esp_gmf_arg_constraint_t s_delay_feedback_constraint = {
    .minimum.f64 = 0.0,
    .maximum.f64 = 0.95,
    .step.f64    = 0.01,
};

static esp_gmf_err_t __delay_set_delay_time(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                            uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_delay_set_delay_time(handle, *((uint16_t *)buf));
}

static esp_gmf_err_t __delay_get_delay_time(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                            uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_delay_get_delay_time(handle, (uint16_t *)buf);
}

static esp_gmf_err_t __delay_set_feedback(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                          uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_delay_set_feedback(handle, *((float *)buf));
}

static esp_gmf_err_t __delay_get_feedback(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                          uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_delay_get_feedback(handle, (float *)buf);
}

static esp_gmf_err_t __delay_set_mix_ratio(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                           uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_delay_set_mix_ratio(handle, *((float *)buf));
}

static esp_gmf_err_t __delay_get_mix_ratio(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                           uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_delay_get_mix_ratio(handle, (float *)buf);
}

static esp_gmf_err_t __delay_reset(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                   uint8_t *buf, int buf_len)
{
    return esp_gmf_delay_reset(handle);
}

static esp_gmf_err_t esp_gmf_delay_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_delay_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_delay_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)self;
    esp_ae_delay_cfg_t *delay_info = (esp_ae_delay_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, delay_info, {return ESP_GMF_JOB_ERR_FAIL;});
    esp_gmf_job_err_t job_ret = ESP_GMF_JOB_ERR_OK;
    delay->bytes_per_sample = (delay_info->bits_per_sample >> 3) * delay_info->channel;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
    esp_ae_delay_open(delay_info, &delay->delay_hd);
    ESP_GMF_CHECK(TAG, delay->delay_hd, {job_ret = ESP_GMF_JOB_ERR_FAIL; goto __delay_open_exit;}, "Failed to create delay handle");
    delay->need_reopen = false;
__delay_open_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
    if (job_ret != ESP_GMF_JOB_ERR_OK) {
        return job_ret;
    }
    GMF_AUDIO_UPDATE_SND_INFO(self, delay_info->sample_rate, delay_info->bits_per_sample, delay_info->channel);
    ESP_LOGD(TAG, "Open, %p", self);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_delay_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
    if (delay->delay_hd != NULL) {
        esp_ae_delay_close(delay->delay_hd);
        delay->delay_hd = NULL;
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_delay_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (delay->need_reopen) {
        esp_gmf_delay_close(self, NULL);
        out_len = esp_gmf_delay_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "Delay reopen failed");
            return out_len;
        }
    }
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int samples_num = ESP_GMF_ELEMENT_GET(delay)->in_attr.data_size / (delay->bytes_per_sample);
    int bytes = samples_num * delay->bytes_per_sample;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    samples_num = in_load->valid_size / (delay->bytes_per_sample);
    bytes = samples_num * delay->bytes_per_sample;
    if ((bytes != in_load->valid_size) || (load_ret < ESP_GMF_IO_OK)) {
        if (load_ret == ESP_GMF_IO_ABORT) {
            out_len = ESP_GMF_JOB_ERR_ABORT;
            goto __delay_release;
        }
        ESP_LOGE(TAG, "Invalid in load size %d, ret %d", in_load->valid_size, load_ret);
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __delay_release;
    }
    if (in_port->is_shared == 1) {
        out_load = in_load;
    }
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, samples_num ? bytes : in_load->buf_length, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, {goto __delay_release;});
    if (samples_num > 0) {
        esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
        esp_ae_err_t ret = esp_ae_delay_process(delay->delay_hd, samples_num, in_load->buf, out_load->buf);
        esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __delay_release;}, "Delay process error %d", ret);
    }
    ESP_LOGV(TAG, "Samples: %d, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d",
             samples_num, in_load, in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done,
             out_load, out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    out_load->valid_size = samples_num * delay->bytes_per_sample;
    out_load->is_done = in_load->is_done;
    out_load->pts = in_load->pts;
    if (out_load->valid_size > 0) {
        esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, out_load->valid_size);
    }
    if (in_load->is_done) {
        out_len = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "Delay done, out len: %d", out_load->valid_size);
    }
__delay_release:
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

static esp_gmf_err_t delay_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_ae_delay_cfg_t *config = (esp_ae_delay_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)self;
    delay->need_reopen = (config->sample_rate != info->sample_rates) || (info->channels != config->channel)
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

static esp_gmf_job_err_t gmf_delay_reset(esp_gmf_element_handle_t handle, void *para)
{
    return esp_gmf_delay_reset(handle);
}

static esp_gmf_err_t esp_gmf_delay_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(delay);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_delay_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_DELAY;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_delay_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)handle;
    esp_ae_delay_cfg_t *cfg = (esp_ae_delay_cfg_t *)OBJ_GET_CFG(handle);
    if (cfg) {
        delay_update_time_constraint(delay, cfg->max_delay_ms);
    }

    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_args_desc_t *get_args = NULL;
    esp_gmf_err_t ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(DELAY, SET_DELAY_TIME, DELAY_TIME),
                                                                 ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0,
                                                                 &delay->delay_time_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to append DELAY_TIME argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(DELAY, SET_DELAY_TIME), __delay_set_delay_time,
                                          set_args, AMETHOD(DELAY, GET_DELAY_TIME), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(DELAY, SET_DELAY_TIME));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    set_args = NULL;
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to copy argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(DELAY, GET_DELAY_TIME), __delay_get_delay_time,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(DELAY, GET_DELAY_TIME));
    get_args = NULL;

    ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(DELAY, SET_MIX_RATIO, MIX_RATIO),
                                                   ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0,
                                                   &s_delay_mix_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to append MIX_RATIO argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(DELAY, SET_MIX_RATIO), __delay_set_mix_ratio,
                                          set_args, AMETHOD(DELAY, GET_MIX_RATIO), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(DELAY, SET_MIX_RATIO));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    set_args = NULL;
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to copy argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(DELAY, GET_MIX_RATIO), __delay_get_mix_ratio,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(DELAY, GET_MIX_RATIO));
    get_args = NULL;

    ret = esp_gmf_args_desc_append_with_constraint(&set_args, AMETHOD_ARG(DELAY, SET_FEEDBACK, FEEDBACK),
                                                   ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0,
                                                   &s_delay_feedback_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to append FEEDBACK argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(DELAY, SET_FEEDBACK), __delay_set_feedback,
                                          set_args, AMETHOD(DELAY, GET_FEEDBACK), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(DELAY, SET_FEEDBACK));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    set_args = NULL;
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {goto __fail;}, "Failed to copy argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(DELAY, GET_FEEDBACK), __delay_get_feedback,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(DELAY, GET_FEEDBACK));
    get_args = NULL;

    ret = esp_gmf_method_append_with_info(&method, AMETHOD(DELAY, RESET), __delay_reset, NULL, NULL, false);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {goto __fail;}, "Failed to register %s method", AMETHOD(DELAY, RESET));

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

esp_gmf_err_t esp_gmf_delay_set_delay_time(esp_gmf_element_handle_t handle, uint16_t delay_time_ms)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_delay_cfg_t *cfg = (esp_ae_delay_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (delay->delay_hd) {
        esp_ae_err_t ae_ret = esp_ae_delay_set_delay_time(delay->delay_hd, delay_time_ms);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_ERR_FAIL;
            goto __exit;
        }
    }
    cfg->delay_para.delay_time_ms = delay_time_ms;
__exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_delay_get_delay_time(esp_gmf_element_handle_t handle, uint16_t *delay_time_ms)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, delay_time_ms, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_delay_cfg_t *cfg = (esp_ae_delay_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (delay->delay_hd) {
        esp_ae_err_t ae_ret = esp_ae_delay_get_delay_time(delay->delay_hd, delay_time_ms);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_ERR_FAIL;
        }
    } else {
        *delay_time_ms = cfg->delay_para.delay_time_ms;
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_delay_set_feedback(esp_gmf_element_handle_t handle, float feedback)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_delay_cfg_t *cfg = (esp_ae_delay_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (delay->delay_hd) {
        esp_ae_err_t ae_ret = esp_ae_delay_set_feedback(delay->delay_hd, feedback);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_ERR_FAIL;
            goto __exit;
        }
    }
    cfg->delay_para.feedback = feedback;
__exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_delay_get_feedback(esp_gmf_element_handle_t handle, float *feedback)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, feedback, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_delay_cfg_t *cfg = (esp_ae_delay_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (delay->delay_hd) {
        esp_ae_err_t ae_ret = esp_ae_delay_get_feedback(delay->delay_hd, feedback);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_ERR_FAIL;
        }
    } else {
        *feedback = cfg->delay_para.feedback;
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_delay_set_mix_ratio(esp_gmf_element_handle_t handle, float mix_ratio)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_delay_cfg_t *cfg = (esp_ae_delay_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (delay->delay_hd) {
        esp_ae_err_t ae_ret = esp_ae_delay_set_mix(delay->delay_hd, mix_ratio);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_ERR_FAIL;
            goto __exit;
        }
    }
    cfg->delay_para.mix = mix_ratio;
__exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_delay_get_mix_ratio(esp_gmf_element_handle_t handle, float *mix_ratio)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, mix_ratio, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_delay_cfg_t *cfg = (esp_ae_delay_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (delay->delay_hd) {
        esp_ae_err_t ae_ret = esp_ae_delay_get_mix(delay->delay_hd, mix_ratio);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_ERR_FAIL;
        }
    } else {
        *mix_ratio = cfg->delay_para.mix;
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_delay_reset(esp_gmf_element_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_delay_t *delay = (esp_gmf_delay_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (delay->delay_hd) {
        esp_ae_err_t ae_ret = esp_ae_delay_reset(delay->delay_hd);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_ERR_FAIL;
        }
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    ESP_LOGD(TAG, "Delay reset");
    return ret;
}

esp_gmf_err_t esp_gmf_delay_init(esp_ae_delay_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_delay_t *delay = esp_gmf_oal_calloc(1, sizeof(esp_gmf_delay_t));
    ESP_GMF_MEM_VERIFY(TAG, delay, {return ESP_GMF_ERR_MEMORY_LACK;}, "delay", sizeof(esp_gmf_delay_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)delay;
    obj->new_obj = esp_gmf_delay_new;
    obj->del_obj = esp_gmf_delay_destroy;
    esp_ae_delay_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(esp_ae_delay_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto DELAY_INIT_FAIL;}, "delay configuration", sizeof(esp_ae_delay_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_delay_cfg_t));
    if (config) {
        memcpy(cfg, config, sizeof(esp_ae_delay_cfg_t));
    } else {
        esp_ae_delay_cfg_t dcfg = DEFAULT_ESP_GMF_DELAY_CONFIG();
        memcpy(cfg, &dcfg, sizeof(esp_ae_delay_cfg_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_delay");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto DELAY_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(delay, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto DELAY_INIT_FAIL, "Failed to initialize delay element");
    ESP_GMF_ELEMENT_GET(delay)->ops.open = esp_gmf_delay_open;
    ESP_GMF_ELEMENT_GET(delay)->ops.process = esp_gmf_delay_process;
    ESP_GMF_ELEMENT_GET(delay)->ops.close = esp_gmf_delay_close;
    ESP_GMF_ELEMENT_GET(delay)->ops.event_receiver = delay_received_event_handler;
    ESP_GMF_ELEMENT_GET(delay)->ops.load_caps = _load_delay_caps_func;
    ESP_GMF_ELEMENT_GET(delay)->ops.load_methods = _load_delay_methods_func;
    ESP_GMF_ELEMENT_GET(delay)->ops.reset = gmf_delay_reset;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
DELAY_INIT_FAIL:
    esp_gmf_delay_destroy(obj);
    return ret;
}
