/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_asrc.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_node.h"
#include "esp_gmf_asrc.h"
#include "gmf_audio_common.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"

/**
 * @brief  Audio ASRC context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t  parent;            /*!< The GMF ASRC handle */
    esp_asrc_handle_t        asrc_hd;           /*!< The ASRC handle */
    uint16_t                 in_sample_bytes;   /*!< Bytes per input sample frame */
    uint16_t                 out_sample_bytes;  /*!< Bytes per output sample frame */
    bool                     need_reopen : 1;   /*!< Whether need to reopen */
    bool                     bypass      : 1;   /*!< Whether bypass */
} esp_gmf_asrc_t;

static const char *TAG = "ESP_GMF_ASRC";

static inline bool esp_gmf_asrc_is_bypass(const esp_asrc_cfg_t *cfg)
{
    return (cfg->src_info.sample_rate == cfg->dest_info.sample_rate)
        && (cfg->src_info.channel == cfg->dest_info.channel)
        && (cfg->src_info.bits_per_sample == cfg->dest_info.bits_per_sample);
}

static inline void esp_gmf_asrc_update_bypass(esp_gmf_asrc_t *asrc, esp_asrc_cfg_t *cfg)
{
    asrc->bypass = esp_gmf_asrc_is_bypass(cfg);
}

static esp_gmf_err_t __asrc_set_dest_rate(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                          uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint32_t dest_rate = *((uint32_t *)buf);
    return esp_gmf_asrc_set_dest_rate(handle, dest_rate);
}

static esp_gmf_err_t __asrc_set_dest_bits(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                          uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t dest_bits = *((uint8_t *)buf);
    return esp_gmf_asrc_set_dest_bits(handle, dest_bits);
}

static esp_gmf_err_t __asrc_set_dest_ch(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                        uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, buf, {return ESP_GMF_ERR_INVALID_ARG;});
    uint8_t dest_ch = *((uint8_t *)buf);
    return esp_gmf_asrc_set_dest_ch(handle, dest_ch);
}

static esp_gmf_err_t esp_gmf_asrc_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_asrc_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_asrc_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_asrc_t *asrc = (esp_gmf_asrc_t *)self;
    esp_asrc_cfg_t *asrc_info = (esp_asrc_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, asrc_info, {return ESP_GMF_JOB_ERR_FAIL;});
    esp_gmf_job_err_t job_ret = ESP_GMF_JOB_ERR_OK;
    esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)self)->lock);
    esp_asrc_err_t ret = esp_asrc_open(asrc_info, &asrc->asrc_hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {job_ret = ESP_GMF_JOB_ERR_FAIL; goto __asrc_open_exit;}, "Failed to create ASRC handle %d", ret);
    ret = esp_asrc_get_bytes_per_sample(asrc->asrc_hd, &asrc->in_sample_bytes, &asrc->out_sample_bytes);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {job_ret = ESP_GMF_JOB_ERR_FAIL; goto __asrc_open_exit;}, "Failed to get ASRC bytes per sample %d", ret);
    ESP_LOGD(TAG, "Open, src: %" PRIu32 ", dest: %" PRIu32 ", ch: %d->%d, bits: %d->%d",
             asrc_info->src_info.sample_rate, asrc_info->dest_info.sample_rate,
             asrc_info->src_info.channel, asrc_info->dest_info.channel,
             asrc_info->src_info.bits_per_sample, asrc_info->dest_info.bits_per_sample);
    asrc->need_reopen = false;
    esp_gmf_asrc_update_bypass(asrc, asrc_info);
__asrc_open_exit:
    esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)self)->lock);
    if (job_ret != ESP_GMF_JOB_ERR_OK) {
        return job_ret;
    }
    GMF_AUDIO_UPDATE_SND_INFO(self, asrc_info->dest_info.sample_rate, asrc_info->dest_info.bits_per_sample,
                              asrc_info->dest_info.channel);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_asrc_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_asrc_t *asrc = (esp_gmf_asrc_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)self)->lock);
    if (asrc->asrc_hd != NULL) {
        esp_asrc_close(asrc->asrc_hd);
        asrc->asrc_hd = NULL;
    }
    esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)self)->lock);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_asrc_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_asrc_t *asrc = (esp_gmf_asrc_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (asrc->need_reopen) {
        esp_gmf_asrc_close(self, NULL);
        out_len = esp_gmf_asrc_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "ASRC reopen failed");
            return out_len;
        }
    }
    esp_asrc_err_t ret = ESP_ASRC_ERR_OK;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int samples_num = ESP_GMF_ELEMENT_GET(asrc)->in_attr.data_size / asrc->in_sample_bytes;
    int bytes = samples_num * asrc->in_sample_bytes;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    samples_num = in_load->valid_size / asrc->in_sample_bytes;
    bytes = samples_num * asrc->in_sample_bytes;
    if ((bytes != in_load->valid_size) || (load_ret < ESP_GMF_IO_OK)) {
        if (load_ret == ESP_GMF_IO_ABORT) {
            out_len = ESP_GMF_JOB_ERR_ABORT;
            goto __asrc_release;
        }
        ESP_LOGE(TAG, "Invalid in load size %d, ret %d", in_load->valid_size, load_ret);
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __asrc_release;
    }
    uint32_t out_samples_num = 0;
    if (samples_num) {
        ret = esp_asrc_get_out_sample_num(asrc->asrc_hd, samples_num, &out_samples_num);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __asrc_release;}, "Failed to get ASRC out size, ret: %d", ret);
    }
    int acq_out_size = out_samples_num == 0 ? in_load->buf_length : out_samples_num * asrc->out_sample_bytes;
    if (asrc->bypass && (in_port->is_shared == true)) {
        out_load = in_load;
    }
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, acq_out_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, {goto __asrc_release;});
    if (samples_num) {
        out_samples_num = out_load->buf_length / asrc->out_sample_bytes;
        ret = esp_asrc_process(asrc->asrc_hd, in_load->buf, samples_num, out_load->buf, &out_samples_num);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __asrc_release;}, "ASRC process error %d", ret);
    }
    out_load->valid_size = out_samples_num * asrc->out_sample_bytes;
    out_load->pts = in_load->pts;
    out_load->is_done = in_load->is_done;
    ESP_LOGV(TAG, "Out Samples: %ld, IN-PLD: %p-%p-%d-%d-%d, OUT-PLD: %p-%p-%d-%d-%d", out_samples_num, in_load, in_load->buf,
             in_load->valid_size, in_load->buf_length, in_load->is_done, out_load,
             out_load->buf, out_load->valid_size, out_load->buf_length, out_load->is_done);
    esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, out_load->valid_size);
    if (in_load->is_done) {
        out_len = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "ASRC done, out len: %d", out_load->valid_size);
    }
__asrc_release:
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

static esp_gmf_err_t asrc_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_asrc_cfg_t *config = (esp_asrc_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_asrc_t *asrc = (esp_gmf_asrc_t *)self;
    asrc->need_reopen = (config->src_info.sample_rate != info->sample_rates)
        || (config->src_info.channel != info->channels)
        || (config->src_info.bits_per_sample != info->bits);
    config->src_info.sample_rate = info->sample_rates;
    config->src_info.channel = info->channels;
    config->src_info.bits_per_sample = info->bits;
    ESP_LOGD(TAG, "RECV element info, from: %s-%p, next: %p, self: %s-%p, type: %x, state: %s, rate: %d, ch: %d, bits: %d",
             OBJ_GET_TAG(el), el, esp_gmf_node_for_next((esp_gmf_node_t *)el), OBJ_GET_TAG(self), self, evt->type,
             esp_gmf_event_get_state_str(state), info->sample_rates, info->channels, info->bits);
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_asrc_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_asrc_t *asrc = (esp_gmf_asrc_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(asrc);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_asrc_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t cap = {0};
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    do {
        cap.cap_eightcc = ESP_GMF_CAPS_AUDIO_RATE_CONVERT;
        cap.attr_fun = NULL;
        ret = esp_gmf_cap_append(&caps, &cap);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, break, "Failed to append rate convert capability");

        cap.cap_eightcc = ESP_GMF_CAPS_AUDIO_CHANNEL_CONVERT;
        ret = esp_gmf_cap_append(&caps, &cap);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, break, "Failed to append channel convert capability");

        cap.cap_eightcc = ESP_GMF_CAPS_AUDIO_BIT_CONVERT;
        ret = esp_gmf_cap_append(&caps, &cap);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, break, "Failed to append bit convert capability");

        esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
        el->caps = caps;
        return ret;
    } while (0);
    if (caps) {
        esp_gmf_cap_destroy(caps);
    }
    return ret;
}

static esp_gmf_err_t _load_asrc_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    // Set destination rate method
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(RATE_CVT, SET_DEST_RATE, RATE),
                                   ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append RATE argument");
    ret = esp_gmf_method_append(&method, AMETHOD(RATE_CVT, SET_DEST_RATE), __asrc_set_dest_rate, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register %s method", AMETHOD(RATE_CVT, SET_DEST_RATE));

    // Set destination channel method
    set_args = NULL;
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(CH_CVT, SET_DEST_CH, CH),
                                   ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append CHANNEL argument");
    ret = esp_gmf_method_append(&method, AMETHOD(CH_CVT, SET_DEST_CH), __asrc_set_dest_ch, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register %s method", AMETHOD(CH_CVT, SET_DEST_CH));

    // Set destination bits method
    set_args = NULL;
    ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(BIT_CVT, SET_DEST_BITS, BITS),
                                   ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append BITS argument");
    ret = esp_gmf_method_append(&method, AMETHOD(BIT_CVT, SET_DEST_BITS), __asrc_set_dest_bits, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register %s method", AMETHOD(BIT_CVT, SET_DEST_BITS));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_asrc_set_dest_field(esp_gmf_element_handle_t handle, bool update_rate, uint32_t dest_rate,
                                                 bool update_ch, uint8_t dest_ch, bool update_bits, uint8_t dest_bits)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    esp_asrc_cfg_t *cfg = (esp_asrc_cfg_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_asrc_t *asrc = (esp_gmf_asrc_t *)handle;
    esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
    if (update_rate && (cfg->dest_info.sample_rate != dest_rate)) {
        cfg->dest_info.sample_rate = dest_rate;
        asrc->need_reopen = true;
    }
    if (update_ch && (cfg->dest_info.channel != dest_ch)) {
        cfg->dest_info.channel = dest_ch;
        asrc->need_reopen = true;
    }
    if (update_bits && (cfg->dest_info.bits_per_sample != dest_bits)) {
        cfg->dest_info.bits_per_sample = dest_bits;
        asrc->need_reopen = true;
    }
    esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
    return ret;
}

esp_gmf_err_t esp_gmf_asrc_set_dest_rate(esp_gmf_element_handle_t handle, uint32_t dest_rate)
{
    return esp_gmf_asrc_set_dest_field(handle, true, dest_rate, false, 0, false, 0);
}

esp_gmf_err_t esp_gmf_asrc_set_dest_ch(esp_gmf_element_handle_t handle, uint8_t dest_ch)
{
    return esp_gmf_asrc_set_dest_field(handle, false, 0, true, dest_ch, false, 0);
}

esp_gmf_err_t esp_gmf_asrc_set_dest_bits(esp_gmf_element_handle_t handle, uint8_t dest_bits)
{
    return esp_gmf_asrc_set_dest_field(handle, false, 0, false, 0, true, dest_bits);
}

esp_gmf_err_t esp_gmf_asrc_init(esp_asrc_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_asrc_t *asrc = esp_gmf_oal_calloc(1, sizeof(esp_gmf_asrc_t));
    ESP_GMF_MEM_VERIFY(TAG, asrc, {return ESP_GMF_ERR_MEMORY_LACK;}, "ASRC", sizeof(esp_gmf_asrc_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)asrc;
    obj->new_obj = esp_gmf_asrc_new;
    obj->del_obj = esp_gmf_asrc_destroy;
    esp_asrc_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(esp_asrc_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto ASRC_INIT_FAIL;}, "ASRC configuration", sizeof(esp_asrc_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_asrc_cfg_t));
    if (config) {
        memcpy(cfg, config, sizeof(esp_asrc_cfg_t));
    } else {
        esp_asrc_cfg_t dcfg = DEFAULT_ESP_GMF_ASRC_CONFIG();
        memcpy(cfg, &dcfg, sizeof(esp_asrc_cfg_t));
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_asrc");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ASRC_INIT_FAIL, "Failed to set obj tag");
    esp_asrc_buffer_alignment_t buffer_alignment = {0};
    esp_asrc_err_t asrc_ret = esp_asrc_get_buffer_alignment(&buffer_alignment);
    ESP_GMF_RET_ON_ERROR(TAG, asrc_ret, {ret = ESP_GMF_ERR_FAIL; goto ASRC_INIT_FAIL;}, "Failed to get ASRC buffer alignment");

    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE,
                                     buffer_alignment.inbuf_addr_align, buffer_alignment.inbuf_size_align,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE,
                                      buffer_alignment.outbuf_addr_align, buffer_alignment.outbuf_size_align,
                                      ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(asrc, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ASRC_INIT_FAIL, "Failed to initialize ASRC element");
    ESP_GMF_ELEMENT_GET(asrc)->ops.open = esp_gmf_asrc_open;
    ESP_GMF_ELEMENT_GET(asrc)->ops.process = esp_gmf_asrc_process;
    ESP_GMF_ELEMENT_GET(asrc)->ops.close = esp_gmf_asrc_close;
    ESP_GMF_ELEMENT_GET(asrc)->ops.event_receiver = asrc_received_event_handler;
    ESP_GMF_ELEMENT_GET(asrc)->ops.load_caps = _load_asrc_caps_func;
    ESP_GMF_ELEMENT_GET(asrc)->ops.load_methods = _load_asrc_methods_func;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
ASRC_INIT_FAIL:
    esp_gmf_asrc_destroy(obj);
    return ret;
}
