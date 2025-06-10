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
#include "esp_gmf_mixer.h"
#include "esp_heap_caps.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"
#ifdef MIXER_DEBUG
#include "esp_timer.h"
#endif

#define MIXER_DEFAULT_PROC_TIME_MS      (10)
#define MIXER_GET_FRAME_BYTE_SIZE(info) (MIXER_DEFAULT_PROC_TIME_MS * info->sample_rate * info->channel * info->bits_per_sample / 8000)

typedef struct {
    esp_gmf_audio_element_t parent;            /*!< The GMF mixer handle */
    esp_ae_mixer_handle_t   mixer_hd;          /*!< The audio effects mixer handle */
    uint8_t                 bytes_per_sample;  /*!< Bytes number of per sampling point */
    uint32_t                process_num;       /*!< The data number of mixer processing */
    uint32_t                frame_time;        /*!< The time of one frame, unit: ms */
    esp_gmf_payload_t     **in_load;           /*!< The array of input payload */
    esp_gmf_payload_t      *out_load;          /*!< The output payload */
    uint8_t               **in_arr;            /*!< The input buffer pointer array of mixer */
    esp_ae_mixer_mode_t    *mode;              /*!< The mixer mode */
    uint8_t                 src_num;           /*!< The number of source */
    bool                    need_reopen : 1;   /*!< Whether need to reopen.
                                                    True: Execute the close function first, then execute the open function
                                                    False: Do nothing */
} esp_gmf_mixer_t;

static const char *TAG = "ESP_GMF_MIXER";

const esp_ae_mixer_info_t esp_gmf_default_mixer_src_info[] = {
    {1.0, 0.5, 500},
    {0.5, 0.0, 500},
};

static inline esp_gmf_err_t dupl_esp_ae_mixer_cfg(esp_ae_mixer_cfg_t *config, esp_ae_mixer_cfg_t **new_config)
{
    void *sub_cfg = NULL;
    *new_config = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, *new_config, {return ESP_GMF_ERR_MEMORY_LACK;}, "mixer configuration", sizeof(*config));
    memcpy(*new_config, config, sizeof(*config));
    if (config->src_info && (config->src_num > 0)) {
        sub_cfg = esp_gmf_oal_calloc(1, config->src_num * sizeof(esp_ae_mixer_info_t));
        ESP_GMF_MEM_VERIFY(TAG, sub_cfg, {esp_gmf_oal_free(*new_config); return ESP_GMF_ERR_MEMORY_LACK;},
                           "source information", config->src_num * sizeof(esp_ae_mixer_info_t));
        memcpy(sub_cfg, config->src_info, config->src_num * sizeof(esp_ae_mixer_info_t));
        (*new_config)->src_info = sub_cfg;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static inline void free_esp_ae_mixer_cfg(esp_ae_mixer_cfg_t *config)
{
    if (config) {
        if (config->src_info && (config->src_info != esp_gmf_default_mixer_src_info)) {
            esp_gmf_oal_free(config->src_info);
        }
        esp_gmf_oal_free(config);
    }
}

static esp_gmf_err_t __mixer_set_mode(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t src_idx = (uint8_t)(*buf);
    return esp_gmf_mixer_set_mode(handle, src_idx, *((esp_ae_mixer_mode_t *)(buf + arg_desc->next->offset)));
}

static esp_gmf_err_t __mixer_set_audio_info(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                            uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, arg_desc, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_args_desc_t *mix_desc = arg_desc;
    uint32_t rate = *((uint32_t *)buf);
    mix_desc = mix_desc->next;
    uint8_t ch = (uint8_t)(*(buf + mix_desc->offset));
    mix_desc = mix_desc->next;
    uint8_t bits = (uint8_t)(*(buf + mix_desc->offset));
    return esp_gmf_mixer_set_audio_info(handle, rate, bits, ch);
}

static esp_gmf_err_t esp_gmf_mixer_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_mixer_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_mixer_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_mixer_t *mixer = (esp_gmf_mixer_t *)self;
    esp_ae_mixer_cfg_t *mixer_info = (esp_ae_mixer_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, mixer_info, {return ESP_GMF_JOB_ERR_FAIL;})
    mixer->bytes_per_sample = (mixer_info->bits_per_sample >> 3) * mixer_info->channel;
    esp_ae_mixer_open(mixer_info, &mixer->mixer_hd);
    ESP_GMF_CHECK(TAG, mixer->mixer_hd, {return ESP_GMF_JOB_ERR_FAIL;}, "Failed to create mixer handle");
    for (int i = 0; i < mixer_info->src_num; i++) {
        esp_ae_mixer_set_mode(mixer->mixer_hd, i, mixer->mode[i]);
    }
    uint32_t process_num = MIXER_GET_FRAME_BYTE_SIZE(mixer_info);
    mixer->process_num = (ESP_GMF_ELEMENT_GET(mixer)->in_attr.data_size == 0) ? (process_num) : (ESP_GMF_ELEMENT_GET(mixer)->in_attr.data_size);
    mixer->frame_time = GMF_AUDIO_CALC_PTS(mixer->process_num, mixer_info->sample_rate, mixer_info->channel, mixer_info->bits_per_sample);
    mixer->src_num = mixer_info->src_num;
    mixer->in_load = esp_gmf_oal_calloc(1, sizeof(esp_gmf_payload_t *) * mixer_info->src_num);
    ESP_GMF_MEM_VERIFY(TAG, mixer->in_load, {return ESP_GMF_JOB_ERR_FAIL;},
                       "in load", sizeof(esp_gmf_payload_t *) * mixer_info->src_num);
    mixer->in_arr = esp_gmf_oal_calloc(1, sizeof(int *) * mixer_info->src_num);
    ESP_GMF_MEM_VERIFY(TAG, mixer->in_arr, {return ESP_GMF_JOB_ERR_FAIL;},
                       "in buffer array", sizeof(int *) * mixer_info->src_num);
    GMF_AUDIO_UPDATE_SND_INFO(self, mixer_info->sample_rate, mixer_info->bits_per_sample, mixer_info->channel);

    mixer->need_reopen = false;
    ESP_LOGD(TAG, "Open, %p", self);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_mixer_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_mixer_t *mixer = (esp_gmf_mixer_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    if (mixer->mixer_hd != NULL) {
        esp_ae_mixer_close(mixer->mixer_hd);
        mixer->mixer_hd = NULL;
    }
    if (mixer->in_arr != NULL) {
        esp_gmf_oal_free(mixer->in_arr);
        mixer->in_arr = NULL;
    }
    if (mixer->in_load != NULL) {
        esp_gmf_oal_free(mixer->in_load);
        mixer->in_load = NULL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_mixer_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_mixer_t *mixer = (esp_gmf_mixer_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (mixer->need_reopen) {
        esp_gmf_mixer_close(self, NULL);
        out_len = esp_gmf_mixer_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "Mixer reopen failed");
            return out_len;
        }
    }
    int read_len = 0;
    esp_gmf_err_io_t ret = ESP_GMF_IO_OK;
    int status_end = 0;
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return ESP_GMF_JOB_ERR_FAIL;}, "Failed to apply mixer setting");
    esp_gmf_port_handle_t in = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t in_port = in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    int index = mixer->src_num;
    memset(mixer->in_load, 0, sizeof(esp_gmf_payload_t *) * index);
    mixer->out_load = NULL;
    int i = 0;
#ifdef MIXER_DEBUG
    uint64_t start = 0;
    uint64_t end = 0;
    uint64_t acquire_in_time = 0;
    uint32_t frame_time = mixer->frame_time;
#endif
    while (in_port != NULL) {
#ifdef MIXER_DEBUG
        start = esp_timer_get_time();
#endif
        int wait_ticks = ((in_port->wait_ticks < (mixer->frame_time / index)) ? (mixer->frame_time / index) : (in_port->wait_ticks));
        ret = esp_gmf_port_acquire_in(in_port, &(mixer->in_load[i]), mixer->process_num, wait_ticks);
#ifdef MIXER_DEBUG
        end = esp_timer_get_time();
        acquire_in_time += ((end - start) / 1000);
        ESP_LOGD(TAG, "Port %d acquire in time: %lld ms, wait ticks: %d", i, (end - start) / 1000, wait_ticks);
#endif
        if (ret == ESP_GMF_IO_FAIL || mixer->in_load[i]->buf == NULL) {
            ESP_LOGE(TAG, "Acquire in failed, idx:%d, ret: %d", i, ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
            goto __mixer_release;
        }
        if (ret == ESP_GMF_IO_TIMEOUT || ret == ESP_GMF_IO_ABORT || mixer->in_load[i]->is_done) {
            status_end++;
        }
        read_len = mixer->in_load[i]->valid_size;
        mixer->in_arr[i] = mixer->in_load[i]->buf;
        if (read_len < mixer->process_num) {
            memset(mixer->in_arr[i] + read_len, 0, mixer->process_num - read_len);
        }
        in_port = in_port->next;
        ESP_LOGV(TAG, "IN: idx: %d load: %p, buf: %p, valid size: %d, buf length: %d, done: %d",
                 i, mixer->in_load[i], mixer->in_load[i]->buf, mixer->in_load[i]->valid_size,
                 mixer->in_load[i]->buf_length, mixer->in_load[i]->is_done);
        i++;
    }
#ifdef MIXER_DEBUG
    if (acquire_in_time > frame_time) {
        ESP_LOGW(TAG, "Total acquire in time: %lld ms, frame time: %ld ms", acquire_in_time, frame_time);
    }
#endif
    ret = esp_gmf_port_acquire_out(out_port, &mixer->out_load, mixer->process_num, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, ret, out_len, { goto __mixer_release;});
    // Down-mixer never stop in gmf, only user can set to stop
    if (status_end == index) {
        memset(mixer->out_load->buf, 0, mixer->process_num);
        out_len = ESP_GMF_JOB_ERR_OK;
        goto __mixer_release;
    }
    esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)self)->lock);
    esp_ae_err_t porc_ret = esp_ae_mixer_process(mixer->mixer_hd, mixer->process_num / mixer->bytes_per_sample,
                                                 (void *)mixer->in_arr, mixer->out_load->buf);
    esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)self)->lock);
    if (porc_ret != ESP_AE_ERR_OK) {
        ESP_LOGE(TAG, "Mix process error %d.", porc_ret);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    ESP_LOGV(TAG, "OUT: load: %p, buf: %p, valid size: %d, buf length: %d",
             mixer->out_load, mixer->out_load->buf, mixer->out_load->valid_size, mixer->out_load->buf_length);
    mixer->out_load->valid_size = mixer->process_num;
    if (mixer->out_load->valid_size > 0) {
        esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, mixer->out_load->valid_size);
    }
__mixer_release:
    // Release in and out port
    if (mixer->out_load != NULL) {
        ret = esp_gmf_port_release_out(out_port, mixer->out_load, ESP_GMF_MAX_DELAY);
        if ((ret < ESP_GMF_IO_OK) && (ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "OUT port release error, ret:%d", ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    in_port = in;
    i = 0;
    while (in_port != NULL && mixer->in_load[i] != NULL) {
        ret = esp_gmf_port_release_in(in_port, mixer->in_load[i], ESP_GMF_MAX_DELAY);
        if ((ret < ESP_GMF_IO_OK) && (ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "IN port release error, ret:%d", ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
        in_port = in_port->next;
        i++;
    }
    return out_len;
}

static esp_gmf_err_t mixer_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_ae_mixer_cfg_t *config = (esp_ae_mixer_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_mixer_t *mixer = (esp_gmf_mixer_t *)self;
    mixer->need_reopen = (config->sample_rate != info->sample_rates) || (info->channels != config->channel) || (config->bits_per_sample != info->bits);
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

static esp_gmf_err_t esp_gmf_mixer_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_mixer_t *mixer = (esp_gmf_mixer_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    free_esp_ae_mixer_cfg(OBJ_GET_CFG(self));
    if (mixer->mode) {
        esp_gmf_oal_free(mixer->mode);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(mixer);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_mixer_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_MIXER;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_mixer_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_err_t ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MIXER, SET_INFO, RATE), ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append RATE argument");
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MIXER, SET_INFO, CH), ESP_GMF_ARGS_TYPE_UINT8,
                                   sizeof(uint8_t), sizeof(uint32_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append CHANNEL argument");
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MIXER, SET_INFO, BITS), ESP_GMF_ARGS_TYPE_UINT8,
                                   sizeof(uint8_t), sizeof(uint8_t) + sizeof(uint32_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append BITS argument");
    ret = esp_gmf_method_append(&method, AMETHOD(MIXER, SET_INFO), __mixer_set_audio_info, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(MIXER, SET_INFO));

    set_args = NULL;
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MIXER, SET_MODE, IDX), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append INDEX argument");
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(MIXER, SET_MODE, MODE), ESP_GMF_ARGS_TYPE_INT32,
                                   sizeof(int32_t), sizeof(uint8_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to append MODE argument");
    ret = esp_gmf_method_append(&method, AMETHOD(MIXER, SET_MODE), __mixer_set_mode, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {return ret;}, "Failed to register %s method", AMETHOD(MIXER, SET_MODE));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mixer_set_mode(esp_gmf_element_handle_t handle, uint8_t src_idx, esp_ae_mixer_mode_t mode)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_mixer_t *mixer = (esp_gmf_mixer_t *)handle;
    esp_ae_mixer_cfg_t *cfg = (esp_ae_mixer_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    if (src_idx >= cfg->src_num) {
        ESP_LOGE(TAG, "Source index %d overlimit %d hd:%p", src_idx, cfg->src_num, mixer);
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (mixer->mixer_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_ae_err_t ret = esp_ae_mixer_set_mode(mixer->mixer_hd, src_idx, mode);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_JOB_ERR_FAIL;, "mixerualize set error %d", ret);
    }
    mixer->mode[src_idx] = mode;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mixer_set_audio_info(esp_gmf_element_handle_t handle, uint32_t sample_rate,
                                           uint8_t bits, uint8_t channel)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_ae_mixer_cfg_t *cfg = (esp_ae_mixer_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    if (cfg->sample_rate == sample_rate && cfg->bits_per_sample == bits && cfg->channel == channel) {
        return ESP_GMF_ERR_OK;
    }
    cfg->sample_rate = sample_rate;
    cfg->bits_per_sample = bits;
    cfg->channel = channel;
    esp_gmf_mixer_t *mixer = (esp_gmf_mixer_t *)handle;
    mixer->need_reopen = true;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_mixer_init(esp_ae_mixer_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_mixer_t *mixer = esp_gmf_oal_calloc(1, sizeof(esp_gmf_mixer_t));
    ESP_GMF_MEM_VERIFY(TAG, mixer, {return ESP_GMF_ERR_MEMORY_LACK;}, "mixer", sizeof(esp_gmf_mixer_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)mixer;
    obj->new_obj = esp_gmf_mixer_new;
    obj->del_obj = esp_gmf_mixer_destroy;
    esp_ae_mixer_cfg_t *cfg = NULL;
    if (config) {
        if (config->src_info == NULL) {
            config->src_info = (esp_ae_mixer_info_t *)esp_gmf_default_mixer_src_info;
            config->src_num = sizeof(esp_gmf_default_mixer_src_info) / sizeof(esp_ae_mixer_info_t);
        }
        dupl_esp_ae_mixer_cfg(config, &cfg);
    } else {
        esp_ae_mixer_cfg_t dcfg = DEFAULT_ESP_GMF_MIXER_CONFIG();
        dcfg.src_info = (esp_ae_mixer_info_t *)esp_gmf_default_mixer_src_info;
        dcfg.src_num = sizeof(esp_gmf_default_mixer_src_info) / sizeof(esp_ae_mixer_info_t);
        dupl_esp_ae_mixer_cfg(&dcfg, &cfg);
    }
    ESP_GMF_CHECK(TAG, cfg, ret = ESP_GMF_ERR_MEMORY_LACK; goto MIXER_INIT_FAIL;, "Failed to allocate mixer configuration");
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_ae_mixer_cfg_t));
    mixer->mode = esp_gmf_oal_calloc(cfg->src_num, sizeof(esp_ae_mixer_mode_t));
    ESP_GMF_MEM_VERIFY(TAG, mixer->mode, {ret = ESP_GMF_ERR_MEMORY_LACK; goto MIXER_INIT_FAIL;}, "Allocate(%d) failed", cfg->src_num * sizeof(esp_ae_mixer_mode_t));
    for (int i = 0; i < cfg->src_num; i++) {
        mixer->mode[i] = ESP_AE_MIXER_MODE_FADE_UPWARD;
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_mixer");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto MIXER_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    mixer->process_num = MIXER_GET_FRAME_BYTE_SIZE(cfg);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_MULTI, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, mixer->process_num);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, mixer->process_num);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(mixer, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto MIXER_INIT_FAIL, "Failed to initialize mixer element");
    ESP_GMF_ELEMENT_GET(mixer)->ops.open = esp_gmf_mixer_open;
    ESP_GMF_ELEMENT_GET(mixer)->ops.process = esp_gmf_mixer_process;
    ESP_GMF_ELEMENT_GET(mixer)->ops.close = esp_gmf_mixer_close;
    ESP_GMF_ELEMENT_GET(mixer)->ops.event_receiver = mixer_received_event_handler;
    ESP_GMF_ELEMENT_GET(mixer)->ops.load_caps = _load_mixer_caps_func;
    ESP_GMF_ELEMENT_GET(mixer)->ops.load_methods = _load_mixer_methods_func;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
MIXER_INIT_FAIL:
    esp_gmf_mixer_destroy(obj);
    return ret;
}
