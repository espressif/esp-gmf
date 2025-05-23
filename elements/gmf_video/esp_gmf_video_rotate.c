/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_err.h"
#include "esp_gmf_node.h"
#include "esp_gmf_video_rotate.h"
#include "esp_gmf_video_methods_def.h"
#include "esp_gmf_video_element.h"
#include "gmf_video_common.h"
#include "esp_gmf_caps_def.h"

static const char *TAG = "IMGFX_ROTATE_EL";

typedef struct _gmf_imgfx_rotate_t {
    esp_gmf_video_element_t   parent;
    esp_imgfx_rotate_handle_t hd;
    bool                      need_recfg;
} esp_gmf_rotate_hd_t;

static esp_gmf_job_err_t video_rotate_el_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_rotate_hd_t *video_el = (esp_gmf_rotate_hd_t *)self;
    // Get and check config
    esp_imgfx_rotate_cfg_t *cfg = (esp_imgfx_rotate_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_CHECK(TAG, cfg, return ESP_GMF_JOB_ERR_FAIL, "Failed to get rotate config");
    // Open rotate module
    esp_imgfx_rotate_open(cfg, &video_el->hd);
    ESP_GMF_CHECK(TAG, video_el->hd, return ESP_GMF_JOB_ERR_FAIL, "Failed to create rotate handle");
    // Get video size
    esp_imgfx_get_image_size(cfg->in_pixel_fmt, &cfg->in_res, (uint32_t *)&(ESP_GMF_ELEMENT_GET(video_el)->in_attr.data_size));
    esp_imgfx_resolution_t res;
    esp_imgfx_rotate_get_rotated_resolution(video_el->hd, &res);
    esp_imgfx_get_image_size(cfg->in_pixel_fmt, &res, (uint32_t *)&(ESP_GMF_ELEMENT_GET(video_el)->out_attr.data_size));
    // Report information to the next element, the next element will use this information to configure
    gmf_video_update_info(self, res.width, res.height, cfg->in_pixel_fmt);
    // The video_el->hd has been opened using newest configuration, so it can set the need_recfg to false
    video_el->need_recfg = false;
    ESP_LOGD(TAG, "Open, %p", self);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t video_rotate_el_process(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_rotate_hd_t *video_el = (esp_gmf_rotate_hd_t *)self;
    esp_imgfx_data_t in_image;
    esp_imgfx_data_t out_image;
    esp_imgfx_rotate_cfg_t *cfg = (esp_imgfx_rotate_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_MEM_CHECK(TAG, cfg, return ESP_GMF_JOB_ERR_FAIL);
    bool bypass = cfg->degree % 360 == 0;
    if (video_el->need_recfg) {
        // Reset rotate configuration
        esp_imgfx_err_t imgfx_ret = esp_imgfx_rotate_set_cfg(video_el->hd, cfg);
        if (imgfx_ret != ESP_IMGFX_ERR_OK) {
            return ESP_GMF_JOB_ERR_FAIL;
        }
        // Get video size
        esp_imgfx_get_image_size(cfg->in_pixel_fmt, &cfg->in_res, (uint32_t *)&(ESP_GMF_ELEMENT_GET(video_el)->in_attr.data_size));
        esp_imgfx_resolution_t res;
        esp_imgfx_rotate_get_rotated_resolution(video_el->hd, &res);
        esp_imgfx_get_image_size(cfg->in_pixel_fmt, &res, (uint32_t *)&(ESP_GMF_ELEMENT_GET(video_el)->out_attr.data_size));
        video_el->need_recfg = false;
    }
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    esp_gmf_job_err_t ret = ESP_GMF_JOB_ERR_OK;
    esp_gmf_err_io_t load_ret = ESP_GMF_IO_OK;
    ret = video_el_acquire_payload(in_port, out_port, &in_load, &out_load, (ESP_GMF_ELEMENT_GET(video_el)->in_attr.data_size),
                                   (ESP_GMF_ELEMENT_GET(video_el)->out_attr.data_size), bypass);
    if (ret != ESP_GMF_JOB_ERR_OK) {
        goto __release;
    }
    /* It is for bypass */
    if (in_load == out_load) {
        goto __release;
    }
    if (in_load->is_done) {
        out_load->is_done = in_load->is_done;
        out_load->pts = in_load->pts;
        ret = ESP_GMF_JOB_ERR_DONE;
        ESP_LOGD(TAG, "It's done, out: %d", in_load->valid_size);
        goto __release;
    }
    in_image.data = in_load->buf;
    in_image.data_len = in_load->valid_size;
    out_image.data = out_load->buf;
    out_image.data_len = out_load->buf_length;
    esp_imgfx_err_t imgfx_ret = esp_imgfx_rotate_process(video_el->hd, &in_image, &out_image);
    if (imgfx_ret != ESP_IMGFX_ERR_OK) {
        ESP_LOGE(TAG, "Image effects rotate process failed, ret: %d-%p", imgfx_ret, video_el);
        ret = ESP_GMF_JOB_ERR_FAIL;
        goto __release;
    }
    // Copy the information to the output payload. The next element will use this information to process
    out_load->is_done = in_load->is_done;
    out_load->valid_size = (ESP_GMF_ELEMENT_GET(video_el)->out_attr.data_size);
    out_load->pts = in_load->pts;
__release:
    // Release in and out port
    if (out_load != NULL) {
        load_ret = esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "OUT port release error, ret:%d", load_ret);
            ret = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    if (in_load != NULL) {
        load_ret = esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "IN port release error, ret:%d", load_ret);
            ret = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    return ret;
}

static esp_gmf_job_err_t video_rotate_el_close(esp_gmf_element_handle_t self, void *para)
{
    ESP_LOGD(TAG, "Closed, %p", self);
    esp_gmf_rotate_hd_t *video_el = (esp_gmf_rotate_hd_t *)self;
    if (video_el) {
        if (video_el->hd) {
            esp_imgfx_rotate_close(video_el->hd);
            video_el->hd = NULL;
        }
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t video_rotate_el_delete(esp_gmf_obj_handle_t handle)
{
    ESP_LOGD(TAG, "Deleted, %p", handle);
    void *cfg = OBJ_GET_CFG(handle);
    if (cfg) {
        esp_gmf_oal_free(cfg);
    }
    esp_gmf_video_el_deinit(handle);
    esp_gmf_oal_free(handle);
    return ESP_GMF_ERR_OK;
}

static inline esp_gmf_err_t esp_gmf_video_rotate_set_cfg(esp_gmf_element_handle_t self, esp_imgfx_rotate_cfg_t *config)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_JOB_ERR_FAIL);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_JOB_ERR_FAIL);
    esp_gmf_rotate_hd_t *video_el = (esp_gmf_rotate_hd_t *)self;
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)video_el;
    // reset obj config
    if (obj->cfg) {
        memcpy(obj->cfg, config, sizeof(esp_imgfx_rotate_cfg_t));
        video_el->need_recfg = true;
        return ESP_GMF_JOB_ERR_OK;
    }
    ESP_LOGE(TAG, " %s-%p is no configured yet", __func__, self);
    return ESP_GMF_JOB_ERR_FAIL;
}

static inline esp_gmf_err_t esp_gmf_video_rotate_get_cfg(esp_gmf_element_handle_t self, esp_imgfx_rotate_cfg_t *config)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_JOB_ERR_FAIL);
    esp_gmf_rotate_hd_t *video_el = (esp_gmf_rotate_hd_t *)self;
    // Get cfg from rotate handle if handle valid or-else get from object
    if (video_el->hd) {
        esp_imgfx_err_t imgfx_ret = esp_imgfx_rotate_get_cfg(video_el->hd, config);
        if (imgfx_ret != ESP_IMGFX_ERR_OK) {
            ESP_LOGE(TAG, "Get video effects rotate cfg failed, hd:%p, ret: %d", self, imgfx_ret);
            return ESP_GMF_JOB_ERR_FAIL;
        }
        return ESP_GMF_JOB_ERR_OK;
    } else {
        esp_gmf_obj_t *obj = (esp_gmf_obj_t *)video_el;
        // Get obj config
        if (obj->cfg) {
            memcpy(config, obj->cfg, sizeof(esp_imgfx_rotate_cfg_t));
            return ESP_GMF_JOB_ERR_OK;
        }
    }
    ESP_LOGE(TAG, " %s-%p is no configured yet", __func__, self);
    return ESP_GMF_JOB_ERR_FAIL;
}

static esp_gmf_err_t video_set_dst_degree(void *handle, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    ESP_GMF_MEM_CHECK(TAG, buf, return ESP_GMF_ERR_INVALID_ARG);
    return esp_gmf_video_rotate_set_rotation(handle, *(uint16_t *)buf);
}

static esp_gmf_err_t video_rotate_el_load_caps(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_VIDEO_ROTATE;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to create capability");
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t video_rotate_el_load_methods(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_args_desc_t *set_args = NULL;
    ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(ROTATOR, SET_ANGLE, DEGREE), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append rotation degree");
    ret = esp_gmf_method_append(&method, VMETHOD(ROTATOR, SET_ANGLE), video_set_dst_degree, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register %s method", VMETHOD(ROTATOR, SET_ANGLE));
    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ret;
}

static esp_gmf_err_t video_rotate_el_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, ctx, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, evt, return ESP_GMF_ERR_INVALID_ARG);
    if ((evt->type != ESP_GMF_EVT_TYPE_REPORT_INFO)
        || (evt->sub != ESP_GMF_INFO_VIDEO)
        || (evt->payload == NULL)) {
        return ESP_GMF_ERR_OK;
    }
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t)ctx;
    esp_gmf_element_handle_t el = evt->from;
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_NONE;
    esp_gmf_element_get_state(self, &state);
    esp_gmf_info_video_t *info = (esp_gmf_info_video_t *)evt->payload;
    esp_gmf_video_el_set_src_info(self, info);
    esp_imgfx_rotate_cfg_t *config = (esp_imgfx_rotate_cfg_t *)OBJ_GET_CFG(self);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_FAIL);
    esp_gmf_rotate_hd_t *video_el = (esp_gmf_rotate_hd_t *)self;
    GMF_VIDEO_UPDATE_CONFIG(config, info, video_el->need_recfg);
    ESP_LOGD(TAG, "RECV element info, from: %s-%p, next: %p, self: %s-%p, type: %x, state: %s, width: %d, height: %d, pixel format: %lx",
             OBJ_GET_TAG(el), el, esp_gmf_node_for_next((esp_gmf_node_t *)el), OBJ_GET_TAG(self), self, evt->type,
             esp_gmf_event_get_state_str(state), info->width, info->height, info->format_id);
    // It is for the first time to receive event from previous element
    if (state == ESP_GMF_EVENT_STATE_NONE) {
        esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t video_rotate_el_new(void *config, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_video_rotate_init((esp_imgfx_rotate_cfg_t *)config, handle);
}

esp_gmf_err_t esp_gmf_video_rotate_init(esp_imgfx_rotate_cfg_t *config, esp_gmf_obj_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    *handle = NULL;
    esp_gmf_rotate_hd_t *video_el = esp_gmf_oal_calloc(1, sizeof(esp_gmf_rotate_hd_t));
    ESP_GMF_MEM_CHECK(TAG, video_el, return ESP_GMF_ERR_MEMORY_LACK);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)video_el;
    // Set element tag
    ret = esp_gmf_obj_set_tag(video_el, "vid_rotate");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto __init_exit, "Failed set OBJ tag");
    // Configure object callbacks
    obj->new_obj = video_rotate_el_new;
    obj->del_obj = video_rotate_el_delete;
    esp_imgfx_rotate_cfg_t *cfg = esp_gmf_oal_calloc(1, sizeof(esp_imgfx_rotate_cfg_t));
    ESP_GMF_MEM_VERIFY(TAG, cfg, {ret = ESP_GMF_ERR_MEMORY_LACK; goto __init_exit;}, "rotate configuration", sizeof(esp_imgfx_rotate_cfg_t));
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_imgfx_rotate_cfg_t));
    if (config) {
        memcpy(cfg, config, sizeof(esp_imgfx_rotate_cfg_t));
    } else {
        esp_imgfx_rotate_cfg_t dcfg = DEFAULT_ESP_GMF_ROTATE_CONFIG();
        memcpy(cfg, &dcfg, sizeof(esp_imgfx_rotate_cfg_t));
    }
    // Configure element
    esp_gmf_element_cfg_t el_cfg = {
        .dependency = true,
    };
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                      ESP_GMF_PORT_TYPE_BLOCK, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    // Initialize element
    ret = esp_gmf_video_el_init(video_el, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto __init_exit, "Failed initialize video element");
    // Register element's callbacks
    esp_gmf_video_element_t *parent = (esp_gmf_video_element_t *)video_el;
    parent->base.ops.open = video_rotate_el_open;
    parent->base.ops.process = video_rotate_el_process;
    parent->base.ops.close = video_rotate_el_close;
    parent->base.ops.event_receiver = video_rotate_el_received_event_handler;
    parent->base.ops.load_caps = video_rotate_el_load_caps;
    parent->base.ops.load_methods = video_rotate_el_load_methods;
    *handle = (esp_gmf_obj_handle_t)video_el;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ret;
__init_exit:
    video_rotate_el_delete(video_el);
    return ret;
}

esp_gmf_err_t esp_gmf_video_rotate_set_rotation(esp_gmf_element_handle_t handle, uint16_t degree)
{
    esp_imgfx_rotate_cfg_t config;
    esp_gmf_err_t ret = esp_gmf_video_rotate_get_cfg(handle, &config);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to set destination degree");
    config.degree = degree;
    return esp_gmf_video_rotate_set_cfg(handle, &config);
}
