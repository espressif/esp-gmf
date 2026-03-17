/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_err.h"
#include "esp_gmf_node.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_video_fps_cvt.h"
#include "esp_gmf_video_methods_def.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_element.h"
#include "esp_gmf_video_element.h"
#include "esp_gmf_info.h"
#include "gmf_video_common.h"

static const char *TAG = "VID_FPS_CVT";

/**
 * @brief  Video frame rate convert definition
 */
typedef struct {
    esp_gmf_video_element_t  parent;     /*!< Video element parent */
    uint16_t                 dst_fps;    /*!< Destination frame rate */
    uint32_t                 frame_num;  /*!< Accumulated frame number that received */
    uint64_t                 start_pts;  /*!< Started PTS for first received frame */
} gmf_vid_rate_cvt_t;

static esp_gmf_job_err_t gmf_vid_rate_cvt_open(esp_gmf_element_handle_t self, void *para)
{
    gmf_vid_rate_cvt_t *rate_cvt = (gmf_vid_rate_cvt_t *)self;
    esp_gmf_info_video_t *src_info = &rate_cvt->parent.src_info;
    if (rate_cvt->dst_fps == 0 || rate_cvt->dst_fps > src_info->fps) {
        ESP_LOGE(TAG, "dst fps %d > src fps %d", rate_cvt->dst_fps, src_info->fps);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    esp_gmf_info_video_t vid_info = *src_info;
    vid_info.fps = rate_cvt->dst_fps;
    esp_gmf_element_notify_vid_info(self, &vid_info);
    rate_cvt->frame_num = 0;
    rate_cvt->start_pts = 0;
    return ESP_GMF_JOB_ERR_OK;
}

static bool rate_control_need_drop(gmf_vid_rate_cvt_t *rate_cvt, esp_gmf_payload_t *in_load)
{
    esp_gmf_info_video_t *src_info = &rate_cvt->parent.src_info;
    if (rate_cvt->dst_fps == src_info->fps) {
        return false;
    }
    if (rate_cvt->frame_num == 0) {
        rate_cvt->start_pts = in_load->pts;
        rate_cvt->frame_num++;
        return false;
    }
    uint64_t expected_pts = rate_cvt->start_pts + (rate_cvt->frame_num) * 1000 / rate_cvt->dst_fps;
    if (in_load->pts >= expected_pts) {
        rate_cvt->frame_num++;
        return false;
    }
    return true;
}

static esp_gmf_job_err_t gmf_vid_rate_cvt_process(esp_gmf_element_handle_t self, void *para)
{
    gmf_vid_rate_cvt_t *rate_cvt = (gmf_vid_rate_cvt_t *)self;
    int ret = 0;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    ret = esp_gmf_port_acquire_in(in_port, &in_load, ESP_GMF_ELEMENT_GET(self)->in_attr.data_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, ret, ret, return ret);
    // Handle drop logic
    if (in_load->is_done == false && in_load->valid_size > 0 && rate_control_need_drop(rate_cvt, in_load)) {
        esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
        return ESP_GMF_JOB_ERR_CONTINUE;
    }
    out_load = in_load;
    ret = esp_gmf_port_acquire_out(out_port, &out_load, in_load->valid_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, ret, ret, esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY); return ret);
    esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
    esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
    if (out_load->is_done) {
        ret = ESP_GMF_JOB_ERR_DONE;
    }
    return ret;
}

static esp_gmf_job_err_t gmf_vid_rate_cvt_close(esp_gmf_element_handle_t self, void *para)
{
    gmf_vid_rate_cvt_t *rate_cvt = (gmf_vid_rate_cvt_t *)self;
    rate_cvt->frame_num = 0;
    rate_cvt->start_pts = 0;
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_err_t set_dst_fps(esp_gmf_element_handle_t self, esp_gmf_args_desc_t *arg_desc, uint8_t *buf, int buf_len)
{
    gmf_vid_rate_cvt_t *rate_cvt = (gmf_vid_rate_cvt_t *)self;
    rate_cvt->dst_fps = *(uint16_t *)buf;
    // After change fps reset frame number
    rate_cvt->frame_num = 0;
    return ESP_OK;
}

static esp_gmf_err_t gmf_vid_rate_cvt_load_methods(esp_gmf_element_handle_t handle)
{
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_method_t *methods = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    do {
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(FPS_CVT, SET_FPS, FPS), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(FPS_CVT, SET_FPS), set_dst_fps, set_args);
        ((esp_gmf_element_t *) handle)->method = methods;
        return ESP_GMF_ERR_OK;
    } while (0);
    ESP_LOGE(TAG, "Fail to load methods");
    if (set_args) {
        esp_gmf_args_desc_destroy(set_args);
    }
    if (methods) {
        esp_gmf_method_destroy(methods);
    }
    return ESP_GMF_ERR_MEMORY_LACK;
}

static esp_gmf_err_t gmf_vid_rate_cvt_load_caps(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t cap = {
        .cap_eightcc = ESP_GMF_CAPS_VIDEO_FPS_CVT
    };
    esp_gmf_err_t ret = esp_gmf_cap_append(&caps, &cap);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append caps");
    ((esp_gmf_element_t *) handle)->caps = caps;
    return ret;
}

static esp_gmf_err_t gmf_vid_rate_cvt_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_video_fps_cvt_init(cfg, (esp_gmf_element_handle_t*)handle);
}

static esp_gmf_err_t gmf_vid_rate_cvt_destroy(esp_gmf_element_handle_t self)
{
    gmf_vid_rate_cvt_t *rate_cvt = (gmf_vid_rate_cvt_t *)self;
    esp_gmf_video_el_deinit(self);
    if (rate_cvt) {
        esp_gmf_oal_free(rate_cvt);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_video_fps_cvt_init(void *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    gmf_vid_rate_cvt_t *vid_rate_cvt = esp_gmf_oal_calloc(1, sizeof(gmf_vid_rate_cvt_t));
    ESP_GMF_MEM_CHECK(TAG, vid_rate_cvt, return ESP_GMF_ERR_MEMORY_LACK);
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)vid_rate_cvt;
    obj->new_obj = gmf_vid_rate_cvt_new;
    obj->del_obj = gmf_vid_rate_cvt_destroy;

    esp_gmf_err_t ret = esp_gmf_obj_set_tag(obj, "vid_fps_cvt");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _cvt_init_fail, "Failed set OBJ tag");

    esp_gmf_element_cfg_t el_cfg = { 0 };
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_video_el_init(obj, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _cvt_init_fail, "Failed init video rate convert");

    vid_rate_cvt->parent.base.ops.open = gmf_vid_rate_cvt_open;
    vid_rate_cvt->parent.base.ops.process = gmf_vid_rate_cvt_process;
    vid_rate_cvt->parent.base.ops.close = gmf_vid_rate_cvt_close;
    vid_rate_cvt->parent.base.ops.event_receiver = esp_gmf_video_handle_events;
    vid_rate_cvt->parent.base.ops.load_methods = gmf_vid_rate_cvt_load_methods;
    vid_rate_cvt->parent.base.ops.load_caps = gmf_vid_rate_cvt_load_caps;

    *handle = obj;
    return ESP_GMF_ERR_OK;

_cvt_init_fail:
    esp_gmf_obj_delete(obj);
    return ret;
}

esp_gmf_err_t esp_gmf_video_fps_cvt_set_fps(esp_gmf_element_handle_t handle, uint16_t fps)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method((esp_gmf_element_handle_t)handle, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(FPS_CVT, SET_FPS), &method);
    uint8_t buf[2] = { 0 };
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(FPS_CVT, SET_FPS, FPS), buf, (uint8_t *)&fps, sizeof(fps));
    return esp_gmf_element_exe_method((esp_gmf_element_handle_t)handle, VMETHOD(FPS_CVT, SET_FPS), buf, sizeof(buf));
}
