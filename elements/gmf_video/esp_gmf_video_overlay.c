/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_err.h"
#include "esp_fourcc.h"
#include "esp_gmf_node.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_video_element.h"
#include "esp_gmf_info.h"
#include "esp_gmf_video_overlay.h"
#include "esp_gmf_video_methods_def.h"
#include "esp_gmf_caps_def.h"
#include "gmf_video_common.h"

static const char *TAG = "OVERLAY_MIXER";

/**
 * @brief  Video pixel data definition
 */
typedef struct {
    uint8_t  *data;  /*!< Video pixel data */
    uint32_t  size;  /*!< Video pixel data size */
} esp_gmf_video_pixel_data_t;

/**
 * @brief  Video overlay definition
 */
typedef struct {
    esp_gmf_video_element_t     parent;           /*!< Video element parent */
    esp_gmf_port_handle_t       overlay_port;     /*!< GMF port for overlay */
    bool                        enable;           /*!< Overlay enable setting from user */
    bool                        overlay_enabled;  /*!< Whether overlay enabled or not */
    esp_gmf_overlay_rgn_info_t  overlay_rgn;      /*!< Overlay region info */
    uint8_t                     window_alpha;     /*!< Overlay window alpha */
    bool                        is_open;          /*!< Whether element is open or not */
} gmf_vid_overlay_t;

static esp_gmf_err_t sw_mixer_open(gmf_vid_overlay_t *mixer)
{
    // Currently only support RGB565
    if (mixer->parent.src_info.format_id != ESP_FOURCC_RGB16) {
        ESP_LOGE(TAG, "Only support RGB565");
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    return ESP_GMF_ERR_OK;
}

static inline uint16_t sw_mix_rgb565(uint16_t pixel_a, uint16_t pixel_b, uint8_t alpha)
{
    uint8_t inv_alpha = 255 - alpha;
    uint8_t r_a = (pixel_a >> 11) & 0x1F;
    uint8_t g_a = (pixel_a >> 5) & 0x3F;
    uint8_t b_a = pixel_a & 0x1F;

    uint8_t r_b = (pixel_b >> 11) & 0x1F;
    uint8_t g_b = (pixel_b >> 5) & 0x3F;
    uint8_t b_b = pixel_b & 0x1F;

    uint8_t r = (r_a * inv_alpha + r_b * alpha) >> 8;
    uint8_t g = (g_a * inv_alpha + g_b * alpha) >> 8;
    uint8_t b = (b_a * inv_alpha + b_b * alpha) >> 8;

    return (r << 11) | (g << 5) | b;
}

static esp_gmf_err_t sw_mixer_process(gmf_vid_overlay_t *mixer, esp_gmf_video_pixel_data_t *dst,
                                      esp_gmf_video_pixel_data_t *window_data)
{
    if (mixer->window_alpha == 0) {
        return ESP_GMF_ERR_OK;
    }
    esp_gmf_video_rgn_t *window = &mixer->overlay_rgn.dst_rgn;
    esp_gmf_info_video_t *src_info = &mixer->parent.src_info;

    if (mixer->overlay_rgn.format_id == ESP_FOURCC_RGB16) {
        // size check
        int dst_size = src_info->width * src_info->height * 2;
        int window_size = window->width * window->height * 2;
        if (dst->size < dst_size || window_data->size < window_size) {
            return ESP_GMF_ERR_INVALID_ARG;
        }
        uint16_t *dst_pixel = ((uint16_t *)dst->data) + window->y * src_info->width;
        dst_pixel += window->x;
        uint16_t *window_pixel = (uint16_t *)window_data->data;
        if (mixer->window_alpha == 255) {
            for (int i = 0; i < window->height; i++) {
                memcpy(dst_pixel, window_pixel, window->width * 2);
                dst_pixel += src_info->width;
                window_pixel += window->width;
            }
            return ESP_GMF_ERR_OK;
        } else {
            for (int i = 0; i < window->height; i++) {
                uint16_t *cur_dst = dst_pixel;
                uint16_t *cur_window = window_pixel;
                uint16_t *line_end = cur_window + window->width;
                while (cur_window < line_end) {
                    *cur_dst = sw_mix_rgb565(*cur_dst, *cur_window, mixer->window_alpha);
                    cur_dst++;
                    cur_window++;
                }
                dst_pixel += src_info->width;
                window_pixel = line_end;
            }
            return ESP_GMF_ERR_OK;
        }
    }
    return ESP_GMF_ERR_NOT_SUPPORT;
}

static esp_gmf_err_t overlay_enable(gmf_vid_overlay_t *overlay_mixer)
{
    esp_gmf_info_video_t *src_info = &overlay_mixer->parent.src_info;
    // Element not open or no overlay set
    if (overlay_mixer->overlay_port == NULL || overlay_mixer->enable == false || overlay_mixer->is_open == false) {
        return ESP_GMF_ERR_OK;
    }
    if (overlay_mixer->overlay_enabled) {
        return ESP_GMF_ERR_OK;
    }
    esp_gmf_video_rgn_t *rgn = &overlay_mixer->overlay_rgn.dst_rgn;
    if (overlay_mixer->overlay_rgn.format_id != src_info->format_id ||
        rgn->x + rgn->width > src_info->width ||
        rgn->y + rgn->height > src_info->height) {
        ESP_LOGE(TAG, "Wrong overlay region or codec settings");
        return ESP_GMF_ERR_NOT_SUPPORT;
    }
    esp_gmf_err_t ret = sw_mixer_open(overlay_mixer);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    overlay_mixer->overlay_enabled = true;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t overlay_disable(gmf_vid_overlay_t *overlay_mixer)
{
    if (overlay_mixer->overlay_enabled == false) {
        return ESP_GMF_ERR_INVALID_STATE;
    }
    overlay_mixer->overlay_enabled = false;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_job_err_t gmf_vid_overlay_open(esp_gmf_element_handle_t self, void *para)
{
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)self;
    overlay_mixer->is_open = true;
    esp_gmf_err_t ret = overlay_enable(overlay_mixer);
    if (ret != ESP_GMF_ERR_OK) {
        return ESP_GMF_JOB_ERR_FAIL;
    }
    esp_gmf_element_notify_vid_info(self, &overlay_mixer->parent.src_info);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t gmf_vid_overlay_process(esp_gmf_element_handle_t self, void *para)
{
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)self;
    int ret = 0;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    ret = esp_gmf_port_acquire_in(in_port, &in_load, ESP_GMF_ELEMENT_GET(self)->in_attr.data_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, ret, ret, return ret);
    out_load = in_load;
    if (in_load->valid_size > 0 && overlay_mixer->overlay_enabled && overlay_mixer->overlay_port) {
        // Fetch overlay data
        esp_gmf_payload_t *overlay_load = NULL;
        ret = esp_gmf_port_acquire_in(overlay_mixer->overlay_port, &overlay_load, ESP_GMF_ELEMENT_GET(self)->in_attr.data_size,
                                      ESP_GMF_MAX_DELAY);
        if (ret >= 0) {
            // TODO workaround use PTS to store alpha
            uint8_t alpha = (uint8_t)overlay_load->pts;
            overlay_mixer->window_alpha = alpha;
            // Do mixer
            esp_gmf_video_pixel_data_t dst_frame = {
                .data = in_load->buf,
                .size = in_load->valid_size,
            };
            esp_gmf_video_pixel_data_t overlay_frame = {
                .data = overlay_load->buf,
                .size = overlay_load->valid_size,
            };
            ret = sw_mixer_process(overlay_mixer, &dst_frame, &overlay_frame);
            esp_gmf_port_release_in(overlay_mixer->overlay_port, overlay_load, ESP_GMF_MAX_DELAY);
        } else {
            ESP_LOGE(TAG, "Fail to fetch overlay data ret %d", ret);
        }
    }
    ret = esp_gmf_port_acquire_out(out_port, &out_load, in_load->valid_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, ret, ret, esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY); return ret);
    esp_gmf_port_release_out(out_port, out_load, ESP_GMF_MAX_DELAY);
    esp_gmf_port_release_in(in_port, in_load, ESP_GMF_MAX_DELAY);
    if (out_load->is_done) {
        ret = ESP_GMF_JOB_ERR_DONE;
    }
    return ret;
}

static esp_gmf_job_err_t gmf_vid_overlay_close(esp_gmf_element_handle_t self, void *para)
{
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)self;
    overlay_disable(overlay_mixer);
    overlay_mixer->is_open = false;
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_err_t gmf_vid_overlay_destroy(esp_gmf_element_handle_t self)
{
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)self;
    esp_gmf_video_el_deinit(self);
    if (overlay_mixer != NULL) {
        esp_gmf_oal_free(overlay_mixer);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_video_overlay_set_overlay_port(esp_gmf_element_handle_t self, esp_gmf_port_handle_t port)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, port, return ESP_GMF_ERR_INVALID_ARG);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method(self, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(OVERLAY, SET_PORT), &method);
    uint8_t buf[sizeof(esp_gmf_port_handle_t)];
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_PORT, PORT), buf, (uint8_t *)&port, sizeof(esp_gmf_port_handle_t));
    return esp_gmf_element_exe_method(self, VMETHOD(OVERLAY, SET_PORT), buf, sizeof(buf));
}

esp_gmf_err_t esp_gmf_video_overlay_set_alpha(esp_gmf_element_handle_t self, uint8_t alpha)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method(self, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(OVERLAY, SET_ALPHA), &method);
    uint8_t buf[sizeof(uint8_t)];
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_ALPHA, ALPHA), buf, (uint8_t *)&alpha, sizeof(esp_gmf_port_handle_t));
    return esp_gmf_element_exe_method(self, VMETHOD(OVERLAY, SET_ALPHA), buf, sizeof(buf));
}

esp_gmf_err_t esp_gmf_video_overlay_set_rgn(esp_gmf_element_handle_t self, esp_gmf_overlay_rgn_info_t *rgn_info)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, rgn_info, return ESP_GMF_ERR_INVALID_ARG);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method(self, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(OVERLAY, SET_RGN), &method);
    uint8_t buf[sizeof(esp_gmf_overlay_rgn_info_t)];
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, FMT), buf, (uint8_t *)&rgn_info->format_id, sizeof(uint32_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, X), buf, (uint8_t *)&rgn_info->dst_rgn.x, sizeof(uint16_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, Y), buf, (uint8_t *)&rgn_info->dst_rgn.y, sizeof(uint16_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, WIDTH), buf, (uint8_t *)&rgn_info->dst_rgn.width, sizeof(uint16_t));
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, SET_RGN, HEIGHT), buf, (uint8_t *)&rgn_info->dst_rgn.height, sizeof(uint16_t));
    return esp_gmf_element_exe_method(self, VMETHOD(OVERLAY, SET_RGN), buf, sizeof(buf));
}

esp_gmf_err_t esp_gmf_video_overlay_enable(esp_gmf_element_handle_t self, bool enable)
{
    ESP_GMF_NULL_CHECK(TAG, self, return ESP_GMF_ERR_INVALID_ARG);
    const esp_gmf_method_t *method_head = NULL;
    const esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method(self, &method_head);
    esp_gmf_method_found(method_head, VMETHOD(OVERLAY, OVERLAY_ENABLE), &method);
    uint8_t buf[sizeof(bool)];
    esp_gmf_args_set_value(method->args_desc, VMETHOD_ARG(OVERLAY, OVERLAY_ENABLE, ENABLE), buf, (uint8_t *)&enable, sizeof(bool));
    return esp_gmf_element_exe_method(self, VMETHOD(OVERLAY, OVERLAY_ENABLE), buf, sizeof(buf));
}

static esp_gmf_err_t set_mixer_enable(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                      uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)handle;
    overlay_mixer->enable = *(bool *)buf;
    return overlay_mixer->enable ? overlay_enable(overlay_mixer) : overlay_disable(overlay_mixer);
}

static esp_gmf_err_t set_mixer_rgn(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                   uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)handle;
    overlay_mixer->overlay_rgn.format_id = *(uint32_t *)buf;
    buf += sizeof(uint32_t);
    overlay_mixer->overlay_rgn.dst_rgn.x = *(uint16_t *)(buf);
    overlay_mixer->overlay_rgn.dst_rgn.y = *(uint16_t *)(buf + 2);
    overlay_mixer->overlay_rgn.dst_rgn.width = *(uint16_t *)(buf + 4);
    overlay_mixer->overlay_rgn.dst_rgn.height = *(uint16_t *)(buf + 6);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t set_mixer_port(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                    uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)handle;
    overlay_mixer->overlay_port = *(esp_gmf_port_handle_t *)buf;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t set_mixer_alpha(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                     uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    gmf_vid_overlay_t *overlay_mixer = (gmf_vid_overlay_t *)handle;
    overlay_mixer->window_alpha = *(uint8_t *)buf;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t gmf_vid_overlay_load_methods(esp_gmf_element_handle_t handle)
{
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_method_t *methods = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    do {
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, OVERLAY_ENABLE, ENABLE), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(OVERLAY, OVERLAY_ENABLE), set_mixer_enable, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, FMT), ESP_GMF_ARGS_TYPE_UINT32, sizeof(uint32_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, X), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 4);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, Y), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 6);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, WIDTH), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 8);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_RGN, HEIGHT), ESP_GMF_ARGS_TYPE_UINT16, sizeof(uint16_t), 10);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(OVERLAY, SET_RGN), set_mixer_rgn, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_PORT, PORT), ESP_GMF_ARGS_TYPE_UINT32, sizeof(intptr_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(OVERLAY, SET_PORT), set_mixer_port, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        set_args = NULL;
        ret = esp_gmf_args_desc_append(&set_args, VMETHOD_ARG(OVERLAY, SET_ALPHA, ALPHA), ESP_GMF_ARGS_TYPE_UINT8, sizeof(uint8_t), 0);
        GMF_VIDEO_BREAK_ON_FAIL(ret);
        ret = esp_gmf_method_append(&methods, VMETHOD(OVERLAY, SET_ALPHA), set_mixer_alpha, set_args);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

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

static esp_gmf_err_t gmf_vid_overlay_load_caps(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t cap = {0};
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    do {
        cap.cap_eightcc = ESP_GMF_CAPS_VIDEO_OVERLAY;
        cap.attr_fun = NULL;
        ret = esp_gmf_cap_append(&caps, &cap);
        GMF_VIDEO_BREAK_ON_FAIL(ret);

        ((esp_gmf_element_t *) handle)->caps = caps;
        return ret;
    } while (0);
    if (caps) {
        esp_gmf_cap_destroy(caps);
    }
    return ret;
}

static esp_gmf_err_t gmf_vid_overlay_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_video_overlay_init(cfg, (esp_gmf_element_handle_t *)handle);
}

esp_gmf_err_t esp_gmf_video_overlay_init(void *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_MEM_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    gmf_vid_overlay_t *overlay_mixer = esp_gmf_oal_calloc(1, sizeof(gmf_vid_overlay_t));
    ESP_GMF_MEM_CHECK(TAG, overlay_mixer, return ESP_GMF_ERR_MEMORY_LACK);
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)overlay_mixer;
    obj->new_obj = gmf_vid_overlay_new;
    obj->del_obj = gmf_vid_overlay_destroy;

    int ret = esp_gmf_obj_set_tag(obj, "vid_overlay");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _overlay_init_fail, "Failed set OBJ tag");

    esp_gmf_element_cfg_t el_cfg = { 0 };
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_MULTI, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
                                     ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_video_el_init(obj, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto _overlay_init_fail, "Failed init video rate convert");

    overlay_mixer->parent.base.ops.open = gmf_vid_overlay_open;
    overlay_mixer->parent.base.ops.process = gmf_vid_overlay_process;
    overlay_mixer->parent.base.ops.close = gmf_vid_overlay_close;
    overlay_mixer->parent.base.ops.event_receiver = esp_gmf_video_handle_events;
    overlay_mixer->parent.base.ops.load_methods = gmf_vid_overlay_load_methods;
    overlay_mixer->parent.base.ops.load_caps = gmf_vid_overlay_load_caps;

    *handle = obj;
    ESP_LOGI(TAG, "Create video overlay, %s-%p", OBJ_GET_TAG(obj), obj);
    return ESP_GMF_ERR_OK;

_overlay_init_fail:
    esp_gmf_obj_delete(obj);
    return ret;
}
