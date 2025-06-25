/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "capture_pipeline_nego.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_audio_element.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_audio_enc.h"
#include "capture_share_copy_el.h"
#include "esp_log.h"
#include "capture_pipeline_builder.h"
#include "capture_pipeline_utils.h"
#include "capture_audio_src_el.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_audio_param.h"

#define TAG "AUD_PIPE_NEGO"

#define SAMPLE_SIZE(info)    ((info).channel * (info).bits_per_sample >> 3)
#define AUDIO_FRAME_DURATION (20)  // Unit ms

static esp_capture_err_t capture_path_apply_setting(esp_gmf_element_handle_t element, esp_capture_audio_info_t *sink_info,
                                                    esp_capture_audio_info_t *dst_info)
{
    const esp_gmf_cap_t *caps = NULL;
    esp_gmf_element_get_caps(element, &caps);

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    for (; caps; caps = caps->next) {
        if (caps->cap_eightcc == ESP_GMF_CAPS_AUDIO_ENCODER) {
            esp_gmf_info_sound_t snd_info = {
                .format_id = sink_info->format_id,
                .sample_rates = sink_info->sample_rate,
                .channels = sink_info->channel,
                .bits = sink_info->bits_per_sample,
            };
            ret = esp_gmf_audio_enc_reconfig_by_sound_info(element, &snd_info);
            if (ret != ESP_GMF_ERR_OK) {
                break;
            }
            dst_info->format_id = sink_info->format_id;
            continue;
        }
        if (caps->cap_eightcc == ESP_GMF_CAPS_AUDIO_BIT_CONVERT) {
            ret = esp_gmf_audio_param_set_dest_bits(element, sink_info->bits_per_sample);
            dst_info->bits_per_sample = sink_info->bits_per_sample;
        } else if (caps->cap_eightcc == ESP_GMF_CAPS_AUDIO_CHANNEL_CONVERT) {
            ret = esp_gmf_audio_param_set_dest_ch(element, sink_info->channel);
            dst_info->channel = sink_info->channel;
        } else if (caps->cap_eightcc == ESP_GMF_CAPS_AUDIO_RATE_CONVERT) {
            ret = esp_gmf_audio_param_set_dest_rate(element, sink_info->sample_rate);
            dst_info->sample_rate = sink_info->sample_rate;
        }
        if (ret != ESP_GMF_ERR_OK) {
            break;
        }
    }
    return ret == ESP_GMF_ERR_OK ? ESP_CAPTURE_ERR_OK : ESP_CAPTURE_ERR_NOT_SUPPORTED;
}

static bool capture_negotiate_ok(esp_capture_audio_info_t *a, esp_capture_audio_info_t *b)
{
    return (a->format_id == b->format_id && a->sample_rate == b->sample_rate && a->channel == b->channel && a->bits_per_sample == b->bits_per_sample);
}

static esp_capture_err_t capture_do_negotiate(esp_capture_pipeline_builder_if_t *builder, esp_capture_gmf_pipeline_t *pipeline,
                                              esp_capture_audio_info_t *src_info, esp_capture_audio_info_t *dst_info)
{
    int path_idx = GET_PATH_IDX(pipeline->path_mask);
    esp_capture_stream_info_t sink_cfg = {};
    for (int i = 0; i <= path_idx; i++) {
        if ((pipeline->path_mask & (1 << i)) == 0) {
            continue;
        }
        builder->get_sink_cfg(builder, i, &sink_cfg);
        if (sink_cfg.audio_info.format_id) {
            ESP_LOGI(TAG, "Path mask %d select sink:%d format %d", pipeline->path_mask, i, sink_cfg.audio_info.format_id);
            break;
        }
    }
    if (sink_cfg.audio_info.format_id == ESP_CAPTURE_FMT_ID_NONE) {
        // This sink is not enable yet
        return ESP_CAPTURE_ERR_OK;
    }
    int ret = esp_capture_audio_pipeline_auto_setup(pipeline->pipeline, src_info, &sink_cfg.audio_info, dst_info);
    if (ret == ESP_CAPTURE_ERR_OK) {
        if (capture_pipeline_is_sink(pipeline->pipeline) && !capture_negotiate_ok(&sink_cfg.audio_info, dst_info)) {
            ESP_LOGE(TAG, "Fail to negotiate expect %d %dHZ %dch, actual %d %dHZ %dch",
                     sink_cfg.audio_info.format_id, (int)sink_cfg.audio_info.sample_rate, sink_cfg.audio_info.channel,
                     dst_info->format_id, (int)dst_info->sample_rate, dst_info->channel);
            ret = ESP_CAPTURE_ERR_NOT_SUPPORTED;
        }
    }
    return ret;
}

static esp_capture_err_t capture_negotiate_all_link(esp_capture_pipeline_builder_if_t *builder,
                                                    esp_capture_gmf_pipeline_t *pipelines,
                                                    uint8_t pipeline_num,
                                                    esp_capture_gmf_pipeline_t *src,
                                                    esp_capture_audio_info_t *src_info,
                                                    uint8_t path_mask)
{
    esp_gmf_pipeline_handle_t src_pipe = src->pipeline;
    const void *link = NULL;
    esp_capture_audio_info_t dst_info = *src_info;
    // TODO if path belong to 2 path, prefer to negotiate last path
    int ret = capture_do_negotiate(builder, src, src_info, &dst_info);
    if (ret != ESP_CAPTURE_ERR_OK) {
        return ret;
    }
    while (1) {
        esp_capture_audio_info_t cur_src_info = dst_info;
        esp_gmf_pipeline_handle_t to = NULL;
        esp_gmf_pipeline_get_linked_pipeline(src_pipe, &link, &to);
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
        ret = capture_negotiate_all_link(builder, pipelines, pipeline_num, dst_pipe, &cur_src_info, path_mask);
        if (ret != ESP_CAPTURE_ERR_OK) {
            return ret;
        }
    }
    return ret;
}

static inline int get_default_in_sample(esp_capture_audio_info_t *info)
{
    return AUDIO_FRAME_DURATION * info->sample_rate / 1000;
}

static inline int negotiate_in_sample(esp_capture_pipeline_builder_if_t *builder, uint8_t path_mask,
                                      esp_capture_gmf_pipeline_t *pipelines, uint8_t pipeline_num,
                                      esp_capture_audio_info_t *src_info)
{
    // After element open, get frame size from sinks
    int min_sample_size = 0;
    for (int i = 0; i < pipeline_num; i++) {
        if (capture_pipeline_is_sink(pipelines[i].pipeline) == false) {
            continue;
        }
        if ((path_mask & pipelines[i].path_mask) == 0) {
            continue;
        }
        esp_gmf_element_handle_t enc_element;
        enc_element = capture_get_element_by_caps(pipelines[i].pipeline, ESP_GMF_CAPS_AUDIO_ENCODER);
        if (enc_element == NULL) {
            continue;
        }
        uint32_t in_sample_size = 0, out_sample_size = 0;
        esp_gmf_audio_enc_get_frame_size(enc_element, &in_sample_size, &out_sample_size);
        if (in_sample_size == 0) {
            break;
        }
        uint8_t path_idx = GET_PATH_IDX(path_mask & pipelines[i].path_mask);
        esp_capture_stream_info_t sink_cfg = {};
        builder->get_sink_cfg(builder, path_idx, &sink_cfg);
        if (sink_cfg.audio_info.sample_rate == 0) {
            continue;
        }
        int sink_frame_size = SAMPLE_SIZE(sink_cfg.audio_info);
        in_sample_size = in_sample_size / sink_frame_size * src_info->sample_rate / sink_cfg.audio_info.sample_rate;
        if (min_sample_size == 0 || in_sample_size < min_sample_size) {
            min_sample_size = in_sample_size;
        }
    }
    if (min_sample_size == 0) {
        min_sample_size = get_default_in_sample(src_info);
    }
    return min_sample_size;
}

esp_capture_err_t esp_capture_audio_pipeline_auto_setup(void *h, esp_capture_audio_info_t *src_info,
                                                        esp_capture_audio_info_t *sink_info, esp_capture_audio_info_t *dst_info)
{
    if (h == NULL || src_info == NULL || sink_info == NULL || dst_info == NULL) {
        return ESP_CAPTURE_ERR_INVALID_ARG;
    }
    esp_gmf_pipeline_handle_t pipeline = (esp_gmf_pipeline_handle_t)h;
    esp_gmf_element_handle_t element = NULL;

    bool reported = false;
    *dst_info = *src_info;

    esp_gmf_pipeline_get_head_el(pipeline, &element);
    for (; element; esp_gmf_pipeline_get_next_el(pipeline, element, &element)) {
        const char *tag = OBJ_GET_TAG(element);
        // Skip for copier element
        if (strcmp(tag, "share_copier") == 0) {
            continue;
        }
        if (reported == false) {
            esp_gmf_info_sound_t snd_info = {
                .sample_rates = dst_info->sample_rate,
                .channels = dst_info->channel,
                .bits = dst_info->bits_per_sample,
            };
            esp_gmf_pipeline_report_info(pipeline, ESP_GMF_INFO_SOUND, &snd_info, sizeof(snd_info));
            reported = true;
        }
        int ret = capture_path_apply_setting(element, sink_info, dst_info);
        if (ret != ESP_CAPTURE_ERR_OK) {
            ESP_LOGE(TAG, "Fail to apply setting ret:%d", ret);
            return ret;
        }
    }
    return ESP_CAPTURE_ERR_OK;
}

esp_capture_err_t esp_capture_audio_pipeline_auto_negotiate(esp_capture_pipeline_builder_if_t *builder, uint8_t path_mask)
{
    int ret = ESP_CAPTURE_ERR_OK;
    esp_capture_gmf_pipeline_t *pipelines = NULL;
    do {
        uint8_t pipeline_num = 0;
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
        capture_pipeline_sort(pipelines, pipeline_num);
        for (int i = 0; i < pipeline_num; i++) {
            if ((pipelines[i].path_mask & path_mask) == 0) {
                continue;
            }
            // Iterate source
            if (capture_pipeline_is_src(pipelines[i].pipeline, pipelines, pipeline_num) == false) {
                continue;
            }
            esp_capture_audio_info_t max_caps = {
                .format_id = ESP_CAPTURE_FMT_ID_PCM,
            };
            uint8_t connected_sink = 0;
            esp_capture_stream_info_t sink_cfg = {};
            // Get source linked sinks
            for (int j = 0; j < pipeline_num; j++) {
                if (capture_pipeline_is_sink(pipelines[j].pipeline) == false) {
                    continue;
                }
                if ((pipelines[i].path_mask & pipelines[j].path_mask) == 0) {
                    continue;
                }
                uint8_t path_idx = GET_PATH_IDX(pipelines[i].path_mask & pipelines[j].path_mask);
                // Check sink pipe line is which path
                builder->get_sink_cfg(builder, path_idx, &sink_cfg);
                // Make sure codec is set
                if (sink_cfg.audio_info.format_id) {
                    MAX_AUD_SINK_CFG(max_caps, sink_cfg);
                    connected_sink++;
                    if (sink_cfg.audio_info.format_id != ESP_CAPTURE_FMT_ID_PCM) {
                        max_caps.format_id = sink_cfg.audio_info.format_id;
                    }
                    ESP_LOGD(TAG, "Sink %d format %d", connected_sink, sink_cfg.audio_info.format_id);
                }
            }
            if (connected_sink == 0) {
                continue;
            }
            esp_gmf_element_handle_t src_element = NULL;
            esp_gmf_pipeline_get_el_by_name(pipelines[i].pipeline, "aud_src", &src_element);
            if (src_element == NULL) {
                ESP_LOGE(TAG, "Source pipeline must contain aud_src element");
                continue;
            }
            esp_capture_audio_info_t src_info = {};
            ret = capture_audio_src_el_negotiate(src_element, &max_caps, &src_info);
            if (ret != ESP_CAPTURE_ERR_OK && max_caps.format_id != ESP_CAPTURE_FMT_ID_PCM) {
                max_caps.format_id = ESP_CAPTURE_FMT_ID_PCM;
                ret = capture_audio_src_el_negotiate(src_element, &max_caps, &src_info);
            }
            ESP_LOGI(TAG, "Negotiate return %d src_format:%d sample_rate:%d\n",
                     ret, src_info.format_id, (int)src_info.sample_rate);
            if (ret != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Fail to negotiate source");
                break;
            }
            ret = capture_negotiate_all_link(builder, pipelines, pipeline_num, &pipelines[i], &src_info, path_mask);
            if (ret != ESP_CAPTURE_ERR_OK) {
                ESP_LOGE(TAG, "Fail to negotiate links");
                break;
            }
            int min_sample_size = negotiate_in_sample(builder, pipelines[i].path_mask, pipelines,
                                                      pipeline_num, &src_info);
            capture_audio_src_el_set_in_frame_samples(src_element, min_sample_size);
        }
    } while (0);
    if (pipelines) {
        capture_free(pipelines);
    }
    return ret;
}
