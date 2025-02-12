/*
 * ESPRESSIF MIT License
 *
 * Copyright (c) 2024 <ESPRESSIF SYSTEMS (SHANGHAI) CO., LTD>
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

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_gmf_element.h"

#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_node.h"

static const char *TAG = "ESP_GMF_ELEMENT";

static inline int _get_port_cnt(esp_gmf_port_handle_t port)
{
    int k = 0;
    esp_gmf_port_handle_t tmp = port;
    while (tmp) {
        k++;
        tmp = tmp->next;
    }
    return k;
}

static inline int _del_port_from_el(esp_gmf_port_handle_t *head, esp_gmf_port_handle_t io_inst)
{
    int ret = esp_gmf_port_del_at(head, io_inst);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_ERR_NOT_FOUND, "Not found port, head:%p, port:%p", head, io_inst);
    esp_gmf_port_deinit(io_inst);
    return ret;
}

static inline esp_gmf_err_t esp_gmf_notify_state_changed(esp_gmf_element_handle_t handle, esp_gmf_event_state_t st)
{
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    esp_gmf_event_pkt_t evt = {
        .from = el,
        .type = ESP_GMF_EVT_TYPE_CHANGE_STATE,
        .sub = st,
        .payload = NULL,
        .payload_size = 0,
    };
    el->cur_state = st;
    if (el->event_func == NULL) {
        ESP_LOGW(TAG, "[%s,%p] element not has register event callback", OBJ_GET_TAG(el), el);
        return ESP_GMF_ERR_FAIL;
    }
    return el->event_func(&evt, el->ctx);
}

esp_gmf_err_t esp_gmf_element_init(esp_gmf_element_handle_t handle, esp_gmf_element_cfg_t *config)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    if (config->dependency == true) {
        el->init_state = ESP_GMF_EVENT_STATE_NONE;
    } else {
        el->init_state = ESP_GMF_EVENT_STATE_INITIALIZED;
    }
    el->cur_state = el->init_state;
    el->event_func = config->cb;
    el->dependency = config->dependency;
    el->in_attr.cap = config->in_attr.cap == 0 ? ESP_GMF_EL_PORT_CAP_SINGLE : config->in_attr.cap;
    el->out_attr.cap = config->out_attr.cap == 0 ? ESP_GMF_EL_PORT_CAP_SINGLE : config->out_attr.cap;
    el->in_attr.type = config->in_attr.type == 0 ? ESP_GMF_PORT_TYPE_BYTE : config->in_attr.type;
    el->out_attr.type = config->out_attr.type == 0 ? ESP_GMF_PORT_TYPE_BYTE : config->out_attr.type;
    el->in_attr.size = config->in_attr.size == 0 ? ESP_GMF_ELEMENT_PORT_SIZE_DEFAULT : config->in_attr.size;
    el->out_attr.size = config->out_attr.size == 0 ? ESP_GMF_ELEMENT_PORT_SIZE_DEFAULT : config->out_attr.size;

    el->ctx = config->ctx;
    el->job_mask = 0;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_deinit(esp_gmf_element_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    esp_gmf_method_destroy(el->method);
    esp_gmf_port_handle_t port = el->in;
    while (port) {
        esp_gmf_port_handle_t tmp = port->next;
        esp_gmf_port_deinit(port);
        port = tmp;
    }
    port = el->out;
    while (port) {
        esp_gmf_port_handle_t tmp = port->next;
        esp_gmf_port_deinit(port);
        port = tmp;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_set_event_func(esp_gmf_element_handle_t handle, esp_gmf_event_cb cb, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->event_func = cb;
    el->ctx = ctx;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_register_in_port(esp_gmf_element_handle_t handle, esp_gmf_port_handle_t io_inst)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, io_inst, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    if ((el->in_attr.type & io_inst->type) == 0) {
        ESP_LOGE(TAG, "The in port type[%x] is unsupported, expected:%x,[%p-%s]", io_inst->type, el->in_attr.type, handle, OBJ_GET_TAG(handle));
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    int cnt = _get_port_cnt(el->in);
    if ((el->in_attr.cap & ESP_GMF_EL_PORT_CAP_SINGLE) && cnt) {
        ESP_LOGE(TAG, "Can't register more in ports for an element that is only support single port, cnt:%x,[%p-%s]", cnt, handle, OBJ_GET_TAG(handle));
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    if (el->in == NULL) {
        el->in = io_inst;
        io_inst->next = NULL;
    } else {
        esp_gmf_port_add_last(el->in, io_inst);
    }
    esp_gmf_port_set_reader(io_inst, (void *)handle);
    ESP_LOGD(TAG, "REG in port, el:%p-%s, head:%p, new:%p", el, OBJ_GET_TAG(el), el->in, io_inst);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_unregister_in_port(esp_gmf_element_handle_t handle, esp_gmf_port_handle_t io_inst)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    int ret = ESP_GMF_ERR_OK;
    if (io_inst) {
        ESP_LOGD(TAG, "UNREG in port, el:%p-%s, head:%p, del:%p, cnt:%d", el, OBJ_GET_TAG(el), el->in, io_inst, _get_port_cnt(el->in));
        ret = _del_port_from_el(&el->in, io_inst);
        ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_ERR_NOT_FOUND, "Unregister in port failed, del:%p, el:%p-%s", io_inst, el, OBJ_GET_TAG(el));
    } else {
        esp_gmf_port_handle_t tmp = el->in;
        while (tmp) {
            esp_gmf_port_handle_t next = tmp->next;
            ESP_LOGD(TAG, "UNREG in port, el:%p-%s, head:%p, del:%p, cnt:%d", el, OBJ_GET_TAG(el), el->in, tmp, _get_port_cnt(el->in));
            ret = _del_port_from_el(&el->in, tmp);
            ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_ERR_NOT_FOUND, "Unregister all in port failed, del:%p, el:%p-%s", tmp, el, OBJ_GET_TAG(el));
            tmp = next;
        }
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_register_out_port(esp_gmf_element_handle_t handle, esp_gmf_port_handle_t io_inst)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, io_inst, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    if ((el->out_attr.type & io_inst->type) == 0) {
        ESP_LOGE(TAG, "The out port type[%x] is unsupported, expected:%x, [%p-%s]", io_inst->type, el->out_attr.type, handle, OBJ_GET_TAG(handle));
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    int cnt = _get_port_cnt(el->out);
    if ((el->out_attr.cap & ESP_GMF_EL_PORT_CAP_SINGLE) && cnt) {
        ESP_LOGE(TAG, "Can't register more out ports for an element that is only support single port, cnt:%x, [%p-%s]", cnt, handle, OBJ_GET_TAG(handle));
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    if (el->out == NULL) {
        el->out = io_inst;
        io_inst->next = NULL;
    } else {
        esp_gmf_port_add_last(el->out, io_inst);
    }
    esp_gmf_port_set_writer(io_inst, (void *)handle);
    ESP_LOGD(TAG, "REG out port, el:%p-%s, head:%p, new:%p", el, OBJ_GET_TAG(el), el->out, io_inst);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_unregister_out_port(esp_gmf_element_handle_t handle, esp_gmf_port_handle_t io_inst)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    int ret = ESP_GMF_ERR_OK;
    if (io_inst) {
        ESP_LOGD(TAG, "UNREG out port, el:%p-%s, head:%p, del:%p, cnt:%d", el, OBJ_GET_TAG(el), el->out, io_inst, _get_port_cnt(el->out));
        ret = _del_port_from_el(&el->out, io_inst);
        ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_ERR_NOT_FOUND, "Unregister in port failed, del:%p, el:%p-%s", io_inst, el, OBJ_GET_TAG(el));
    } else {
        esp_gmf_port_handle_t tmp = el->out;
        while (tmp) {
            esp_gmf_port_handle_t next = tmp->next;
            ESP_LOGD(TAG, "UNREG out port, el:%p-%s, head:%p, del:%p, cnt:%d", el, OBJ_GET_TAG(el), el->out, tmp, _get_port_cnt(el->out));
            ret = _del_port_from_el(&el->out, tmp);
            ESP_GMF_RET_ON_ERROR(TAG, ret, return ESP_GMF_ERR_NOT_FOUND, "Unregister all in port failed, del:%p, el:%p-%s", tmp, el, OBJ_GET_TAG(el));
            tmp = next;
        }
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_link_el(esp_gmf_element_handle_t handle, esp_gmf_element_handle_t new_el)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, new_el, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *header_el = (esp_gmf_element_t *)handle;
    esp_gmf_element_t *tail_el = (esp_gmf_element_t *)esp_gmf_node_get_tail((esp_gmf_node_t *)header_el);
    esp_gmf_node_add_last((esp_gmf_node_t *)header_el, (esp_gmf_node_t *)new_el);

    esp_gmf_port_set_reader(tail_el->out, new_el);
    esp_gmf_port_set_writer(ESP_GMF_ELEMENT_GET(new_el)->in, tail_el);

    ESP_LOGD(TAG, "EL:%p-%s, NEXT EL:%p-%s", header_el, OBJ_GET_TAG(header_el), new_el, OBJ_GET_TAG(new_el));
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_get_next_el(esp_gmf_element_handle_t handle, esp_gmf_element_handle_t *next_el)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, next_el, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    *next_el = (esp_gmf_element_t *)esp_gmf_node_for_next((esp_gmf_node_t *)el);
    ESP_LOGD(TAG, "Get next, EL:%p-%s, NEXT EL:%p-%s", el, OBJ_GET_TAG(el), *next_el, OBJ_GET_TAG(*next_el));
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_get_prev_el(esp_gmf_element_handle_t handle, esp_gmf_element_handle_t *prev_el)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, prev_el, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    *prev_el = (esp_gmf_element_t *)esp_gmf_node_for_prev((esp_gmf_node_t *)el);
    ESP_LOGD(TAG, "Get previous, EL:%p-%s, PREV EL:%p-%s", el, OBJ_GET_TAG(el), *prev_el, OBJ_GET_TAG(*prev_el));
    return ESP_GMF_ERR_OK;
}

esp_gmf_job_err_t esp_gmf_element_process_open(esp_gmf_element_handle_t handle, void *para)
{
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    if (el->ops.open == NULL) {
        ESP_LOGE(TAG, "There is no open function [%p-%s]", handle, OBJ_GET_TAG(handle));
        return ESP_GMF_ERR_FAIL;
    }
    // Last element outport allowed to be NULL
    bool last_element = (el->base.next == NULL);
    if ((el->in == NULL) || ((last_element == false) && (el->out == NULL))) {
        ESP_LOGE(TAG, "There is no in or out port,in:%p,out:%p [%p-%s]", el->in, el->out, handle, OBJ_GET_TAG(handle));
        return ESP_GMF_JOB_ERR_FAIL;
    }
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    ret = el->ops.open(el, NULL);
    if (ret == ESP_GMF_JOB_ERR_OK) {
        esp_gmf_notify_state_changed(handle, ESP_GMF_EVENT_STATE_RUNNING);
    }
    return ret;
}

esp_gmf_job_err_t esp_gmf_element_process_running(esp_gmf_element_handle_t handle, void *para)
{
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    if (el->ops.process == NULL) {
        ESP_LOGE(TAG, "There is no process function [%p-%s]", handle, OBJ_GET_TAG(handle));
        return ESP_GMF_ERR_FAIL;
    }
    return el->ops.process(el, NULL);
}

esp_gmf_job_err_t esp_gmf_element_process_close(esp_gmf_element_handle_t handle, void *para)
{
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    if (el->ops.close == NULL) {
        ESP_LOGE(TAG, "There is no close function [%p-%s]", handle, OBJ_GET_TAG(handle));
        return ESP_GMF_ERR_FAIL;
    }
    esp_gmf_job_err_t ret = el->ops.close(el, NULL);
    // Release port still have reference
    esp_gmf_port_t *in_port = ESP_GMF_ELEMENT_GET_IN_PORT(el);
    if (in_port && in_port->ref_count) {
        if (in_port->ops.release) {
            in_port->ops.release(in_port->ctx, in_port->self_payload, 0);
        }
        in_port->ref_count = 0;
    }
    return ret;
}

esp_gmf_err_t esp_gmf_element_set_state(esp_gmf_element_handle_t handle, esp_gmf_event_state_t new_state)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->cur_state = new_state;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_get_state(esp_gmf_element_handle_t handle, esp_gmf_event_state_t *state)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    if (state) {
        *state = el->cur_state;
        return ESP_GMF_ERR_OK;
    }
    return ESP_GMF_ERR_INVALID_ARG;
}

esp_gmf_err_t esp_gmf_element_reset_state(esp_gmf_element_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->cur_state = el->init_state;
    el->job_mask = 0;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_reset_port(esp_gmf_element_handle_t handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    esp_gmf_port_t *tmp = el->in;
    while (tmp) {
        esp_gmf_port_reset(tmp);
        tmp = tmp->next;
    }
    tmp = el->out;
    while (tmp) {
        esp_gmf_port_reset(tmp);
        tmp = tmp->next;
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_receive_event(esp_gmf_element_handle_t handle, esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    if (el->ops.event_receiver) {
        return el->ops.event_receiver(event, el);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_set_job_mask(esp_gmf_element_handle_t handle, uint16_t mask)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->job_mask = mask;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_change_job_mask(esp_gmf_element_handle_t handle, uint16_t mask)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->job_mask |= mask;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_get_job_mask(esp_gmf_element_handle_t handle, uint16_t *mask)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    *mask = el->job_mask;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_element_notify_snd_info(esp_gmf_element_handle_t handle, esp_gmf_info_sound_t *info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    esp_gmf_event_pkt_t evt = {
        .from = el,
        .type = ESP_GMF_EVT_TYPE_REPORT_INFO,
        .sub = ESP_GMF_INFO_SOUND,
        .payload = info,
        .payload_size = sizeof(*info),
    };
    if (el->event_func == NULL) {
        ESP_LOGW(TAG, "[%p-%s] element not has register event callback", el, OBJ_GET_TAG(el));
        return ESP_GMF_ERR_FAIL;
    }
    return el->event_func(&evt, el->ctx);
}

esp_gmf_err_t esp_gmf_element_notify_vid_info(esp_gmf_element_handle_t handle, esp_gmf_info_video_t *info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    esp_gmf_event_pkt_t evt = {
        .from = el,
        .type = ESP_GMF_EVT_TYPE_REPORT_INFO,
        .sub = ESP_GMF_INFO_VIDEO,
        .payload = info,
        .payload_size = sizeof(*info),
    };
    if (el->event_func == NULL) {
        ESP_LOGW(TAG, "[%p-%s] element not has register event callback", el, OBJ_GET_TAG(el));
        return ESP_GMF_ERR_FAIL;
    }
    return el->event_func(&evt, el->ctx);
}

esp_gmf_err_t esp_gmf_element_register_method(esp_gmf_element_handle_t handle, const char *name, esp_gmf_method_func func, esp_gmf_args_desc_t *args_desc)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, func, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    int ret = esp_gmf_method_append(&el->method, name, func, args_desc);
    return ret;
}

esp_gmf_err_t esp_gmf_element_exe_method(esp_gmf_element_handle_t handle, const char *name, uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, name, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, buf, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    esp_gmf_method_t *mthd = el->method;
    if (mthd == NULL) {
        ESP_LOGE(TAG, "There are no executable methods, [%p-%s]", el, OBJ_GET_TAG(el));
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    int ret = 0;
    while (mthd) {
        if (strcasecmp(mthd->name, name) == 0) {
            ret = mthd->func(handle, mthd->args_desc, buf, buf_len);
        }
        ESP_LOGD(TAG, "Method[%p-%s], ret:%x, [%p-%s]\r\n", mthd, mthd->name, ret, el, OBJ_GET_TAG(el));
        mthd = mthd->next;
    }
    return ret;
}

esp_gmf_err_t esp_gmf_element_get_method(esp_gmf_element_handle_t handle, esp_gmf_method_t **mthd)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, mthd, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    *mthd = el->method;
    return ESP_GMF_ERR_OK;
}
