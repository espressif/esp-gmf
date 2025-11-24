/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gmf_general_el.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_port.h"
#include "esp_gmf_element.h"
#include "esp_log.h"
#include "esp_gmf_err.h"

static const char *TAG = "GENERAL_EL";

static esp_gmf_err_t general_el_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return general_el_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t general_el_open(esp_gmf_element_handle_t self, void *para)
{
    general_el_t *el = (general_el_t *)self;
    general_el_cfg_t *cfg = (general_el_cfg_t *)OBJ_GET_CFG(self);
    el->open_order = cfg->state->open_order++;
    el->open_count++;
    if (cfg->is_dependent && cfg->report_in_process == false) {
        // Notify dependency to next
        esp_gmf_element_notify_snd_info(self, &el->snd_info);
    }
    // Return configured error value if set, otherwise return OK
    if (el->open_return != ESP_GMF_JOB_ERR_OK) {
        return el->open_return;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t general_el_close(esp_gmf_element_handle_t self, void *para)
{
    general_el_t *el = (general_el_t *)self;
    el->close_count++;
    // Return configured error value if set, otherwise return OK
    if (el->close_return != ESP_GMF_JOB_ERR_OK) {
        return el->close_return;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t general_el_reset(esp_gmf_element_handle_t self, void *para)
{
    general_el_t *el = (general_el_t *)self;
    el->reset_count++;
    el->running_count = 0;
    // Return configured error value if set, otherwise return OK
    if (el->reset_return != ESP_GMF_JOB_ERR_OK) {
        return el->reset_return;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t general_el_process(esp_gmf_element_handle_t self, void *para)
{
    general_el_t *el = (general_el_t *)self;
    general_el_cfg_t *cfg = (general_el_cfg_t *)OBJ_GET_CFG(self);
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;

    // Return configured error value if set (before normal processing)
    if (el->process_return != ESP_GMF_JOB_ERR_OK) {
        return el->process_return;
    }

    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    int bytes = ESP_GMF_ELEMENT_GET(el)->in_attr.data_size;
    esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &in_load, bytes, ESP_GMF_MAX_DELAY);
    do {
        ESP_GMF_PORT_CHECK(TAG, load_ret, out_len, break, "Failed to acquire in, ret: %d", load_ret);

        int running_count = el->running_count;
        el->running_count++;
        if ((el->open_order == 0) && (el->running_count >= DEFAULT_RUN_LOOPS)) {
            in_load->is_done = true;
        }
        if (cfg->is_dependent && cfg->report_in_process) {
            if (running_count < cfg->report_pos) {
                // No need report for all data consumed
                out_len = ESP_GMF_JOB_ERR_CONTINUE;
                break;
            }
            if (running_count == cfg->report_pos) {
                // Report info to next
                esp_gmf_element_notify_snd_info(self, &el->snd_info);
            }
        }
        load_ret = esp_gmf_port_acquire_out(out_port, &out_load, in_load->buf_length, ESP_GMF_MAX_DELAY);
        ESP_GMF_PORT_CHECK(TAG, load_ret, out_len, break, "Failed to acquire out, ret: %d", load_ret);
        out_load->pts = in_load->pts;
        out_load->is_done = in_load->is_done;
        if (in_load->is_done) {
            ESP_LOGW(TAG, "el_process, %s-%p, out_done", OBJ_GET_TAG(el), el);
            out_len = ESP_GMF_JOB_ERR_DONE;
        }
    } while (0);
    if (out_load != NULL) {
        esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
    }
    if (in_load != NULL) {
        esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
    }
    return out_len;
}

static esp_gmf_err_t general_el_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, ctx, { return ESP_GMF_ERR_INVALID_ARG; });
    ESP_GMF_NULL_CHECK(TAG, evt, { return ESP_GMF_ERR_INVALID_ARG; });
    if ((evt->type != ESP_GMF_EVT_TYPE_REPORT_INFO)
        || (evt->sub != ESP_GMF_INFO_SOUND)
        || (evt->payload == NULL)) {
        return ESP_GMF_ERR_OK;
    }
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t)ctx;
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_NONE;
    esp_gmf_element_get_state(self, &state);
    general_el_t *el = (general_el_t *)self;
    memcpy(&el->snd_info, evt->payload, sizeof(esp_gmf_info_sound_t));
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    // Return configured error value if set, otherwise return OK
    if (el->event_return != ESP_GMF_ERR_OK) {
        return el->event_return;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t general_el_destroy(esp_gmf_element_handle_t self)
{
    void *cfg = OBJ_GET_CFG(self);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(self);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t general_el_init(general_el_cfg_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG; });
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    general_el_t *el = esp_gmf_oal_calloc(1, sizeof(general_el_t));
    ESP_GMF_MEM_VERIFY(TAG, el, { return ESP_GMF_ERR_MEMORY_LACK; }, "General element", sizeof(general_el_t));
    // Initialize error return values to OK
    el->open_return = ESP_GMF_JOB_ERR_OK;
    el->close_return = ESP_GMF_JOB_ERR_OK;
    el->reset_return = ESP_GMF_JOB_ERR_OK;
    el->process_return = ESP_GMF_JOB_ERR_OK;
    el->event_return = ESP_GMF_ERR_OK;
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)el;
    obj->new_obj = general_el_new;
    obj->del_obj = general_el_destroy;
    esp_gmf_obj_set_tag(obj, "general");
    esp_gmf_element_cfg_t el_cfg = { 0 };
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = config->is_dependent;
    general_el_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(general_el_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto GENERAL_EL_FAIL; }, "general element configuration", sizeof(general_el_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(general_el_cfg_t));
    if (config) {
        memcpy(cfg, config, sizeof(general_el_cfg_t));
    }
    ret = esp_gmf_audio_el_init(el, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto GENERAL_EL_FAIL, "Failed to initialize general element");
    ESP_GMF_ELEMENT_GET(el)->ops.open = general_el_open;
    ESP_GMF_ELEMENT_GET(el)->ops.process = general_el_process;
    ESP_GMF_ELEMENT_GET(el)->ops.close = general_el_close;
    ESP_GMF_ELEMENT_GET(el)->ops.reset = general_el_reset;
    ESP_GMF_ELEMENT_GET(el)->ops.event_receiver = general_el_event_handler;
    *handle = obj;
    return ESP_GMF_ERR_OK;
GENERAL_EL_FAIL:
    general_el_destroy(obj);
    return ret;
}
