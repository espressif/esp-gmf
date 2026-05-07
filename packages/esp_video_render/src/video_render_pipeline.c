/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <esp_gmf_pipeline.h>
#include <esp_gmf_element.h>
#include <esp_gmf_node.h>
#include <esp_gmf_video_param.h>
#include <esp_gmf_caps_def.h>
#include "video_render_proc.h"
#include "video_render_pipeline.h"
#include "video_render_utils.h"
#include "esp_log.h"
#include "esp_video_render_log.h"

#define TAG  "VIDEO_RENDER_PIPELINE"
#define VIDEO_PIPE_DEBUG

static inline void add_proc(const char *pipeline_tag[], const char *proc_tag, uint8_t *element_num)
{
    uint8_t n = *element_num;
    // Not add duplicate one
    for (int i = 0; i < n; i++) {
        if (strcmp(pipeline_tag[i], proc_tag) == 0) {
            return;
        }
    }
    pipeline_tag[n] = proc_tag;
    (*element_num)++;
}

static const char *get_element_tag_by_caps(esp_gmf_pool_handle_t pool, uint64_t caps_cc)
{
    const void *iter = NULL;
    esp_gmf_element_handle_t element = NULL;
    while (esp_gmf_pool_iterate_element(pool, &iter, &element) == ESP_GMF_ERR_OK) {
        const esp_gmf_cap_t *caps = NULL;
        esp_gmf_element_get_caps(element, &caps);
        while (caps) {
            if (caps->cap_eightcc == caps_cc) {
                return OBJ_GET_TAG(element);
            }
            caps = caps->next;
        }
    }
    return NULL;
}

static esp_gmf_element_handle_t get_backup_element_tag_by_caps(esp_gmf_pool_handle_t pool, uint64_t caps_cc, const char *skipped)
{
    const void *iter = NULL;
    esp_gmf_element_handle_t element = NULL;
    while (esp_gmf_pool_iterate_element(pool, &iter, &element) == ESP_GMF_ERR_OK) {
        const esp_gmf_cap_t *caps = NULL;
        esp_gmf_element_get_caps(element, &caps);
        while (caps) {
            if (caps->cap_eightcc == caps_cc) {
                const char *tag = OBJ_GET_TAG(element);
                if (strcmp(tag, skipped) != 0) {
                    return element;
                }
            }
            caps = caps->next;
        }
    }
    return NULL;
}

static esp_gmf_element_handle_t get_element_by_caps_from_pool(esp_gmf_pool_handle_t pool, uint64_t caps_cc)
{
    const void *iter = NULL;
    esp_gmf_element_handle_t element = NULL;
    while (esp_gmf_pool_iterate_element(pool, &iter, &element) == ESP_GMF_ERR_OK) {
        const esp_gmf_cap_t *caps = NULL;
        esp_gmf_element_get_caps(element, &caps);
        while (caps) {
            if (caps->cap_eightcc == caps_cc) {
                return element;
            }
            caps = caps->next;
        }
    }
    return NULL;
}

static esp_gmf_element_handle_t get_element_by_caps(esp_gmf_pipeline_handle_t pipeline, uint64_t caps_cc)
{
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_pipeline_get_head_el(pipeline, &element);
    for (; element; esp_gmf_pipeline_get_next_el(pipeline, element, &element)) {
        const esp_gmf_cap_t *caps = NULL;
        esp_gmf_element_get_caps(element, &caps);
        while (caps) {
            if (caps->cap_eightcc == caps_cc) {
                return element;
            }
            caps = caps->next;
        }
    }
    return NULL;
}

static bool element_has_caps(esp_gmf_element_handle_t element, uint64_t caps_cc)
{
    const esp_gmf_cap_t *caps = NULL;
    esp_gmf_element_get_caps(element, &caps);
    while (caps) {
        if (caps->cap_eightcc == caps_cc) {
            return true;
        }
        caps = caps->next;
    }
    return false;
}

uint32_t get_decode_dest_format(esp_gmf_element_handle_t element, uint32_t src_format, uint32_t sink_format)
{
    const uint32_t *dst_fmts = NULL;
    uint8_t dst_fmts_num = 0;
    esp_gmf_video_param_get_dst_fmts_by_codec(element, src_format, &dst_fmts, &dst_fmts_num);
    if (dst_fmts_num == 0) {
        ESP_LOGE(TAG, "Fail to get decoder supported dst formats");
        return 0;
    }
    uint32_t sel_fmt = dst_fmts[0];  // User prefer one
    for (int i = 0; i < dst_fmts_num; i++) {
        if (dst_fmts[i] == sink_format) {
            sel_fmt = dst_fmts[i];
            break;
        }
    }
    return sel_fmt;
}

static esp_video_render_err_t element_setting(esp_gmf_element_handle_t element, esp_video_render_frame_info_t *cur_info,
                                              esp_video_render_frame_info_t *sink_info)
{
    const esp_gmf_cap_t *caps = NULL;
    esp_gmf_element_get_caps(element, &caps);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    for (; caps; caps = caps->next) {
        if (caps->cap_eightcc == ESP_GMF_CAPS_VIDEO_COLOR_CONVERT) {
            ret = esp_gmf_video_param_set_dst_format(element, sink_info->format);
            cur_info->format = sink_info->format;
        } else if (caps->cap_eightcc == ESP_GMF_CAPS_VIDEO_DECODER) {
            uint32_t sel_fmt = get_decode_dest_format(element, (uint32_t)cur_info->format, (uint32_t)sink_info->format);
            if (sel_fmt == 0) {
                return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
            }
            ret = esp_gmf_video_param_set_dst_format(element, sel_fmt);
            cur_info->format = sel_fmt;
        } else if (caps->cap_eightcc == ESP_GMF_CAPS_VIDEO_FPS_CVT) {
            ret = esp_gmf_video_param_set_fps(element, sink_info->fps);
            cur_info->fps = sink_info->fps;
        } else if (caps->cap_eightcc == ESP_GMF_CAPS_VIDEO_SCALE) {
            esp_gmf_video_resolution_t res = {
                .width = sink_info->width,
                .height = sink_info->height,
            };
            ret = esp_gmf_video_param_set_dst_resolution(element, &res);
            cur_info->width = sink_info->width;
            cur_info->height = sink_info->height;
        }
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Fail to set for %s", OBJ_GET_TAG(element));
            break;
        }
    }
    return ret == ESP_GMF_ERR_OK ? ESP_VIDEO_RENDER_ERR_OK : ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
}

static esp_video_render_err_t apply_settings(esp_gmf_pipeline_handle_t pipeline, video_render_pipeline_cfg_t *proc_cfg)
{
    esp_video_render_frame_info_t cur_info = proc_cfg->in_frame_info;
    esp_gmf_element_handle_t head_element = NULL;
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_pipeline_get_head_el(pipeline, &element);
    head_element = element;

    // Report info for first element
    esp_gmf_info_video_t v_info = {
        .format_id = (uint32_t)proc_cfg->in_frame_info.format,
        .width = proc_cfg->in_frame_info.width,
        .height = proc_cfg->in_frame_info.height,
        .fps = proc_cfg->in_frame_info.fps,
    };
    int ret = esp_gmf_pipeline_report_info(pipeline, ESP_GMF_INFO_VIDEO, &v_info, sizeof(v_info));
    if (ret != ESP_GMF_ERR_OK) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    for (; element; esp_gmf_pipeline_get_next_el(pipeline, element, &element)) {
        esp_video_render_err_t ret = element_setting(element, &cur_info, &proc_cfg->out_frame_info);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            return ret;
        }
    }
    // Open element in serial
    element = head_element;
    for (; element; esp_gmf_pipeline_get_next_el(pipeline, element, &element)) {
        esp_gmf_event_state_t st = ESP_GMF_EVENT_STATE_NONE;
        esp_gmf_element_get_state(element, &st);
        if (st == ESP_GMF_EVENT_STATE_INITIALIZED) {
            ret = esp_gmf_element_process_open(element, NULL);
            if (ret != ESP_GMF_ERR_OK) {
                ESP_LOGE(TAG, "Failed to open element %s", OBJ_GET_TAG(element));
                break;
            }
        }
    }
    return ret == ESP_GMF_ERR_OK ? ESP_VIDEO_RENDER_ERR_OK : ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
}

static uint8_t video_render_optimize_pipeline(video_render_pipeline_cfg_t *proc_cfg, const char *pipeline_tag[])
{
    uint8_t element_num = 0;
    esp_gmf_element_handle_t dec_element = NULL;
    if (video_render_is_encoded(proc_cfg->in_frame_info.format)) {
        dec_element = get_element_by_caps_from_pool(proc_cfg->pool, ESP_VIDEO_RENDER_PROC_DEC);
        if (dec_element == NULL) {
            ESP_LOGE(TAG, "Not found decode in pool");
            return 0;
        }
        add_proc(pipeline_tag, OBJ_GET_TAG(dec_element), &element_num);
    }
    if (proc_cfg->in_frame_info.fps > proc_cfg->out_frame_info.fps) {
        const char *tag = get_element_tag_by_caps(proc_cfg->pool, ESP_VIDEO_RENDER_PROC_FPS);
        if (tag) {
            add_proc(pipeline_tag, tag, &element_num);
        }
    }
    bool ch_cvt_added = false;
    if (proc_cfg->out_frame_info.format && proc_cfg->in_frame_info.format != proc_cfg->out_frame_info.format &&
        video_render_get_pixel_bits(proc_cfg->in_frame_info.format) <= video_render_get_pixel_bits(proc_cfg->out_frame_info.format)) {
        bool need_add_ccvt = true;
        uint32_t sel_format = proc_cfg->in_frame_info.format;
        if (dec_element) {
            uint32_t out_format = proc_cfg->out_frame_info.format;
            sel_format = get_decode_dest_format(dec_element, proc_cfg->in_frame_info.format, out_format);
            if (sel_format == out_format) {
                // No need to add color convert
                need_add_ccvt = false;
                ch_cvt_added = true;
            }
        }
        if (need_add_ccvt) {
            // Force to add software color convert for YUV420P
            const char *tag = get_element_tag_by_caps(proc_cfg->pool, ESP_VIDEO_RENDER_PROC_CCVT);
            if (tag) {
                if (sel_format == ESP_VIDEO_RENDER_FORMAT_YUV420P && strcmp(tag, "vid_ppa") == 0) {
                    esp_gmf_element_handle_t element = get_backup_element_tag_by_caps(proc_cfg->pool, ESP_VIDEO_RENDER_PROC_CCVT, tag);
                    if (element) {
                        tag = OBJ_GET_TAG(element);
                        ESP_LOGI(TAG, "Force to use %s for HW not support YUV420P", tag);
                    }
                }
                add_proc(pipeline_tag, tag, &element_num);
                ch_cvt_added = true;
            }
        }
    }
    // Add Crop firstly if has
    for (int i = 0; i < proc_cfg->proc_num; i++) {
        if (element_has_caps(proc_cfg->proc_elements[i], ESP_VIDEO_RENDER_PROC_CROP)) {
            const char *tag = OBJ_GET_TAG(proc_cfg->proc_elements[i]);
            if (tag) {
                add_proc(pipeline_tag, tag, &element_num);
            }
            break;
        }
    }
    if ((proc_cfg->in_frame_info.width * proc_cfg->in_frame_info.height) >
        (proc_cfg->out_frame_info.width * proc_cfg->out_frame_info.height)) {
        const char *tag = get_element_tag_by_caps(proc_cfg->pool, ESP_VIDEO_RENDER_PROC_SCALE);
        if (tag) {
            add_proc(pipeline_tag, tag, &element_num);
        }
    }
    // Add Normal procs
    for (int i = 0; i < proc_cfg->proc_num; i++) {
        if (element_has_caps(proc_cfg->proc_elements[i], ESP_VIDEO_RENDER_PROC_DEC)) {
            continue;
        }
        // Put rotate to last position
        if (element_has_caps(proc_cfg->proc_elements[i], ESP_VIDEO_RENDER_PROC_ROTATE)) {
            continue;
        }
        const char *tag = OBJ_GET_TAG(proc_cfg->proc_elements[i]);
        if (tag) {
            video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "try to add normal %s\n", tag);
            add_proc(pipeline_tag, tag, &element_num);
        }
    }
    if (proc_cfg->in_frame_info.width != proc_cfg->out_frame_info.width ||
        proc_cfg->in_frame_info.height != proc_cfg->out_frame_info.height) {
        const char *tag = get_element_tag_by_caps(proc_cfg->pool, ESP_VIDEO_RENDER_PROC_SCALE);
        if (tag) {
            add_proc(pipeline_tag, tag, &element_num);
        }
    }
    if (proc_cfg->out_frame_info.format && proc_cfg->in_frame_info.format != proc_cfg->out_frame_info.format && ch_cvt_added == false) {
        const char *tag = get_element_tag_by_caps(proc_cfg->pool, ESP_VIDEO_RENDER_PROC_CCVT);
        if (tag) {
            add_proc(pipeline_tag, tag, &element_num);
        }
    }
    for (int i = 0; i < proc_cfg->proc_num; i++) {
        // Put rotate to last position
        if (element_has_caps(proc_cfg->proc_elements[i], ESP_VIDEO_RENDER_PROC_ROTATE)) {
            const char *tag = OBJ_GET_TAG(proc_cfg->proc_elements[i]);
            if (tag) {
                video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "try to add normal %s\n", tag);
                add_proc(pipeline_tag, tag, &element_num);
                break;
            }
        }
    }
    // For debug purpose only
#ifdef VIDEO_PIPE_DEBUG
    ESP_LOGI(TAG, "Auto Generate pipeline");
    for (int i = 0; i < element_num; i++) {
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_INFO, "%s -> ", pipeline_tag[i]);
    }
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_INFO, "\n");
#endif  /* VIDEO_PIPE_DEBUG */
    return element_num;
}

static esp_gmf_element_handle_t get_proc_element(esp_gmf_element_handle_t proc_elements[], uint8_t proc_num, const char *el_name)
{
    for (int i = 0; i < proc_num; i++) {
        if (strcmp(OBJ_GET_TAG(proc_elements[i]), el_name) == 0) {
            return proc_elements[i];
        }
    }
    return NULL;
}

static void video_render_unregister_el(esp_gmf_pipeline_handle_t pipeline, esp_gmf_element_handle_t el)
{
    esp_gmf_node_t *cur_el = (esp_gmf_node_t *)pipeline->head_el;
    esp_gmf_node_t *pre_el = NULL;

    while (cur_el) {
        if (cur_el == el) {
            // Update last_el if needed
            if (pipeline->last_el == el) {
                pipeline->last_el = pre_el;
            }
            // Update previous node's next pointer
            if (pre_el) {
                pre_el->next = cur_el->next;
            } else {
                pipeline->head_el = cur_el->next;
            }
            // Update next node's prev pointer
            if (cur_el->next) {
                cur_el->next->prev = pre_el;
            }
            cur_el->next = NULL;
            cur_el->prev = NULL;
            break;
        }
        pre_el = cur_el;
        cur_el = cur_el->next;
    }
}

static esp_gmf_err_t video_render_new_pipeline(esp_gmf_pool_handle_t handle,
                                               const char *el_name[], int num_of_el_name,
                                               esp_gmf_element_handle_t *proc_elements, uint8_t proc_num,
                                               esp_gmf_pipeline_handle_t *pipeline)
{
    if (num_of_el_name < 1) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_pipeline_t *pl = NULL;
    esp_gmf_pipeline_create(&pl);
    ESP_GMF_MEM_CHECK(TAG, pl, return ESP_GMF_ERR_MEMORY_LACK);

    esp_gmf_obj_handle_t new_prev_el_obj = NULL;
    int ret = ESP_GMF_ERR_OK;
    // Link the elements
    for (int i = 0; i < num_of_el_name; ++i) {
        // Check whether already created
        esp_gmf_element_handle_t new_el = get_proc_element(proc_elements, proc_num, el_name[i]);
        if (new_el == NULL) {
            ret = esp_gmf_pool_new_element(handle, el_name[i], &new_el);
        }
        if (ret != ESP_GMF_ERR_OK || new_el == NULL) {
            ESP_LOGE(TAG, "Fail to create element");
            break;
        }
        if (i) {
            esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(NULL, NULL, NULL, NULL,
                                                                        (ESP_GMF_ELEMENT_GET(new_prev_el_obj)->out_attr.data_size), ESP_GMF_MAX_DELAY);
            if (out_port == NULL) {
                ret = ESP_GMF_ERR_MEMORY_LACK;
                break;
            }
            ret = esp_gmf_element_register_out_port((esp_gmf_element_handle_t)new_prev_el_obj, out_port);
            if (ret != ESP_GMF_ERR_OK) {
                esp_gmf_port_deinit(out_port);
                break;
            }
            esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(NULL, NULL, NULL, NULL,
                                                                      (ESP_GMF_ELEMENT_GET(new_el)->in_attr.data_size), ESP_GMF_MAX_DELAY);
            if (in_port == NULL) {
                ret = ESP_GMF_ERR_MEMORY_LACK;
                break;
            }
            ret = esp_gmf_element_register_in_port(new_el, in_port);
            if (ret != ESP_GMF_ERR_OK) {
                esp_gmf_port_deinit(in_port);
                break;
            }
        }
        esp_gmf_pipeline_register_el(pl, new_el);
        new_prev_el_obj = new_el;
    }
    if (ret == ESP_GMF_ERR_OK) {
        *pipeline = pl;
        return ESP_GMF_ERR_OK;
    }
    if (pl) {
        // Keep pre-defined elements
        for (int i = 0; i < proc_num; i++) {
            video_render_unregister_el(pl, proc_elements[i]);
        }
        esp_gmf_pipeline_destroy(pl);
    }
    return ESP_GMF_ERR_MEMORY_LACK;
}

esp_gmf_element_handle_t video_render_create_element(esp_gmf_pool_handle_t pool, esp_video_render_proc_type_t proc_type)
{
    const char *tag = get_element_tag_by_caps(pool, (uint64_t)proc_type);
    if (tag == NULL) {
        return NULL;
    }
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_err_t ret = esp_gmf_pool_new_element(pool, tag, &element);
    if (ret == ESP_GMF_ERR_OK) {
        return element;
    }
    return NULL;
}

static void video_render_pipeline_free(esp_gmf_pipeline_handle_t pipeline, esp_gmf_element_handle_t *kept_elements, uint8_t kept_num)
{
    if (pipeline == NULL) {
        return;
    }
    esp_gmf_element_handle_t head_element = ESP_GMF_PIPELINE_GET_FIRST_ELEMENT(pipeline);
    esp_gmf_element_handle_t tail_element = ESP_GMF_PIPELINE_GET_LAST_ELEMENT(pipeline);
    esp_gmf_element_unregister_in_port(head_element, NULL);
    esp_gmf_element_unregister_out_port(tail_element, NULL);
    // Keep pre-defined elements
    for (int i = 0; i < kept_num; i++) {
        video_render_unregister_el(pipeline, kept_elements[i]);
        esp_gmf_element_unregister_in_port(kept_elements[i], NULL);
        esp_gmf_element_unregister_out_port(kept_elements[i], NULL);
    }
    esp_gmf_pipeline_destroy(pipeline);
}

esp_video_render_err_t video_render_pipeline_open(video_render_pipeline_cfg_t *proc_cfg,
                                                  esp_gmf_pipeline_handle_t *pipeline)
{
    // Optimized element order and create pipeline
    const char *pipeline_tag[proc_cfg->proc_num + 4];
    uint8_t element_num = video_render_optimize_pipeline(proc_cfg, pipeline_tag);
    int ret = video_render_new_pipeline(proc_cfg->pool, pipeline_tag, element_num,
                                        proc_cfg->proc_elements, proc_cfg->proc_num,
                                        pipeline);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Fail to create pipeline");
        // When pipeline create failed need free port accordingly
        if (proc_cfg->in_port) {
            esp_gmf_port_deinit(proc_cfg->in_port);
        }
        if (proc_cfg->out_port) {
            esp_gmf_port_deinit(proc_cfg->out_port);
        }
        return ESP_VIDEO_RENDER_ERR_NO_RESOURCE;
    }
    esp_gmf_element_handle_t head_element = ESP_GMF_PIPELINE_GET_FIRST_ELEMENT((*pipeline));
    esp_gmf_element_handle_t tail_element = ESP_GMF_PIPELINE_GET_LAST_ELEMENT((*pipeline));
    esp_gmf_element_register_in_port(head_element, proc_cfg->in_port);
    esp_gmf_element_register_out_port(tail_element, proc_cfg->out_port);
    esp_gmf_pipeline_set_event(*pipeline, proc_cfg->cb, proc_cfg->ctx);
    // Do settings and open
    ret = apply_settings(*pipeline, proc_cfg);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        video_render_pipeline_free(*pipeline, proc_cfg->proc_elements, proc_cfg->proc_num);
        *pipeline = NULL;
        return ret;
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_gmf_element_handle_t video_render_pipeline_get_element(esp_gmf_pipeline_handle_t pipeline, esp_video_render_proc_type_t proc_type)
{
    if (pipeline == NULL || proc_type == ESP_VIDEO_RENDER_PROC_NONE) {
        return NULL;
    }
    return get_element_by_caps(pipeline, (uint64_t)proc_type);
}

esp_video_render_err_t video_render_pipeline_close(esp_gmf_pipeline_handle_t pipeline,
                                                   esp_gmf_element_handle_t *kept_elements, uint8_t kept_num)
{
    // Close elements in serial
    if (pipeline == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_pipeline_get_head_el(pipeline, &element);
    for (; element; esp_gmf_pipeline_get_next_el(pipeline, element, &element)) {
        int ret = esp_gmf_element_process_close(element, NULL);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to close element %s", OBJ_GET_TAG(element));
        }
        esp_gmf_element_reset_state(element);
    }
    video_render_pipeline_free(pipeline, kept_elements, kept_num);
    return ESP_VIDEO_RENDER_ERR_OK;
}
