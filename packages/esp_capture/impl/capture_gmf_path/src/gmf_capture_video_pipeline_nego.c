/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "capture_pipeline_nego.h"
#include "capture_share_copy_el.h"
#include "capture_video_src_el.h"
#include "capture_video_src_el.h"
#include "capture_pipeline_builder.h"
#include "capture_pipeline_utils.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_video_methods_def.h"
#include "esp_gmf_video_param.h"
#include "esp_gmf_video_ppa.h"
#include "esp_gmf_video_fps_cvt.h"
#include "esp_gmf_video_enc.h"
#include "esp_log.h"

#define TAG "VID_PIPE_NEGO"

extern const char *esp_gmf_video_get_format_string(uint32_t format_id);

static esp_capture_err_t capture_path_apply_setting(esp_gmf_element_handle_t element, esp_capture_video_info_t *sink_info,
                                                    esp_capture_video_info_t *dst_info)
{
    int ret = ESP_GMF_ERR_OK;

    const esp_gmf_cap_t *caps = NULL;
    esp_gmf_element_get_caps(element, &caps);

    for (; caps; caps = caps->next) {
        if (caps->cap_eightcc == ESP_GMF_CAPS_VIDEO_COLOR_CONVERT) {
            ret = esp_gmf_video_param_set_dst_format(element, sink_info->format_id);
            dst_info->format_id = sink_info->format_id;
        } else if (caps->cap_eightcc == ESP_GMF_CAPS_VIDEO_FPS_CVT) {
            ret = esp_gmf_video_param_set_fps(element, sink_info->fps);
            dst_info->fps = sink_info->fps;
        } else if (caps->cap_eightcc == ESP_GMF_CAPS_VIDEO_SCALE) {
            esp_gmf_video_resolution_t res = {
                .width = sink_info->width,
                .height = sink_info->height,
            };
            ret = esp_gmf_video_param_set_dst_resolution(element, &res);
            dst_info->width = sink_info->width;
            dst_info->height = sink_info->height;
        }
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Fail to set for %s", OBJ_GET_TAG(element));
            break;
        }
    }
    return ret == ESP_GMF_ERR_OK ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

esp_capture_err_t esp_capture_video_pipeline_auto_setup(void *h, esp_capture_video_info_t *src_info,
                                                        esp_capture_video_info_t *sink_info,
                                                        esp_capture_video_info_t *dst_info)
{
    esp_gmf_pipeline_handle_t pipeline = (esp_gmf_pipeline_handle_t)h;
    esp_gmf_element_handle_t element = NULL;
    bool reported = false;
    *dst_info = *src_info;
    esp_gmf_pipeline_get_head_el(pipeline, &element);

    // Not setting video encode in this API
    for (; element; esp_gmf_pipeline_get_next_el(pipeline, element, &element)) {
        const char *tag = OBJ_GET_TAG(element);
        if (strcmp(tag, "share_copier") == 0) {
            continue;
        }
        if (reported == false) {
            esp_gmf_info_video_t v_info = {
                .format_id = (uint32_t)src_info->format_id,
                .height = src_info->height,
                .width = src_info->width,
                .fps = src_info->fps,
            };
            esp_gmf_pipeline_report_info(pipeline, ESP_GMF_INFO_VIDEO, &v_info, sizeof(v_info));
            reported = true;
        }
        int ret = capture_path_apply_setting(element, sink_info, dst_info);
        if (ret != ESP_CAPTURE_ERR_OK) {
            return ret;
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

static bool video_need_encode(esp_capture_format_id_t format_id)
{
    if (format_id == ESP_CAPTURE_FMT_ID_MJPEG || format_id == ESP_CAPTURE_FMT_ID_H264) {
        return true;
    }
    return false;
}

static bool capture_negotiate_ok(esp_capture_video_info_t *a, esp_capture_video_info_t *b)
{
    return (a->format_id == b->format_id && a->width == b->width &&
            a->height == b->height && a->fps == b->fps);
}

static esp_capture_err_t capture_negotiate_all_link(esp_capture_pipeline_builder_if_t *builder,
                                                    esp_capture_gmf_pipeline_t *pipelines,
                                                    uint8_t pipeline_num,
                                                    esp_capture_gmf_pipeline_t *src,
                                                    esp_capture_video_info_t *src_info,
                                                    esp_capture_video_info_t *sink_arr,
                                                    uint8_t path_mask)
{
    if ((src->path_mask & path_mask) == 0) {
        return ESP_CAPTURE_ERR_OK;
    }
    esp_capture_video_info_t dst_info = *src_info;
    uint8_t path_idx = GET_PATH_IDX(src->path_mask);
    esp_capture_video_info_t *sink_info = &sink_arr[path_idx];
    // TODO always negotiate with last path
    int ret = esp_capture_video_pipeline_auto_setup(src->pipeline, src_info, sink_info, &dst_info);
    if (ret != ESP_CAPTURE_ERR_OK) {
        return ret;
    }
    if (capture_pipeline_is_sink(src->pipeline)) {
        if (!capture_negotiate_ok(sink_info, &dst_info)) {
            ESP_LOGE(TAG, "Fail to negotiate expect %s %dx%d %dfps, actual %s %dx%d %dfps",
                     esp_gmf_video_get_format_string((uint32_t)sink_info->format_id), (int)sink_info->width, (int)sink_info->height, sink_info->fps,
                     esp_gmf_video_get_format_string((uint32_t)dst_info.format_id), (int)dst_info.width, (int)dst_info.height, dst_info.fps);
            ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
        } else {
            ESP_LOGI(TAG, "Success to negotiate %d format:%s %dx%d %dfps",
                     path_idx,
                     esp_gmf_video_get_format_string((uint32_t)dst_info.format_id), (int)dst_info.width, (int)dst_info.height, dst_info.fps);
        }
        return ret;
    }

    const void *link = NULL;
    while (1) {
        esp_capture_video_info_t cur_src_info = dst_info;
        esp_gmf_pipeline_handle_t to = NULL;
        esp_gmf_pipeline_get_linked_pipeline(src->pipeline, &link, &to);
        if (to == NULL) {
            break;
        }
        esp_capture_gmf_pipeline_t *dst_pipe = capture_pipeline_get_matched(to, pipelines, pipeline_num);
        if (dst_pipe == NULL) {
            ESP_LOGE(TAG, "Pipeline wrong linkage");
            return ESP_CAPTURE_ERR_INVALID_ARG;
        }
        if ((dst_pipe->path_mask & path_mask) == 0) {
            continue;
        }
        ret = capture_negotiate_all_link(builder, pipelines, pipeline_num, dst_pipe, &cur_src_info, sink_arr, path_mask);
        if (ret != ESP_CAPTURE_ERR_OK) {
            return ret;
        }
    }
    return ret;
}

static esp_gmf_element_handle_t get_venc_element(esp_gmf_pipeline_handle_t pipeline)
{
    return capture_get_element_by_caps(pipeline, ESP_GMF_CAPS_VIDEO_ENCODER);
}

static esp_gmf_err_t get_venc_src_fmts(esp_gmf_element_handle_t self, uint32_t dst_codec,
                                       const uint32_t **src_fmts, uint8_t *src_fmts_num)
{
    return esp_gmf_video_param_get_src_fmts_by_codec(self, dst_codec, src_fmts, src_fmts_num);
}

static esp_gmf_err_t set_venc_dst_codec(esp_gmf_element_handle_t self, uint32_t dst_codec)
{
    return esp_gmf_video_param_set_dst_codec(self, dst_codec);
}

static esp_gmf_err_t set_venc_fmt(esp_gmf_element_handle_t self, esp_gmf_info_video_t *vid_info, uint32_t dst_codec)
{
    return esp_gmf_video_param_venc_preset(self, vid_info, dst_codec);
}

static esp_capture_err_t venc_nego_for_encoder(
    esp_gmf_element_handle_t src_element,
    esp_capture_gmf_pipeline_t *sel_pipeline,
    esp_capture_video_info_t *sel_sink,
    esp_capture_video_info_t *nego_info,
    esp_capture_video_info_t *src_info)
{
    esp_gmf_element_handle_t enc_element = get_venc_element(sel_pipeline->pipeline);
    if (enc_element == NULL) {
        ESP_LOGE(TAG, "Sink path not contain venc element");
        return ESP_CAPTURE_ERR_NOT_SUPPORTED;
    }
    const esp_capture_format_id_t *in_formats = NULL;
    uint8_t in_format_num = 0;
    int ret = get_venc_src_fmts(enc_element, sel_sink->format_id, (const uint32_t **)&in_formats, &in_format_num);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    for (int i = 0; i < in_format_num; i++) {
        nego_info->format_id = in_formats[i];
        int ret = capture_video_src_el_negotiate(src_element, nego_info, src_info);
        if (ret == ESP_CAPTURE_ERR_OK) {
            esp_gmf_info_video_t vid_info = {
                .format_id = (uint32_t)in_formats[i],
                .width = sel_sink->width,
                .height = sel_sink->height,
                .fps = sel_sink->fps,
            };
            set_venc_fmt(enc_element, &vid_info, (uint32_t)sel_sink->format_id);
            ESP_LOGI(TAG, "Set sel_path %d in %s out %s fps:%d", GET_PATH_IDX(sel_pipeline->path_mask),
                     esp_gmf_video_get_format_string(in_formats[i]), esp_gmf_video_get_format_string(sel_sink->format_id),
                     sel_sink->fps);
            sel_sink->format_id = in_formats[i];
            return ret;
        }
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t venc_nego_all_sink(uint8_t path_num, uint8_t *sel_path, esp_gmf_element_handle_t src_element,
                                            esp_capture_gmf_pipeline_t *sink_pipeline, esp_capture_video_info_t *sink_in,
                                            esp_capture_video_info_t *nego_info, esp_capture_video_info_t *src_info, bool *sel_bypass)
{
    // Negotiate directly with sink information
    bool src_encoded = video_need_encode(nego_info->format_id);
    int ret = capture_video_src_el_negotiate(src_element, nego_info, src_info);
    if (ret == ESP_CAPTURE_ERR_OK) {
        *sel_bypass = true;
        return ret;
    }
    if (src_encoded) {
        // When encoded, if negotiate fail try to negotiate with encoder input codecs
        ret = venc_nego_for_encoder(src_element, &sink_pipeline[*sel_path], &sink_in[*sel_path], nego_info, src_info);
        if (ret == ESP_CAPTURE_ERR_OK) {
            return ret;
        }
    }
    int cur_sel = *sel_path;
    // Try to negotiate with other path
    for (int i = 0; i < path_num; i++) {
        if (i == cur_sel) {
            continue;
        }
        if (sink_in[i].width * sink_in[i].height < nego_info->width * nego_info->height) {
            continue;
        }
        *sel_path = i;
        *nego_info = sink_in[i];
        src_encoded = video_need_encode(nego_info->format_id);
        if (ret == ESP_CAPTURE_ERR_OK) {
            *sel_bypass = src_encoded;
            return ret;
        }
        ret = venc_nego_for_encoder(src_element, &sink_pipeline[*sel_path], &sink_in[*sel_path], nego_info, src_info);
        if (ret == ESP_CAPTURE_ERR_OK) {
            return ret;
        }
    }
    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static esp_capture_err_t venc_nego_for_input_format(uint8_t path_num, uint8_t sel_path,
                                                    esp_gmf_element_handle_t src_element,
                                                    esp_capture_gmf_pipeline_t *sink_pipeline,
                                                    esp_capture_video_info_t *sink_in,
                                                    esp_capture_video_info_t *nego_info,
                                                    esp_capture_video_info_t *src_info)
{
    bool sel_bypass = false;
    int ret = venc_nego_all_sink(path_num, &sel_path, src_element, sink_pipeline, sink_in, nego_info, src_info, &sel_bypass);
    if (ret != ESP_CAPTURE_ERR_OK) {
        // If directly negotiate with all path failed, try to negotiate with any codec
        nego_info->format_id = ESP_CAPTURE_FMT_ID_ANY;
        ret = capture_video_src_el_negotiate(src_element, nego_info, src_info);
        if (ret != ESP_CAPTURE_ERR_OK) {
            return ret;
        }
    }

    for (int i = 0; i < path_num; i++) {
        if (i == sel_path) {
            if (sel_bypass) {
                esp_gmf_element_handle_t enc_element = get_venc_element(sink_pipeline[sel_path].pipeline);
                if (enc_element) {
                    set_venc_dst_codec(enc_element, (uint32_t)nego_info->format_id);
                }
                continue;
            }
            if (video_need_encode(sink_in[i].format_id) == false) {
                continue;
            }
        }
        esp_gmf_element_handle_t enc_element = get_venc_element(sink_pipeline[i].pipeline);
        if (enc_element) {
            bool need_encode = video_need_encode(sink_in[i].format_id);
            if (need_encode == false) {
                set_venc_dst_codec(enc_element, (uint32_t)sink_in[i].format_id);
            } else {
                const esp_capture_format_id_t *in_formats = NULL;
                uint8_t in_format_num = 0;
                get_venc_src_fmts(enc_element, sink_in[i].format_id, (const uint32_t **)&in_formats, &in_format_num);
                if (in_format_num == 0) {
                    ESP_LOGE(TAG, "Not support format %s", esp_gmf_video_get_format_string(sink_in[i].format_id));
                    return ESP_CAPTURE_ERR_NOT_SUPPORTED;
                }
                esp_gmf_info_video_t vid_info = {
                    .format_id = (uint32_t)in_formats[0],
                    .width = sink_in[i].width,
                    .height = sink_in[i].height,
                    .fps = sink_in[i].fps,
                };
                set_venc_fmt(enc_element, &vid_info, (uint32_t)sink_in[i].format_id);
                ESP_LOGI(TAG, "Set path %d in %s out %s", i, esp_gmf_video_get_format_string(in_formats[0]), esp_gmf_video_get_format_string(sink_in[i].format_id));
                sink_in[i].format_id = in_formats[0];
            }
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_video_pipeline_auto_negotiate(esp_capture_pipeline_builder_if_t *builder, uint8_t path_mask)
{
    int ret = ESP_CAPTURE_ERR_OK;
    esp_capture_gmf_pipeline_t *pipelines = NULL;
    esp_capture_video_info_t *enc_in_info = NULL;
    esp_capture_gmf_pipeline_t *enc_pipeline = NULL;
    do {
        uint8_t pipeline_num = 0;
        uint8_t path_num = 0;
        if (builder->get_pipelines(builder, NULL, &pipeline_num) != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Get pipeline failed");
            ret = ESP_CAPTURE_ERR_INVALID_ARG;
            break;
        }
        pipelines = (esp_capture_gmf_pipeline_t *)calloc(pipeline_num, sizeof(esp_capture_gmf_pipeline_t));
        if (pipelines == NULL) {
            ret = ESP_CAPTURE_ERR_NO_MEM;
            break;
        }
        ret = builder->get_pipelines(builder, pipelines, &pipeline_num);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Get pipeline failed");
            break;
        }
        path_num = capture_pipeline_get_path_num(pipelines, pipeline_num);
        if (path_num == 0) {
            ret = ESP_CAPTURE_ERR_INVALID_ARG;
            break;
        }
        enc_pipeline = (esp_capture_gmf_pipeline_t *)calloc(path_num, sizeof(esp_capture_gmf_pipeline_t));
        if (enc_pipeline == NULL) {
            ret = ESP_CAPTURE_ERR_NO_MEM;
            break;
        }
        enc_in_info = (esp_capture_video_info_t *)calloc(path_num, sizeof(esp_capture_video_info_t));
        if (enc_in_info == NULL) {
            ret = ESP_CAPTURE_ERR_NO_MEM;
            break;
        }
        // Do pipeline sort firstly
        capture_pipeline_sort(pipelines, pipeline_num);
        for (int i = 0; i < pipeline_num; i++) {
            // Skip source which not connected to the select path
            if ((pipelines[i].path_mask & path_mask) == 0) {
                continue;
            }
            // Iterate sources
            if (capture_pipeline_is_src(pipelines[i].pipeline, pipelines, pipeline_num) == false) {
                continue;
            }
            esp_capture_video_info_t max_caps = {};
            esp_capture_stream_info_t sink_cfg = {};
            uint8_t sel_path = ESP_CAPTURE_PIPELINE_NEGO_ALL_MASK;
            // Get source linked sinks
            for (int j = 0; j < pipeline_num; j++) {
                if (capture_pipeline_is_sink(pipelines[j].pipeline) == false) {
                    continue;
                }
                if ((pipelines[i].path_mask & pipelines[j].path_mask) == 0) {
                    continue;
                }
                uint8_t path_idx = GET_PATH_IDX(pipelines[i].path_mask & pipelines[j].path_mask);
                // Check which path sink belong and update sink config
                builder->get_sink_cfg(builder, path_idx, &sink_cfg);
                enc_in_info[path_idx] = sink_cfg.video_info;
                // TODO encoder element must put in sink pipeline
                enc_pipeline[path_idx] = pipelines[j];
                if (sink_cfg.video_info.format_id == ESP_CAPTURE_FMT_ID_NONE) {
                    continue;
                }
                bool need_encode = video_need_encode(sink_cfg.video_info.format_id);
                // High resolution prefer
                int resolution_diff = sink_cfg.video_info.width * sink_cfg.video_info.height - max_caps.width * max_caps.height;
                if (resolution_diff > 0) {
                    sel_path = path_idx;
                    max_caps.format_id = sink_cfg.video_info.format_id;
                } else if (resolution_diff == 0 && need_encode == false) {
                    // None encoder prefer for can bypass directly
                    max_caps.format_id = sink_cfg.video_info.format_id;
                    sel_path = path_idx;
                }
                MAX_VID_SINK_CFG(max_caps, sink_cfg);
            }
            if (sel_path == ESP_CAPTURE_PIPELINE_NEGO_ALL_MASK) {
                continue;
            }
            esp_gmf_element_handle_t src_element = NULL;
            esp_gmf_pipeline_get_el_by_name(pipelines[i].pipeline, "vid_src", &src_element);
            if (src_element == NULL) {
                ESP_LOGE(TAG, "Source pipeline must contain vid_src element");
                continue;
            }
            ESP_LOGD(TAG, "Start to nego for input format %s %dx%d",
                     esp_gmf_video_get_format_string((uint32_t)max_caps.format_id), (int)max_caps.width,
                     (int)max_caps.height);
            esp_capture_video_info_t src_info = {};
            ret = venc_nego_for_input_format(path_num, sel_path, src_element,
                                             enc_pipeline, enc_in_info, &max_caps, &src_info);
            if (ret != ESP_CAPTURE_ERR_OK) {
                // Directly negotiate OK
                ESP_LOGE(TAG, "Fail to negotiate source format");
                break;
            }
            ret = capture_negotiate_all_link(builder, pipelines, pipeline_num, &pipelines[i], &src_info, enc_in_info, path_mask);
            if (ret != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Fail to negotiate links");
                break;
            }
        }
    } while (0);
    if (enc_pipeline) {
        capture_free(enc_pipeline);
    }
    if (pipelines) {
        capture_free(pipelines);
    }
    if (enc_in_info) {
        capture_free(enc_in_info);
    }
    return ret;
}
