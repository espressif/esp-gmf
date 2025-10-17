/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <esp_gmf_pipeline.h>
#include <esp_gmf_element.h>
#include <esp_gmf_node.h>
#include <esp_gmf_audio_param.h>
#include <esp_gmf_caps_def.h>
#include "audio_render_proc.h"
#include "audio_render_pipeline.h"
#include "esp_log.h"

#define TAG "AUD_RENDER_PIPE"

#define ELEMS(arr)                          (sizeof(arr) / sizeof(arr[0]))
#define SAMPLE_SIZE_PER_SECOND(sample_info) (sample_info.sample_rate * sample_info.bits_per_sample * sample_info.channel / 8)
// #define ENABLE_PIPELINE_PRINT

static inline void add_proc(const char *pipeline_tag[], const char *proc_tag, uint8_t* element_num)
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

static const char* get_element_tag_by_caps(esp_gmf_pool_handle_t pool, uint64_t caps_cc)
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

static esp_audio_render_err_t element_setting(esp_gmf_element_handle_t element, esp_audio_render_sample_info_t *cur_info,
                                              esp_audio_render_sample_info_t *sink_info)
{
    const esp_gmf_cap_t *caps = NULL;
    esp_gmf_element_get_caps(element, &caps);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    for (; caps; caps = caps->next) {
        if (caps->cap_eightcc == ESP_GMF_CAPS_AUDIO_BIT_CONVERT) {
            ret = esp_gmf_audio_param_set_dest_bits(element, sink_info->bits_per_sample);
            cur_info->bits_per_sample = sink_info->bits_per_sample;
        } else if (caps->cap_eightcc == ESP_GMF_CAPS_AUDIO_CHANNEL_CONVERT) {
            ret = esp_gmf_audio_param_set_dest_ch(element, sink_info->channel);
            cur_info->channel = sink_info->channel;
        } else if (caps->cap_eightcc == ESP_GMF_CAPS_AUDIO_RATE_CONVERT) {
            ret = esp_gmf_audio_param_set_dest_rate(element, sink_info->sample_rate);
            cur_info->sample_rate = sink_info->sample_rate;
        }
        if (ret != ESP_GMF_ERR_OK) {
            break;
        }
    }
    return ret == ESP_GMF_ERR_OK ? ESP_AUDIO_RENDER_ERR_OK : ESP_AUDIO_RENDER_ERR_NOT_SUPPORTED;
}

static esp_audio_render_err_t apply_settings(esp_gmf_pipeline_handle_t pipeline, audio_render_pipeline_cfg_t *proc_cfg)
{
    esp_audio_render_sample_info_t cur_info = proc_cfg->in_sample_info;
    esp_gmf_element_handle_t head_element = NULL;
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_pipeline_get_head_el(pipeline, &element);
    head_element = element;
    for (; element; esp_gmf_pipeline_get_next_el(pipeline, element, &element)) {
        esp_audio_render_err_t ret = element_setting(element, &cur_info, &proc_cfg->out_sample_info);
        if (ret != ESP_AUDIO_RENDER_ERR_OK) {
            return ret;
        }
    }
    // Report info for first element
    esp_gmf_info_sound_t snd_info = {
        .sample_rates = proc_cfg->in_sample_info.sample_rate,
        .channels = proc_cfg->in_sample_info.channel,
        .bits = proc_cfg->in_sample_info.bits_per_sample,
    };
    esp_gmf_err_t ret = esp_gmf_pipeline_report_info(pipeline, ESP_GMF_INFO_SOUND, &snd_info, sizeof(snd_info));
    if (ret != ESP_GMF_ERR_OK) {
        return ESP_AUDIO_RENDER_ERR_NOT_SUPPORTED;
    }

    // Open element in serial
    element = head_element;
    for (; element; esp_gmf_pipeline_get_next_el(pipeline, element, &element)) {
        ret = esp_gmf_element_process_open(element, NULL);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to open element %s", OBJ_GET_TAG(element));
            break;
        }
    }
    return ret == ESP_GMF_ERR_OK ? ESP_AUDIO_RENDER_ERR_OK : ESP_AUDIO_RENDER_ERR_NOT_SUPPORTED;
}

static void audio_render_pipeline_print(const char *pipeline_tag[], uint8_t element_num)
{
#ifdef ENABLE_PIPELINE_PRINT
    ESP_LOGI(TAG, "Auto Generate pipeline");
    for (int i = 0; i < element_num; i++) {
        printf("%s -> ", pipeline_tag[i]);
    }
    printf("\n");
#endif
}

static uint8_t audio_render_optimize_pipeline(audio_render_pipeline_cfg_t *proc_cfg, const char *pipeline_tag[])
{
    uint8_t element_num = 0;

    // Add Proc which decrease data size
    if (proc_cfg->in_sample_info.channel > proc_cfg->out_sample_info.channel) {
        const char *tag = get_element_tag_by_caps(proc_cfg->pool, ESP_GMF_CAPS_AUDIO_CHANNEL_CONVERT);
        add_proc(pipeline_tag, tag, &element_num);
    }
    if (proc_cfg->in_sample_info.bits_per_sample > proc_cfg->out_sample_info.bits_per_sample) {
        const char *tag = get_element_tag_by_caps(proc_cfg->pool, ESP_GMF_CAPS_AUDIO_BIT_CONVERT);
        add_proc(pipeline_tag, tag, &element_num);
    }
    uint32_t in_sample_size = SAMPLE_SIZE_PER_SECOND(proc_cfg->in_sample_info);
    uint32_t out_sample_size = SAMPLE_SIZE_PER_SECOND(proc_cfg->out_sample_info);
    if (proc_cfg->in_sample_info.sample_rate > proc_cfg->out_sample_info.sample_rate ||
        in_sample_size > out_sample_size) {
        const char *tag = get_element_tag_by_caps(proc_cfg->pool, ESP_GMF_CAPS_AUDIO_RATE_CONVERT);
        add_proc(pipeline_tag, tag, &element_num);
    }

    // Add Normal procs
    for (int i = 0; i < proc_cfg->proc_num; i++) {
        const char *tag = OBJ_GET_TAG(proc_cfg->proc_elements[i]);
        add_proc(pipeline_tag, tag, &element_num);
    }
    if (proc_cfg->in_sample_info.sample_rate < proc_cfg->out_sample_info.sample_rate) {
        const char *tag = get_element_tag_by_caps(proc_cfg->pool, ESP_GMF_CAPS_AUDIO_RATE_CONVERT);
        add_proc(pipeline_tag, tag, &element_num);
    }

    // Add Proc which decrease data size firstly
    if (proc_cfg->in_sample_info.bits_per_sample < proc_cfg->out_sample_info.bits_per_sample) {
        const char *tag = get_element_tag_by_caps(proc_cfg->pool, ESP_GMF_CAPS_AUDIO_BIT_CONVERT);
        add_proc(pipeline_tag, tag, &element_num);
    }
    if (proc_cfg->in_sample_info.channel < proc_cfg->out_sample_info.channel) {
        const char *tag = get_element_tag_by_caps(proc_cfg->pool, ESP_GMF_CAPS_AUDIO_CHANNEL_CONVERT);
        add_proc(pipeline_tag, tag, &element_num);
    }

    audio_render_pipeline_print(pipeline_tag, element_num);
    return element_num;
}

static esp_gmf_element_handle_t *get_proc_element( esp_gmf_element_handle_t proc_elements[], uint8_t proc_num, const char *el_name)
{
    for (int i = 0; i < proc_num; i++) {
        if (strcmp(OBJ_GET_TAG(proc_elements[i]), el_name) == 0) {
            return proc_elements[i];
        }
    }
    return NULL;
}

static void audio_render_unregister_el(esp_gmf_pipeline_handle_t pipeline, esp_gmf_element_handle_t el)
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
                cur_el->next = NULL;
            }
            break;
        }
        pre_el = cur_el;
        cur_el = cur_el->next;
    }
}

static esp_gmf_err_t audio_render_new_pipeline(esp_gmf_pool_handle_t handle,
                                               const char *el_name[], int num_of_el_name,
                                               esp_gmf_element_handle_t *proc_elements, uint8_t proc_num,
                                               esp_gmf_pipeline_handle_t *pipeline)
{
    if (num_of_el_name < 1) {
        ESP_LOGE(TAG, "Failed to create pipeline for no element");
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_pipeline_t *pl = NULL;
    esp_gmf_pipeline_create(&pl);
    ESP_GMF_MEM_CHECK(TAG, pl, return ESP_GMF_ERR_MEMORY_LACK);

    esp_gmf_obj_handle_t new_prev_el_obj = NULL;
    esp_gmf_obj_handle_t new_first_el_obj = NULL;
    int ret = ESP_GMF_ERR_OK;
    // Link the elements
    for (int i = 0; i < num_of_el_name; ++i) {
        // Check whether already created
        esp_gmf_element_handle_t new_el = get_proc_element(proc_elements, proc_num, el_name[i]);
        if (new_el == NULL) {
            ret = esp_gmf_pool_new_element(handle, el_name[i], &new_el);
        }
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Fail to create element");
            break;
        }
        if (i == 0) {
            new_first_el_obj = new_el;
        } else {
            esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(NULL, NULL, NULL, NULL,
                                               (ESP_GMF_ELEMENT_GET(new_first_el_obj)->out_attr.data_size), ESP_GMF_MAX_DELAY);
            if (out_port == NULL) {
                ret = ESP_GMF_ERR_MEMORY_LACK;
                break;
            }
            esp_gmf_element_register_out_port((esp_gmf_element_handle_t)new_prev_el_obj, out_port);
            esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(NULL, NULL, NULL, NULL,
                                               (ESP_GMF_ELEMENT_GET(new_first_el_obj)->in_attr.data_size), ESP_GMF_MAX_DELAY);
            if (in_port == NULL) {
                ret = ESP_GMF_ERR_MEMORY_LACK;
                break;
            }
            esp_gmf_element_register_in_port(new_el, in_port);
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
            audio_render_unregister_el(pl, proc_elements[i]);
        }
        esp_gmf_pipeline_destroy(pl);
    }
    return ESP_GMF_ERR_MEMORY_LACK;
}

esp_gmf_element_handle_t audio_render_create_element(esp_gmf_pool_handle_t pool, esp_audio_render_proc_type_t proc_type)
{
    const char *tag = get_element_tag_by_caps(pool, (uint64_t)proc_type);
    if (tag == NULL) {
        char str[9];
        gmf_eightcc_to_str(proc_type, str);
        ESP_LOGE(TAG, "Not found element for %s", str);
        return NULL;
    }
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_err_t ret = esp_gmf_pool_new_element(pool, tag, &element);
    if (ret == ESP_GMF_ERR_OK) {
        return element;
    }
    return NULL;
}

esp_audio_render_err_t audio_render_pipeline_open(audio_render_pipeline_cfg_t *proc_cfg,
                                                  esp_gmf_pipeline_handle_t *pipeline)
{
    // Optimized element order and create pipeline
    const char *pipeline_tag[proc_cfg->proc_num + 3];
    uint8_t element_num = audio_render_optimize_pipeline(proc_cfg, pipeline_tag);
    int ret = audio_render_new_pipeline(proc_cfg->pool, pipeline_tag, element_num,
                                        proc_cfg->proc_elements, proc_cfg->proc_num,
                                        pipeline);
    if (ret != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to new pipeline ret %d", ret);
        if (proc_cfg->in_port) {
            esp_gmf_port_deinit(proc_cfg->in_port);
        }
        if (proc_cfg->out_port) {
            esp_gmf_port_deinit(proc_cfg->out_port);
        }
        return ESP_AUDIO_RENDER_ERR_NO_RESOURCE;
    }
    esp_gmf_element_handle_t head_element = ESP_GMF_PIPELINE_GET_FIRST_ELEMENT((*pipeline));
    esp_gmf_element_handle_t tail_element = ESP_GMF_PIPELINE_GET_LAST_ELEMENT((*pipeline));
    esp_gmf_element_register_in_port(head_element, proc_cfg->in_port);
    esp_gmf_element_register_out_port(tail_element, proc_cfg->out_port);

    // Do settings and open
    ret = apply_settings(*pipeline, proc_cfg);
    if (ret != ESP_AUDIO_RENDER_ERR_OK) {
        esp_gmf_pipeline_destroy(*pipeline);
        *pipeline = NULL;
        return ret;
    }
    return ESP_AUDIO_RENDER_ERR_OK;
}

esp_gmf_element_handle_t audio_render_pipeline_get_element(esp_gmf_pipeline_handle_t pipeline, esp_audio_render_proc_type_t proc_type)
{
    if (pipeline == NULL || proc_type == ESP_AUDIO_RENDER_PROC_NONE) {
        ESP_LOGE(TAG, "Invalid arg for pipeline %p", pipeline);
        return NULL;
    }
    return get_element_by_caps(pipeline, (uint64_t)proc_type);
}

esp_audio_render_err_t audio_render_pipeline_close(esp_gmf_pipeline_handle_t pipeline,
                                                   esp_gmf_element_handle_t *kept_elements, uint8_t kept_num)
{
    // Close elements in serial
    if (pipeline == NULL) {
        ESP_LOGE(TAG, "Invalid arg for pipeline %p", pipeline);
        return ESP_AUDIO_RENDER_ERR_INVALID_ARG;
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
    // Un-register port
    esp_gmf_element_handle_t head_element = ESP_GMF_PIPELINE_GET_FIRST_ELEMENT(pipeline);
    esp_gmf_element_handle_t tail_element = ESP_GMF_PIPELINE_GET_LAST_ELEMENT(pipeline);
    esp_gmf_element_unregister_in_port(head_element, NULL);
    esp_gmf_element_unregister_out_port(tail_element, NULL);
    // Keep pre-defined elements
    for (int i = 0; i < kept_num; i++) {
        audio_render_unregister_el(pipeline, kept_elements[i]);
        esp_gmf_element_unregister_in_port(kept_elements[i], NULL);
        esp_gmf_element_unregister_out_port(kept_elements[i], NULL);
    }
    esp_gmf_pipeline_destroy(pipeline);
    return ESP_AUDIO_RENDER_ERR_OK;
}
