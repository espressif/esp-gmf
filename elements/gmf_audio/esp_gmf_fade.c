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
#include "esp_gmf_fade.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"

/**
 * @brief  Audio fade context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t parent;            /*!< The GMF fade handle */
    esp_ae_fade_handle_t    fade_hd;           /*!< The audio effects fade handle */
    uint8_t                 bytes_per_sample;  /*!< Bytes number of per sampling point */
    bool                    need_reopen : 1;   /*!< Whether need to reopen.
                                                    True: Execute the close function first, then execute the open function
                                                    False: Do nothing */
} esp_gmf_fade_t;

static const char *TAG = "ESP_GMF_FADE";

static const esp_gmf_arg_constraint_t s_fade_mode_constraint = {
    .minimum.i64 = ESP_AE_FADE_MODE_FADE_IN,
    .maximum.i64 = ESP_AE_FADE_MODE_FADE_OUT,
    .step.i64 = 1,
};

static const esp_gmf_arg_constraint_t s_fade_curve_constraint = {
    .minimum.i64 = ESP_AE_FADE_CURVE_LINE,
    .maximum.i64 = ESP_AE_FADE_CURVE_SQRT,
    .step.i64 = 1,
};

static const esp_gmf_arg_constraint_t s_fade_transit_time_constraint = {
    .minimum.u64 = 0,
    .maximum.u64 = UINT32_MAX,
    .step.u64 = 1,
};

static esp_gmf_err_t __fade_set_mode(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                     uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_fade_set_mode(handle, *((esp_ae_fade_mode_t *)buf));
}

static esp_gmf_err_t __fade_get_mode(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                     uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_fade_get_mode(handle, (esp_ae_fade_mode_t *)buf);
}

static esp_gmf_err_t __fade_set_curve(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_fade_set_curve(handle, *((esp_ae_fade_curve_t *)buf));
}

static esp_gmf_err_t __fade_get_curve(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_fade_get_curve(handle, (esp_ae_fade_curve_t *)buf);
}

static esp_gmf_err_t __fade_set_transit_time(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                             uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_fade_set_transit_time(handle, *((uint32_t *)buf));
}

static esp_gmf_err_t __fade_get_transit_time(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                             uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    return esp_gmf_fade_get_transit_time(handle, (uint32_t *)buf);
}

static esp_gmf_err_t __fade_reset(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                  uint8_t *buf, int buf_len)
{
    return esp_gmf_fade_reset(handle);
}

static esp_gmf_err_t esp_gmf_fade_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_fade_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_fade_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_fade_t *fade = (esp_gmf_fade_t *)self;
    esp_ae_fade_cfg_t *fade_info = (esp_ae_fade_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, fade_info, {return ESP_GMF_JOB_ERR_FAIL;});
    esp_gmf_job_err_t job_ret = ESP_GMF_JOB_ERR_OK;
    fade->bytes_per_sample = (fade_info->bits_per_sample >> 3) * fade_info->channel;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
    esp_ae_fade_open(fade_info, &fade->fade_hd);
    ESP_GMF_CHECK(TAG, fade->fade_hd, {job_ret = ESP_GMF_JOB_ERR_FAIL; goto __fade_open_exit;}, "Failed to create fade handle");
    fade->need_reopen = false;
__fade_open_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
    if (job_ret != ESP_GMF_JOB_ERR_OK) {
        return job_ret;
    }
    GMF_AUDIO_UPDATE_SND_INFO(self, fade_info->sample_rate, fade_info->bits_per_sample, fade_info->channel);
    ESP_LOGD(TAG, "Open, %p", self);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_fade_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_fade_t *fade = (esp_gmf_fade_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
    if (fade->fade_hd != NULL) {
        esp_ae_fade_close(fade->fade_hd);
        fade->fade_hd = NULL;
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_fade_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_fade_t *fade = (esp_gmf_fade_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (fade->need_reopen) {
        esp_gmf_fade_close(self, NULL);
        out_len = esp_gmf_fade_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "Fade reopen failed");
            return out_len;
        }
    }
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int samples_num = ESP_GMF_ELEMENT_GET(fade)->in_attr.data_size / (fade->bytes_per_sample);
    int bytes = samples_num * fade->bytes_per_sample;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    samples_num = in_load->valid_size / (fade->bytes_per_sample);
    bytes = samples_num * fade->bytes_per_sample;
    if ((bytes != in_load->valid_size) || (load_ret < ESP_GMF_IO_OK)) {
        if (load_ret == ESP_GMF_IO_ABORT) {
            out_len = ESP_GMF_JOB_ERR_ABORT;
            goto __fade_release;
        }
        ESP_LOGE(TAG, "Invalid in load size %d, ret %d", in_load->valid_size, load_ret);
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __fade_release;
    }
    if (in_port->is_shared == 1) {
        out_load = in_load;
    }
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, samples_num ? bytes : in_load->buf_length, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, { goto __fade_release;});
    if (samples_num > 0) {
        esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
        esp_ae_err_t ret = esp_ae_fade_process(fade->fade_hd, samples_num, in_load->buf, out_load->buf);
        esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(self)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __fade_release;}, "Fade process error %d", ret);
    }
    ESP_LOGV(TAG, "Samples: %d, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d",
             samples_num, in_load, in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done,
             out_load, out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    out_load->valid_size = samples_num * fade->bytes_per_sample;
    out_load->is_done = in_load->is_done;
    out_load->pts = in_load->pts;
    if (out_load->valid_size > 0) {
        esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, out_load->valid_size);
    }
    if (in_load->is_done) {
        out_len = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "Fade done, out len: %d", out_load->valid_size);
    }
__fade_release:
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

static esp_gmf_err_t fade_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_ae_fade_cfg_t *config = (esp_ae_fade_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_fade_t *fade = (esp_gmf_fade_t *)self;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(self)->lock);
    fade->need_reopen = fade->need_reopen
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

static esp_gmf_err_t esp_gmf_fade_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_fade_t *fade = (esp_gmf_fade_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(fade);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_fade_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_FADE;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_fade_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_args_desc_t *get_args = NULL;
    esp_gmf_err_t ret = esp_gmf_args_desc_append_with_constraint(
        &set_args, AMETHOD_ARG(FADE, SET_MODE, MODE), ESP_GMF_ARGS_TYPE_INT32,
        sizeof(int32_t), 0, &s_fade_mode_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append MODE argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(FADE, SET_MODE), __fade_set_mode,
                                          set_args, AMETHOD(FADE, GET_MODE), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(FADE, SET_MODE));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(FADE, GET_MODE), __fade_get_mode,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(FADE, GET_MODE));

    set_args = NULL;
    get_args = NULL;
    ret = esp_gmf_args_desc_append_with_constraint(
        &set_args, AMETHOD_ARG(FADE, SET_CURVE, CURVE), ESP_GMF_ARGS_TYPE_INT32,
        sizeof(int32_t), 0, &s_fade_curve_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append CURVE argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(FADE, SET_CURVE), __fade_set_curve,
                                          set_args, AMETHOD(FADE, GET_CURVE), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(FADE, SET_CURVE));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(FADE, GET_CURVE), __fade_get_curve,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(FADE, GET_CURVE));

    set_args = NULL;
    get_args = NULL;
    ret = esp_gmf_args_desc_append_with_constraint(
        &set_args, AMETHOD_ARG(FADE, SET_TRANSIT_TIME, TRANSIT_TIME), ESP_GMF_ARGS_TYPE_UINT32,
        sizeof(uint32_t), 0, &s_fade_transit_time_constraint);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append TRANSIT_TIME argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(FADE, SET_TRANSIT_TIME), __fade_set_transit_time,
                                          set_args, AMETHOD(FADE, GET_TRANSIT_TIME), true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(FADE, SET_TRANSIT_TIME));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy argument");
    ret = esp_gmf_method_append_with_info(&method, AMETHOD(FADE, GET_TRANSIT_TIME), __fade_get_transit_time,
                                          get_args, NULL, true);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(FADE, GET_TRANSIT_TIME));

    ret = esp_gmf_method_append_with_info(&method, AMETHOD(FADE, RESET), __fade_reset,
                                          NULL, NULL, false);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(FADE, RESET));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_fade_set_mode(esp_gmf_element_handle_t handle, esp_ae_fade_mode_t mode)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_fade_set_mode: mode=%u", handle, (unsigned)mode);
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_fade_t *fade = (esp_gmf_fade_t *)handle;
    esp_ae_fade_cfg_t *cfg = (esp_ae_fade_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_err_t ret = ESP_GMF_JOB_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (fade->fade_hd) {
        esp_ae_err_t ae_ret = esp_ae_fade_set_mode(fade->fade_hd, mode);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_JOB_ERR_FAIL;
            goto __fade_set_mode_exit;
        }
    }
    cfg->mode = mode;
__fade_set_mode_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_fade_get_mode(esp_gmf_element_handle_t handle, esp_ae_fade_mode_t *mode)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_fade_get_mode", handle);
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, mode, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_fade_cfg_t *cfg = (esp_ae_fade_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_fade_t *fade = (esp_gmf_fade_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_JOB_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (fade->fade_hd) {
        esp_ae_err_t ae_ret = esp_ae_fade_get_mode(fade->fade_hd, mode);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_JOB_ERR_FAIL;
            goto __fade_get_mode_exit;
        }
    } else {
        *mode = cfg->mode;
    }
__fade_get_mode_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_fade_set_curve(esp_gmf_element_handle_t handle, esp_ae_fade_curve_t curve)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_fade_set_curve: curve=%u", handle, (unsigned)curve);
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    esp_gmf_fade_t *fade = (esp_gmf_fade_t *)handle;
    esp_ae_fade_cfg_t *cfg = (esp_ae_fade_cfg_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Failed to set curve, cfg is NULL");
        ret = ESP_GMF_ERR_FAIL;
        goto __fade_set_curve_exit;
    }
    if (cfg->curve == curve) {
        goto __fade_set_curve_exit;
    }
    cfg->curve = curve;
    /* Curve is applied at open; changing it while running needs reopen. */
    if (fade->fade_hd != NULL) {
        fade->need_reopen = true;
    }
__fade_set_curve_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_fade_get_curve(esp_gmf_element_handle_t handle, esp_ae_fade_curve_t *curve)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_fade_get_curve", handle);
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, curve, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    esp_ae_fade_cfg_t *cfg = (esp_ae_fade_cfg_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL) {
        ret = ESP_GMF_ERR_FAIL;
    } else {
        *curve = cfg->curve;
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_fade_set_transit_time(esp_gmf_element_handle_t handle, uint32_t transit_time)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_fade_set_transit_time: transit_time=%u", handle, (unsigned)transit_time);
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    esp_gmf_fade_t *fade = (esp_gmf_fade_t *)handle;
    esp_ae_fade_cfg_t *cfg = (esp_ae_fade_cfg_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL) {
        ESP_LOGE(TAG, "Failed to set transit_time, cfg is NULL");
        ret = ESP_GMF_ERR_FAIL;
        goto __fade_set_transit_time_exit;
    }
    if (cfg->transit_time == transit_time) {
        goto __fade_set_transit_time_exit;
    }
    cfg->transit_time = transit_time;
    /* Transit time is applied at open; changing it while running needs reopen. */
    if (fade->fade_hd != NULL) {
        fade->need_reopen = true;
    }
__fade_set_transit_time_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_fade_get_transit_time(esp_gmf_element_handle_t handle, uint32_t *transit_time)
{
    ESP_LOGI(TAG, "handle:%p esp_gmf_fade_get_transit_time", handle);
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, transit_time, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    esp_ae_fade_cfg_t *cfg = (esp_ae_fade_cfg_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL) {
        ret = ESP_GMF_ERR_FAIL;
    } else {
        *transit_time = cfg->transit_time;
    }
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_fade_reset_weight(esp_gmf_element_handle_t handle)
{
    return esp_gmf_fade_reset(handle);
}

esp_gmf_err_t esp_gmf_fade_reset(esp_gmf_element_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_fade_cfg_t *cfg = (esp_ae_fade_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_fade_t *fade = (esp_gmf_fade_t *)handle;
    esp_gmf_err_t ret = ESP_GMF_JOB_ERR_OK;
    esp_gmf_oal_mutex_lock(ESP_GMF_ELEMENT_GET(handle)->lock);
    if (fade->fade_hd) {
        esp_ae_err_t ae_ret = esp_ae_fade_reset(fade->fade_hd);
        if (ae_ret != ESP_AE_ERR_OK) {
            ret = ESP_GMF_JOB_ERR_FAIL;
            goto __fade_reset_exit;
        }
        esp_ae_fade_get_mode(fade->fade_hd, &cfg->mode);
    }
__fade_reset_exit:
    esp_gmf_oal_mutex_unlock(ESP_GMF_ELEMENT_GET(handle)->lock);
    ESP_LOGD(TAG, "Fade reset");
    return ret;
}

static esp_gmf_job_err_t gmf_fade_reset(esp_gmf_element_handle_t handle, void *para)
{
    return esp_gmf_fade_reset(handle);
}

esp_gmf_err_t esp_gmf_fade_init(esp_ae_fade_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_fade_t *fade = esp_gmf_oal_calloc(1, sizeof(esp_gmf_fade_t));
    ESP_GMF_MEM_VERIFY(TAG, fade, {return ESP_GMF_ERR_MEMORY_LACK;}, "fade", sizeof(esp_gmf_fade_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)fade;
    obj->new_obj = esp_gmf_fade_new;
    obj->del_obj = esp_gmf_fade_destroy;
    esp_ae_fade_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(esp_ae_fade_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto FADE_INIT_FAIL;}, "fade configuration", sizeof(esp_ae_fade_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_fade_cfg_t));
    if (config) {
        memcpy(cfg, config, sizeof(esp_ae_fade_cfg_t));
    } else {
        esp_ae_fade_cfg_t dcfg = DEFAULT_ESP_GMF_FADE_CONFIG();
        memcpy(cfg, &dcfg, sizeof(esp_ae_fade_cfg_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_fade");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto FADE_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(fade, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto FADE_INIT_FAIL, "Failed to initialize fade element");
    ESP_GMF_ELEMENT_GET(fade)->ops.open = esp_gmf_fade_open;
    ESP_GMF_ELEMENT_GET(fade)->ops.process = esp_gmf_fade_process;
    ESP_GMF_ELEMENT_GET(fade)->ops.close = esp_gmf_fade_close;
    ESP_GMF_ELEMENT_GET(fade)->ops.event_receiver = fade_received_event_handler;
    ESP_GMF_ELEMENT_GET(fade)->ops.load_caps = _load_fade_caps_func;
    ESP_GMF_ELEMENT_GET(fade)->ops.load_methods = _load_fade_methods_func;
    ESP_GMF_ELEMENT_GET(fade)->ops.reset = gmf_fade_reset;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
FADE_INIT_FAIL:
    esp_gmf_fade_destroy(obj);
    return ret;
}
