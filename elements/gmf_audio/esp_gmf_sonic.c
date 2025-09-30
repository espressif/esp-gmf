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
#include "esp_gmf_sonic.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"

#define SONIC_DEFAULT_OUTPUT_TIME_MS (10)

/**
 * @brief  Audio sonic context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t parent;            /*!< The GMF sonic handle */
    esp_ae_sonic_handle_t   sonic_hd;          /*!< The audio effects sonic handle */
    uint8_t                 bytes_per_sample;  /*!< Bytes number of per sampling point */
    uint32_t                sample_rate;       /*!< The audio sample rate */
    uint8_t                 bits_per_sample;   /*!< Bits number of per sampling point */
    uint8_t                 channel;           /*!< The audio channel */
    esp_ae_sonic_in_data_t  in_data_hd;        /*!< The sonic input data handle */
    esp_ae_sonic_out_data_t out_data_hd;       /*!< The sonic output data handle */
    float                   speed;             /*!< The audio speed */
    float                   pitch;             /*!< The audio pitch */
    int32_t                 out_size;          /*!< The acquired out size */
    int64_t                 cur_pts;           /*!< The audio Presentation Time Stamp(pts) */
    bool                    need_reopen : 1;   /*!< Whether need to reopen.
                                                    True: Execute the close function first, then execute the open function
                                                    False: Do nothing */
    bool                    is_done : 1;       /*!< Whether the input data is done */
} esp_gmf_sonic_t;

static const char *TAG = "ESP_GMF_SONIC";

static esp_gmf_err_t __sonic_set_speed(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                       uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    float *speed = (float *)buf;
    return esp_gmf_sonic_set_speed(handle, *speed);
}

static esp_gmf_err_t __sonic_get_speed(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                       uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    float *speed = (float *)buf;
    return esp_gmf_sonic_get_speed(handle, speed);
}

static esp_gmf_err_t __sonic_set_pitch(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                       uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    float *pitch = (float *)buf;
    return esp_gmf_sonic_set_pitch(handle, *pitch);
}

static esp_gmf_err_t __sonic_get_pitch(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                       uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    float *pitch = (float *)buf;
    return esp_gmf_sonic_get_pitch(handle, pitch);
}

static esp_gmf_err_t esp_gmf_sonic_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_sonic_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_sonic_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_sonic_t *sonic = (esp_gmf_sonic_t *)self;
    esp_ae_sonic_cfg_t *sonic_info = (esp_ae_sonic_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, sonic_info, {return ESP_GMF_JOB_ERR_FAIL;});
    sonic->sample_rate = sonic_info->sample_rate;
    sonic->channel = sonic_info->channel;
    sonic->bits_per_sample = sonic_info->bits_per_sample;
    sonic->bytes_per_sample = (sonic_info->bits_per_sample >> 3) * sonic_info->channel;
    GMF_AUDIO_UPDATE_SND_INFO(self, sonic_info->sample_rate, sonic_info->bits_per_sample, sonic_info->channel);
    sonic->out_size = SONIC_DEFAULT_OUTPUT_TIME_MS * sonic->sample_rate * sonic->bytes_per_sample / 1000;
    esp_ae_sonic_open(sonic_info, &sonic->sonic_hd);
    ESP_GMF_CHECK(TAG, sonic->sonic_hd, {return ESP_GMF_JOB_ERR_FAIL;}, "Failed to create sonic handle");
    esp_ae_sonic_set_speed(sonic->sonic_hd, sonic->speed);
    esp_ae_sonic_set_pitch(sonic->sonic_hd, sonic->pitch);
    sonic->need_reopen = false;
    ESP_LOGD(TAG, "Open, %p", self);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_sonic_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_sonic_t *sonic = (esp_gmf_sonic_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    if (sonic->sonic_hd != NULL) {
        esp_ae_sonic_close(sonic->sonic_hd);
        sonic->sonic_hd = NULL;
    }
    sonic->cur_pts = 0;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t gmf_sonic_bypass_process(esp_gmf_sonic_t *sonic, esp_gmf_port_handle_t in_port, esp_gmf_port_handle_t out_port,
                                                  esp_gmf_payload_t **in_load, esp_gmf_payload_t **out_load)
{
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    int load_ret = esp_gmf_port_acquire_in(in_port, in_load, ESP_GMF_ELEMENT_GET(sonic)->in_attr.data_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, load_ret, out_len, return out_len);
    sonic->is_done = (*in_load)->is_done;
    if (!sonic->is_done && (*in_load)->valid_size == 0) {
        return ESP_GMF_JOB_ERR_CONTINUE;
    }
    if (in_port->is_shared == 1) {
        *out_load = *in_load;
    }
    size_t out_size = (*in_load)->valid_size ? (*in_load)->valid_size : ESP_GMF_ELEMENT_GET(sonic)->in_attr.data_size;
    load_ret = esp_gmf_port_acquire_out(out_port, out_load, out_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, return out_len);
    if (in_port->is_shared != 1) {
        memcpy((*out_load)->buf, (*in_load)->buf, (*in_load)->valid_size);
        (*out_load)->valid_size = (*in_load)->valid_size;
        (*out_load)->is_done = (*in_load)->is_done;
        (*out_load)->pts = (*in_load)->pts;
    }
    return sonic->is_done ? ESP_GMF_JOB_ERR_DONE : out_len;
}

static esp_gmf_job_err_t esp_gmf_sonic_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_sonic_t *sonic = (esp_gmf_sonic_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    esp_gmf_err_io_t load_ret = ESP_GMF_IO_OK;
    if (sonic->need_reopen) {
        esp_gmf_sonic_close(self, NULL);
        out_len = esp_gmf_sonic_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "Sonic reopen failed");
            return out_len;
        }
    }
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    bool is_done = false;
    sonic->out_data_hd.needed_num = sonic->out_size / sonic->bytes_per_sample;
    if (sonic->speed == 1.0f && sonic->pitch == 1.0f && (sonic->out_data_hd.out_num < sonic->out_data_hd.needed_num)) {
        out_len = gmf_sonic_bypass_process(sonic, in_port, out_port, &in_load, &out_load);
        goto __sonic_release;
    } else {
        if ((sonic->out_data_hd.out_num < sonic->out_data_hd.needed_num) && sonic->is_done == false) {
            int samples_num = ESP_GMF_ELEMENT_GET(sonic)->in_attr.data_size / (sonic->bytes_per_sample);
            int bytes = samples_num * sonic->bytes_per_sample;
            load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
            samples_num = in_load->valid_size / (sonic->bytes_per_sample);
            bytes = samples_num * sonic->bytes_per_sample;
            sonic->is_done = in_load->is_done;
            if ((bytes != in_load->valid_size) || (load_ret < ESP_GMF_IO_OK)) {
                ESP_LOGE(TAG, "Invalid in load size %d, ret %d", in_load->valid_size, load_ret);
                out_len = ESP_GMF_JOB_ERR_FAIL;
                goto __sonic_release;
            }
            sonic->in_data_hd.samples = in_load->buf;
            sonic->in_data_hd.num = samples_num;
            sonic->cur_pts = in_load->pts;
            if (sonic->in_data_hd.num == 0) {
                if (sonic->is_done == true) {
                    out_len = ESP_GMF_JOB_ERR_DONE;
                    is_done = true;
                } else {
                    out_len = ESP_GMF_JOB_ERR_CONTINUE;
                    goto __sonic_release;
                }
            }
        }
        load_ret = esp_gmf_port_acquire_out(out_port, &out_load, sonic->out_size, ESP_GMF_MAX_DELAY);
        ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, goto __sonic_release);
        sonic->out_data_hd.samples = out_load->buf;
        out_load->valid_size = 0;
        out_load->pts = sonic->cur_pts;
        if (!is_done) {
            esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)self)->lock);
            esp_ae_err_t ret = esp_ae_sonic_process(sonic->sonic_hd, &sonic->in_data_hd, &sonic->out_data_hd);
            esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)self)->lock);
            ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __sonic_release;}, "Sonic process error %d", ret);
            sonic->in_data_hd.samples = ((uint8_t *)sonic->in_data_hd.samples) + sonic->in_data_hd.consume_num * sonic->bytes_per_sample;
            sonic->in_data_hd.num -= sonic->in_data_hd.consume_num;
            sonic->in_data_hd.consume_num = 0;
            out_load->valid_size = sonic->out_data_hd.out_num * sonic->bytes_per_sample;
            if (out_load->valid_size > 0) {
                esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, out_load->valid_size);
            }
            out_load->is_done = false;
            out_load->pts = sonic->cur_pts;
            sonic->cur_pts += (uint64_t)((float)(GMF_AUDIO_CALC_PTS(out_load->valid_size, sonic->sample_rate, sonic->channel, sonic->bits_per_sample)) * sonic->speed);
            ESP_LOGV(TAG, "%s, I: %p-buf: %p-sz: %d, O: %p-buf: %p-sz: %d, ret: %d", __func__, in_port,
                    in_load->buf, in_load->valid_size, out_port,
                    out_load->buf, out_load->buf_length, ret);
            if (sonic->out_data_hd.out_num == sonic->out_data_hd.needed_num) {
                out_len = ESP_GMF_JOB_ERR_TRUNCATE;
                goto __sonic_release;
            }
        }
        if (sonic->is_done == true) {
            out_load->is_done = true;
            out_len = ESP_GMF_JOB_ERR_DONE;
            goto __sonic_release;
        }
    }
__sonic_release:
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

static esp_gmf_err_t sonic_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_ae_sonic_cfg_t *config = (esp_ae_sonic_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_sonic_t *sonic = (esp_gmf_sonic_t *)self;
    sonic->need_reopen = (config->sample_rate != info->sample_rates) || (info->channels != config->channel) || (config->bits_per_sample != info->bits);
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

static esp_gmf_err_t esp_gmf_sonic_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_sonic_t *sonic = (esp_gmf_sonic_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(sonic);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_sonic_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_SONIC;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_sonic_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_args_desc_t *get_args = NULL;
    esp_gmf_err_t ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(SONIC, SET_SPEED, SPEED),
                                                 ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append argument");
    ret = esp_gmf_method_append(&method, AMETHOD(SONIC, SET_SPEED), __sonic_set_speed, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(SONIC, SET_SPEED));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy argument");
    ret = esp_gmf_method_append(&method, AMETHOD(SONIC, GET_SPEED), __sonic_get_speed, get_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(SONIC, GET_SPEED));

    set_args = NULL;
    get_args = NULL;
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(SONIC, SET_PITCH, PITCH), ESP_GMF_ARGS_TYPE_FLOAT, sizeof(float), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append argument");
    ret = esp_gmf_method_append(&method, AMETHOD(SONIC, SET_PITCH), __sonic_set_pitch, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(SONIC, SET_PITCH));

    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to copy argument");
    ret = esp_gmf_method_append(&method, AMETHOD(SONIC, GET_PITCH), __sonic_get_pitch, get_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(SONIC, GET_PITCH));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_sonic_set_speed(esp_gmf_element_handle_t handle, float speed)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_sonic_t *sonic = (esp_gmf_sonic_t *)handle;
    if (sonic->sonic_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_sonic_set_speed(sonic->sonic_hd, speed);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_JOB_ERR_FAIL;, "sonicualize set error %d", ret);
    }
    sonic->speed = speed;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_sonic_get_speed(esp_gmf_element_handle_t handle, float *speed)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, speed, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_sonic_t *sonic = (esp_gmf_sonic_t *)handle;
    if (sonic->sonic_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_sonic_get_speed(sonic->sonic_hd, speed);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_JOB_ERR_FAIL;, "sonicualize set error %d", ret);
    } else {
        *speed = sonic->speed;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_sonic_set_pitch(esp_gmf_element_handle_t handle, float pitch)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_sonic_t *sonic = (esp_gmf_sonic_t *)handle;
    if (sonic->sonic_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_sonic_set_pitch(sonic->sonic_hd, pitch);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_JOB_ERR_FAIL;, "sonicualize set error %d", ret);
    }
    sonic->pitch = pitch;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_sonic_get_pitch(esp_gmf_element_handle_t handle, float *pitch)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, pitch, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_sonic_t *sonic = (esp_gmf_sonic_t *)handle;
    if (sonic->sonic_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_sonic_get_pitch(sonic->sonic_hd, pitch);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_JOB_ERR_FAIL;, "sonicualize set error %d", ret);
    } else {
        *pitch = sonic->pitch;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_sonic_init(esp_ae_sonic_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_sonic_t *sonic = esp_gmf_oal_calloc(1, sizeof(esp_gmf_sonic_t));
    ESP_GMF_MEM_VERIFY(TAG, sonic, {return ESP_GMF_ERR_MEMORY_LACK;}, "sonic", sizeof(esp_gmf_sonic_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)sonic;
    obj->new_obj = esp_gmf_sonic_new;
    obj->del_obj = esp_gmf_sonic_destroy;
    esp_ae_sonic_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(esp_ae_sonic_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, ret = ESP_GMF_ERR_MEMORY_LACK; goto SONIC_INIT_FAIL;, "sonic configuration", sizeof(esp_ae_sonic_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_sonic_cfg_t));
    if (config) {
        memcpy(cfg, config, sizeof(esp_ae_sonic_cfg_t));
    } else {
        esp_ae_sonic_cfg_t dcfg = DEFAULT_ESP_GMF_SONIC_CONFIG();
        memcpy(cfg, &dcfg, sizeof(esp_ae_sonic_cfg_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_sonic");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto SONIC_INIT_FAIL, "Failed to set obj tag");
    sonic->speed = 1.0f;
    sonic->pitch = 1.0f;
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(sonic, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto SONIC_INIT_FAIL, "Failed to initialize sonic element");
    ESP_GMF_ELEMENT_GET(sonic)->ops.open = esp_gmf_sonic_open;
    ESP_GMF_ELEMENT_GET(sonic)->ops.process = esp_gmf_sonic_process;
    ESP_GMF_ELEMENT_GET(sonic)->ops.close = esp_gmf_sonic_close;
    ESP_GMF_ELEMENT_GET(sonic)->ops.event_receiver = sonic_received_event_handler;
    ESP_GMF_ELEMENT_GET(sonic)->ops.load_caps = _load_sonic_caps_func;
    ESP_GMF_ELEMENT_GET(sonic)->ops.load_methods = _load_sonic_methods_func;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
SONIC_INIT_FAIL:
    esp_gmf_sonic_destroy(obj);
    return ret;
}
