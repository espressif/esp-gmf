/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2025 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD.>
 *
 * Permission is hereby granted for use on all ESPRESSIF SYSTEMS products, in which case,
 * it is free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished
 * to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or
 * substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_node.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_audio_element.h"
#include "esp_gmf_audio_enc.h"
#include "esp_audio_enc_default.h"
#include "esp_gmf_cache.h"

#define AUD_ENC_DEFAULT_INPUT_TIME_MS (20)
#define SET_ENC_BASIC_INFO(cfg, info) do {          \
    (cfg)->sample_rate     = (info)->sample_rates;  \
    (cfg)->channel         = (info)->channels;      \
    (cfg)->bits_per_sample = (info)->bits;          \
} while (0)

/**
 * @brief Audio encoder context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t parent;          /*!< The GMF audio encoder handle */
    esp_audio_enc_handle_t  audio_enc_hd;    /*!< The audio encoder handle */
    esp_gmf_cache_t        *cached_payload;  /*!< A Cached payload for data concatenation */
} esp_gmf_audio_enc_t;

static const char *TAG = "ESP_GMF_AENC";

static inline esp_gmf_err_t dupl_esp_gmf_audio_enc_cfg(esp_audio_enc_config_t *config, esp_audio_enc_config_t **new_config)
{
    void *sub_cfg = NULL;
    *new_config = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, *new_config, {return ESP_GMF_ERR_MEMORY_LACK;}, "audio encoder handle configuration", sizeof(*config));
    memcpy(*new_config, config, sizeof(*config));
    if (config->cfg && (config->cfg_sz > 0)) {
        sub_cfg = esp_gmf_oal_calloc(1, config->cfg_sz);
        ESP_GMF_MEM_VERIFY(TAG, sub_cfg, {esp_gmf_oal_free(*new_config); return ESP_GMF_ERR_MEMORY_LACK;},
                           "audio encoder configuration", (int)config->cfg_sz);
        memcpy(sub_cfg, config->cfg, config->cfg_sz);
        (*new_config)->cfg = sub_cfg;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static inline void free_esp_gmf_audio_enc_cfg(esp_audio_enc_config_t *config)
{
    if (config) {
        if (config->cfg) {
            esp_gmf_oal_free(config->cfg);
            config->cfg = NULL;
            config->cfg_sz = 0;
        }
        esp_gmf_oal_free(config);
    }
}

static inline void audio_enc_change_audio_info(esp_gmf_audio_element_handle_t self, esp_gmf_info_sound_t *info)
{
    esp_audio_enc_config_t *enc_cfg = (esp_audio_enc_config_t *)OBJ_GET_CFG(self);
    switch (enc_cfg->type) {
        case ESP_AUDIO_TYPE_AAC: {
            esp_aac_enc_config_t *aac_enc_cfg = (esp_aac_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(aac_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_AMRNB: {
            esp_amrnb_enc_config_t *amr_enc_cfg = (esp_amrnb_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(amr_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_G711U:
        case ESP_AUDIO_TYPE_G711A: {
            esp_g711_enc_config_t *g711_enc_cfg = (esp_g711_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(g711_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_AMRWB: {
            esp_amrnb_enc_config_t *amr_enc_cfg = (esp_amrnb_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(amr_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_ALAC: {
            esp_alac_enc_config_t *alac_enc_cfg = (esp_alac_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(alac_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_PCM: {
            esp_pcm_enc_config_t *pcm_enc_cfg = (esp_pcm_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(pcm_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_OPUS: {
            esp_opus_enc_config_t *opus_enc_cfg = (esp_opus_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(opus_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_ADPCM: {
            esp_adpcm_enc_config_t *adpcm_enc_cfg = (esp_adpcm_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(adpcm_enc_cfg, info);
            break;
        }
        default:
            break;
    }
}

static uint32_t audio_enc_get_rate(esp_audio_enc_config_t *enc_cfg)
{
    uint32_t sample_rate = 0;
    if (enc_cfg->type == ESP_AUDIO_TYPE_PCM) {
        esp_pcm_enc_config_t *pcm_enc_cfg = (esp_pcm_enc_config_t *)enc_cfg->cfg;
        sample_rate = pcm_enc_cfg->sample_rate;
    } else {
        esp_g711_enc_config_t *g711_enc_cfg = (esp_g711_enc_config_t *)enc_cfg->cfg;
        sample_rate = g711_enc_cfg->sample_rate;
    }
    return sample_rate;
}

static esp_gmf_err_t esp_gmf_audio_enc_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    *handle = NULL;
    esp_audio_enc_config_t *enc_cfg = (esp_audio_enc_config_t *)cfg;
    esp_gmf_obj_handle_t new_obj = NULL;
    esp_gmf_err_t ret = esp_gmf_audio_enc_init(enc_cfg, &new_obj);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    ret = esp_gmf_audio_enc_cast(enc_cfg, new_obj);
    if (ret != ESP_GMF_ERR_OK) {
        esp_gmf_obj_delete(new_obj);
        return ret;
    }
    *handle = (void *)new_obj;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_audio_enc_open(esp_gmf_audio_element_handle_t self, void *para)
{
    esp_gmf_audio_enc_t *enc = (esp_gmf_audio_enc_t *)self;
    esp_audio_enc_config_t *enc_cfg = (esp_audio_enc_config_t *)OBJ_GET_CFG(enc);
    ESP_GMF_CHECK(TAG, enc_cfg, {return ESP_GMF_JOB_ERR_FAIL;}, "There is no encoder configuration");
    esp_audio_err_t ret = esp_audio_enc_open(enc_cfg, &enc->audio_enc_hd);
    ESP_GMF_CHECK(TAG, enc->audio_enc_hd, {return ESP_GMF_JOB_ERR_FAIL;}, "Failed to create audio encoder handle");
    if (esp_audio_enc_get_frame_size(enc->audio_enc_hd, &ESP_GMF_ELEMENT_GET(enc)->in_attr.data_size, &ESP_GMF_ELEMENT_GET(enc)->out_attr.data_size) != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Failed to obtain frame size, ret: %d", ret);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    if (enc_cfg->type == ESP_AUDIO_TYPE_PCM || enc_cfg->type == ESP_AUDIO_TYPE_G711A || enc_cfg->type == ESP_AUDIO_TYPE_G711U) {
        ESP_GMF_ELEMENT_GET(enc)->in_attr.data_size = ESP_GMF_ELEMENT_GET(enc)->in_attr.data_size * audio_enc_get_rate(enc_cfg) * AUD_ENC_DEFAULT_INPUT_TIME_MS / 1000;
        ESP_GMF_ELEMENT_GET(enc)->out_attr.data_size = ESP_GMF_ELEMENT_GET(enc)->in_attr.data_size;
    }
    esp_gmf_port_enable_payload_share(ESP_GMF_ELEMENT_GET(self)->in, false);
    esp_gmf_cache_new(ESP_GMF_ELEMENT_GET(enc)->in_attr.data_size, &enc->cached_payload);
    ESP_GMF_CHECK(TAG, enc->cached_payload, {return ESP_GMF_JOB_ERR_FAIL;}, "Failed to new a cached payload on open");
    ESP_LOGI(TAG, "Open, type:%d, acquire in frame: %d, out frame: %d", enc_cfg->type, ESP_GMF_ELEMENT_GET(enc)->in_attr.data_size, ESP_GMF_ELEMENT_GET(enc)->out_attr.data_size);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_audio_enc_process(esp_gmf_audio_element_handle_t self, void *para)
{
    esp_gmf_audio_enc_t *audio_enc = (esp_gmf_audio_enc_t *)self;
    int out_len = 0;
    esp_audio_err_t ret = ESP_AUDIO_ERR_OK;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *origin_in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    esp_audio_enc_in_frame_t enc_in_frame = {0};
    esp_audio_enc_out_frame_t enc_out_frame = {0};
    esp_gmf_err_io_t load_ret = 0;
    bool needed_load = false;
    esp_gmf_payload_t *in_load = NULL;

    esp_gmf_cache_ready_for_load(audio_enc->cached_payload, &needed_load);
    if (needed_load) {
        load_ret = esp_gmf_port_acquire_in(in_port, &origin_in_load, ESP_GMF_ELEMENT_GET(audio_enc)->in_attr.data_size, in_port->wait_ticks);
        ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, load_ret, out_len, {goto __audio_enc_release;});
        esp_gmf_cache_load(audio_enc->cached_payload, origin_in_load);
    }
    esp_gmf_cache_acquire(audio_enc->cached_payload, ESP_GMF_ELEMENT_GET(audio_enc)->in_attr.data_size, &in_load);
    ESP_LOGD(TAG, "Acq cache, buf:%p, vld:%d, len:%d, done:%d", in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done);
    if ((in_load->valid_size != ESP_GMF_ELEMENT_GET(audio_enc)->in_attr.data_size) && (in_load->is_done != true)) {
        out_len = ESP_GMF_JOB_ERR_CONTINUE;
        ESP_LOGD(TAG, "Return Continue, size:%d", in_load->valid_size);
        goto __audio_enc_release;
    }
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, ESP_GMF_ELEMENT_GET(audio_enc)->out_attr.data_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, {goto __audio_enc_release;});
    if (out_load->buf_length < (ESP_GMF_ELEMENT_GET(audio_enc)->out_attr.data_size)) {
        ESP_LOGE(TAG, "The out payload valid size(%d) is smaller than wanted size(%d)",
                    out_load->buf_length, (ESP_GMF_ELEMENT_GET(audio_enc)->out_attr.data_size));
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __audio_enc_release;
    }

    enc_in_frame.buffer = in_load->buf;
    enc_in_frame.len = in_load->valid_size;
    enc_out_frame.buffer = out_load->buf;
    enc_out_frame.len = ESP_GMF_ELEMENT_GET(audio_enc)->out_attr.data_size;
    ret = esp_audio_enc_process(audio_enc->audio_enc_hd, &enc_in_frame, &enc_out_frame);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __audio_enc_release;}, "Audio encoder process error %d", ret);
    out_load->valid_size = enc_out_frame.encoded_bytes;
    out_len = out_load->valid_size;
    out_load->is_done = in_load->is_done;

    if (in_load->is_done) {
        ESP_LOGW(TAG, "Got done, out size: %d", out_load->valid_size);
        out_len = ESP_GMF_JOB_ERR_DONE;
    }
    esp_gmf_cache_ready_for_load(audio_enc->cached_payload, &needed_load);
    if (needed_load == false) {
        out_len = ESP_GMF_JOB_ERR_TRUNCATE;
        int cached_size = 0;
        esp_gmf_cache_get_cached_size(audio_enc->cached_payload, &cached_size);
        ESP_LOGD(TAG, "Return TRUNCATE, reminder in size: %d", cached_size);
    }

__audio_enc_release:
    esp_gmf_cache_release(audio_enc->cached_payload, in_load);
    if ((origin_in_load != NULL) && (out_len != ESP_GMF_JOB_ERR_TRUNCATE)) {
        load_ret = esp_gmf_port_release_in(in_port, origin_in_load, in_port->wait_ticks);
        ESP_GMF_PORT_RELEASE_OUT_CHECK(TAG, load_ret, out_len, NULL);
    }
    if (out_load != NULL) {
        load_ret = esp_gmf_port_release_out(out_port, out_load, out_port->wait_ticks);
        ESP_GMF_PORT_RELEASE_OUT_CHECK(TAG, load_ret, out_len, NULL);
    }
    return out_len;
}

static esp_gmf_job_err_t esp_gmf_audio_enc_close(esp_gmf_audio_element_handle_t self, void *para)
{
    ESP_LOGD(TAG, "Closed, %p", self);
    esp_gmf_audio_enc_t *enc = (esp_gmf_audio_enc_t *)self;
    if (enc->cached_payload) {
        esp_gmf_cache_delete(enc->cached_payload);
        enc->cached_payload = NULL;
    }
    if (enc->audio_enc_hd != NULL) {
        esp_audio_enc_close(enc->audio_enc_hd);
        enc->audio_enc_hd = NULL;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_err_t audio_enc_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, ctx, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, evt, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t)ctx;
    esp_gmf_element_handle_t el = evt->from;
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_NONE;
    esp_gmf_element_get_state(self, &state);
    esp_gmf_element_handle_t prev = NULL;
    esp_gmf_element_get_prev_el(self, &prev);
    if ((state == ESP_GMF_EVENT_STATE_NONE) || (prev == el)) {
        if (evt->sub == ESP_GMF_INFO_SOUND) {
            esp_gmf_info_sound_t info = {0};
            memcpy(&info, evt->payload, evt->payload_size);
            audio_enc_change_audio_info(self, &info);
            ESP_LOGD(TAG, "RECV info, from: %s-%p, next: %p, self: %s-%p, type: %x, state: %s, rate: %d, ch: %d, bits: %d",
                     OBJ_GET_TAG(el), el, esp_gmf_node_for_next((esp_gmf_node_t *)el), OBJ_GET_TAG(self), self, evt->type,
                     esp_gmf_event_get_state_str(state), info.sample_rates, info.channels, info.bits);
            esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
        }
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_audio_enc_destroy(esp_gmf_audio_element_handle_t self)
{
    ESP_LOGD(TAG, "Destroyed, %p", self);
    esp_gmf_audio_enc_t *enc = (esp_gmf_audio_enc_t *)self;
    free_esp_gmf_audio_enc_cfg(OBJ_GET_CFG(self));
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(enc);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_audio_enc_init(esp_audio_enc_config_t *config, esp_gmf_obj_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_audio_enc_t *audio_enc = esp_gmf_oal_calloc(1, sizeof(esp_gmf_audio_enc_t));
    ESP_GMF_MEM_VERIFY(TAG, audio_enc, {return ESP_GMF_ERR_MEMORY_LACK;}, "audio encoder", sizeof(esp_gmf_audio_enc_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)audio_enc;
    obj->new_obj = esp_gmf_audio_enc_new;
    obj->del_obj = esp_gmf_audio_enc_destroy;
    if (config) {
        esp_audio_enc_config_t *new_config = NULL;
        dupl_esp_gmf_audio_enc_cfg(config, &new_config);
        ESP_GMF_CHECK(TAG, new_config, {ret = ESP_GMF_ERR_MEMORY_LACK; goto ES_ENC_FAIL;}, "Failed to duplicate audio encoder configuration");
        esp_gmf_obj_set_config(obj, new_config, sizeof(*config));
    }
    ret = esp_gmf_obj_set_tag(obj, "encoder");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ES_ENC_FAIL, "Failed to set obj tag");
    *handle = obj;
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(audio_enc, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ES_ENC_FAIL, "Failed to initialize audio encoder element");
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ret;
ES_ENC_FAIL:
    esp_gmf_obj_delete(obj);
    return ret;
}

esp_gmf_err_t esp_gmf_audio_enc_cast(esp_audio_enc_config_t *config, esp_gmf_obj_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, config, {return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_enc_config_t *new_config = NULL;
    dupl_esp_gmf_audio_enc_cfg(config, &new_config);
    ESP_GMF_CHECK(TAG, new_config, {return ESP_GMF_ERR_MEMORY_LACK;}, "Failed to duplicate audio encoder configuration");
    // Free memory before overwriting
    free_esp_gmf_audio_enc_cfg(OBJ_GET_CFG(handle));
    esp_gmf_obj_set_config(handle, new_config, sizeof(*config));
    esp_gmf_audio_element_t *audio_enc_el = (esp_gmf_audio_element_t *)handle;
    audio_enc_el->base.ops.open = esp_gmf_audio_enc_open;
    audio_enc_el->base.ops.process = esp_gmf_audio_enc_process;
    audio_enc_el->base.ops.close = esp_gmf_audio_enc_close;
    audio_enc_el->base.ops.event_receiver = audio_enc_received_event_handler;
    return ESP_GMF_ERR_OK;
}
