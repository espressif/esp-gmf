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
#include "esp_gmf_interleave.h"
#include "esp_ae_data_weaver.h"
#include "esp_heap_caps.h"
#include "gmf_audio_common.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_element.h"

#define ESP_GMF_PROCESS_SAMPLE (256)

/**
 * @brief Audio interleave context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t parent;           /*!< The GMF interleave handle */
    uint8_t                 bytes_per_sample; /*!< Bytes number of per sampling point */
    esp_gmf_payload_t     **in_load;          /*!< The array of input payload */
    esp_gmf_payload_t      *out_load;         /*!< The output payload */
    uint8_t               **in_arr;           /*!< The array of input buffer pointer */
    uint8_t                 src_num;          /*!< The number of input stream source */
    uint8_t                 bits_per_sample;  /*!< Bits number of per sampling point */
    bool                    need_reopen : 1;  /*!< Whether need to reopen.
                                                   True: Execute the close function first, then execute the open function
                                                   False: Do nothing */
} esp_gmf_interleave_t;

static const char *TAG = "ESP_GMF_INTLV";

static inline esp_gmf_err_t dupl_esp_ae_interleave_cfg(esp_gmf_interleave_cfg *config, esp_gmf_interleave_cfg **new_config)
{
    *new_config = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, *new_config, {return ESP_GMF_ERR_MEMORY_LACK;}, "interleave configuration", sizeof(*config));
    memcpy(*new_config, config, sizeof(*config));
    return ESP_GMF_JOB_ERR_OK;
}

static inline void free_esp_ae_interleave_cfg(esp_gmf_interleave_cfg *config)
{
    if (config) {
        esp_gmf_oal_free(config);
    }
}

static esp_gmf_err_t esp_gmf_interleave_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_interleave_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_interleave_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_interleave_t *interleave = (esp_gmf_interleave_t *)self;
    esp_gmf_interleave_cfg *interleave_info = (esp_gmf_interleave_cfg *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, interleave_info, {return ESP_GMF_JOB_ERR_FAIL;})
    interleave->bytes_per_sample = (interleave_info->bits_per_sample >> 3);
    interleave->in_load = esp_gmf_oal_calloc(1, sizeof(esp_gmf_payload_t *) * interleave_info->src_num);
    ESP_GMF_MEM_VERIFY(TAG, interleave->in_load, {return ESP_GMF_JOB_ERR_FAIL;},
                       "in load", sizeof(esp_gmf_payload_t *) * interleave_info->src_num);
    interleave->in_arr = esp_gmf_oal_calloc(1, sizeof(int *) * interleave_info->src_num);
    ESP_GMF_MEM_VERIFY(TAG, interleave->in_arr, {return ESP_GMF_JOB_ERR_FAIL;},
                       "in buffer array", sizeof(int *) * interleave_info->src_num);
    GMF_AUDIO_UPDATE_SND_INFO(self, interleave_info->sample_rate, interleave_info->bits_per_sample, interleave_info->src_num);
    interleave->src_num = interleave_info->src_num;
    interleave->bits_per_sample = interleave_info->bits_per_sample;
    interleave->need_reopen = false;
    ESP_LOGD(TAG, "Open, %p", self);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_interleave_close(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_interleave_t *interleave = (esp_gmf_interleave_t *)self;
    ESP_LOGD(TAG, "Closed, %p", self);
    if (interleave->in_arr != NULL) {
        esp_gmf_oal_free(interleave->in_arr);
        interleave->in_arr = NULL;
    }
    if (interleave->in_load != NULL) {
        esp_gmf_oal_free(interleave->in_load);
        interleave->in_load = NULL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_interleave_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_interleave_t *interleave = (esp_gmf_interleave_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    if (interleave->need_reopen) {
        esp_gmf_interleave_close(self, NULL);
        out_len = esp_gmf_interleave_open(self, NULL);
        if (out_len != ESP_GMF_JOB_ERR_OK) {
            ESP_LOGE(TAG, "Interleave reopen failed");
            return out_len;
        }
    }
    esp_gmf_port_handle_t in = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t in_port = in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_interleave_cfg *interleave_info = (esp_gmf_interleave_cfg *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, interleave_info, return ESP_GMF_JOB_ERR_FAIL);
    int index = interleave_info->src_num;
    int i = 0;
    bool is_done = false;
    esp_gmf_err_io_t load_ret = ESP_GMF_IO_OK;
    memset(interleave->in_load, 0, sizeof(esp_gmf_payload_t *) * index);
    interleave->out_load = NULL;
    int samples_num = ESP_GMF_ELEMENT_GET(interleave)->in_attr.data_size / (interleave->bytes_per_sample);
    int bytes = samples_num * interleave->bytes_per_sample;
    while (in_port != NULL) {
        load_ret = esp_gmf_port_acquire_in(in_port, &(interleave->in_load[i]), bytes, in_port->wait_ticks);
        ESP_GMF_PORT_CHECK(TAG, load_ret, out_len, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __intlv_release;}, "Failed to acquire in, idx:%d, ret: %d", i, load_ret);
        interleave->in_arr[i] = interleave->in_load[i]->buf;
        in_port = in_port->next;
        // if one load is done means interleave need to done
        is_done = (((is_done == 1) || (interleave->in_load[i]->is_done == 1)) ? true : false);
        ESP_LOGV(TAG, "IN: idx: %d load: %p, buf: %p, valid size: %d, buf length: %d, done: %d",
                 i, interleave->in_load[i], interleave->in_load[i]->buf, interleave->in_load[i]->valid_size,
                 interleave->in_load[i]->buf_length, interleave->in_load[i]->is_done);
        i++;
    }
    samples_num = interleave->in_load[0]->valid_size / (interleave->bytes_per_sample);
    bytes = samples_num * interleave->bytes_per_sample;
    load_ret = esp_gmf_port_acquire_out(out_port, &interleave->out_load,
                                        samples_num ? bytes * interleave->src_num : interleave->in_load[0]->buf_length, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, { goto __intlv_release;});
    if (samples_num > 0) {
        esp_ae_err_t ret = esp_ae_intlv_process(interleave->src_num, interleave->bits_per_sample, samples_num,
                                                (void **)interleave->in_arr, interleave->out_load->buf);
        ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __intlv_release;}, "Interleave process error, ret: %d", ret);
    }
    ESP_LOGV(TAG, "OUT: load: %p, buf: %p, valid size: %d, buf length: %d",
             interleave->out_load, interleave->out_load->buf, interleave->out_load->valid_size, interleave->out_load->buf_length);
    interleave->out_load->valid_size = samples_num * interleave->bytes_per_sample * interleave->src_num;
    if (interleave->out_load->valid_size > 0) {
        esp_gmf_audio_el_update_file_pos((esp_gmf_element_handle_t)self, interleave->out_load->valid_size);
    }
    // Interleave finished
    if (is_done == true) {
        ESP_LOGD(TAG, "Interleave is done.");
        interleave->out_load->is_done = true;
        out_len = ESP_GMF_JOB_ERR_DONE;
    }
__intlv_release:
    if (interleave->out_load != NULL) {
        load_ret = esp_gmf_port_release_out(out_port, interleave->out_load, ESP_GMF_MAX_DELAY);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "OUT port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    in_port = in;
    i = 0;
    while (in_port != NULL && interleave->in_load[i] != NULL) {
        load_ret = esp_gmf_port_release_in(in_port, interleave->in_load[i], ESP_GMF_MAX_DELAY);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "IN port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
        in_port = in_port->next;
        i++;
    }
    return out_len;
}

static esp_gmf_err_t interleave_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
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
    esp_gmf_interleave_cfg *config = (esp_gmf_interleave_cfg *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_interleave_t *interleave = (esp_gmf_interleave_t *)self;
    interleave->need_reopen = (config->sample_rate != info->sample_rates) || (config->bits_per_sample != info->bits);
    config->sample_rate = info->sample_rates;
    config->bits_per_sample = info->bits;
    ESP_LOGD(TAG, "RECV element info, from: %s-%p, next: %p, self: %s-%p, type: %x, state: %s, rate: %d, ch: %d, bits: %d",
             OBJ_GET_TAG(el), el, esp_gmf_node_for_next((esp_gmf_node_t *)el), OBJ_GET_TAG(self), self, evt->type,
             esp_gmf_event_get_state_str(state), info->sample_rates, info->channels, info->bits);
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_interleave_destroy(esp_gmf_element_handle_t self)
{
    esp_gmf_interleave_t *interleave = (esp_gmf_interleave_t *)self;
    ESP_LOGD(TAG, "Destroyed, %p", self);
    free_esp_ae_interleave_cfg(OBJ_GET_CFG(self));
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(interleave);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_interleave_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_INTERLEAVE;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_interleave_init(esp_gmf_interleave_cfg *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_interleave_t *interleave = esp_gmf_oal_calloc(1, sizeof(esp_gmf_interleave_t));
    ESP_GMF_MEM_VERIFY(TAG, interleave, {return ESP_GMF_ERR_MEMORY_LACK;}, "interleave", sizeof(esp_gmf_interleave_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)interleave;
    obj->new_obj = esp_gmf_interleave_new;
    obj->del_obj = esp_gmf_interleave_destroy;
    esp_gmf_interleave_cfg *cfg = esp_gmf_oal_calloc(1, sizeof(esp_gmf_interleave_cfg));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto INTLV_INIT_FAIL;}, "interleave configuration", sizeof(esp_gmf_interleave_cfg));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_gmf_interleave_cfg));
    if (config) {
        memcpy(cfg, config, sizeof(esp_gmf_interleave_cfg));
    } else {
        esp_gmf_interleave_cfg dcfg = DEFAULT_ESP_GMF_INTERLEAVE_CONFIG();
        memcpy(cfg, &dcfg, sizeof(esp_gmf_interleave_cfg));
    }
    ret = esp_gmf_obj_set_tag(obj, "aud_intlv");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto INTLV_INIT_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_MULTI, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(interleave, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto INTLV_INIT_FAIL, "Failed to initialize interleave element");
    ESP_GMF_ELEMENT_GET(interleave)->ops.open = esp_gmf_interleave_open;
    ESP_GMF_ELEMENT_GET(interleave)->ops.process = esp_gmf_interleave_process;
    ESP_GMF_ELEMENT_GET(interleave)->ops.close = esp_gmf_interleave_close;
    ESP_GMF_ELEMENT_GET(interleave)->ops.event_receiver = interleave_received_event_handler;
    ESP_GMF_ELEMENT_GET(interleave)->ops.load_caps = _load_interleave_caps_func;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;
INTLV_INIT_FAIL:
    esp_gmf_interleave_destroy(obj);
    return ret;
}
