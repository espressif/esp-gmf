/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "capture_pipeline_utils.h"
#include "esp_log.h"

#define TAG "CAPTURE_UTILS"

uint8_t capture_pipeline_get_path_num(esp_capture_gmf_pipeline_t *pipeline, uint8_t num)
{
    uint8_t path_num = 0;
    uint8_t max_mask = 0;
    for (int i = 0; i < num; i++) {
        if (pipeline->path_mask > max_mask) {
            max_mask = pipeline->path_mask;
        }
    }
    path_num = sizeof(int) * 8 - __builtin_clz(max_mask);
    return path_num;
}

bool capture_pipeline_is_sink(esp_gmf_pipeline_handle_t pipeline)
{
    esp_gmf_pipeline_handle_t to = NULL;
    const void *link = NULL;
    esp_gmf_pipeline_get_linked_pipeline(pipeline, &link, &to);
    return (to == NULL);
}

uint8_t capture_pipeline_get_link_num(esp_gmf_pipeline_handle_t pipeline)
{
    uint8_t link_num = 0;
    const void *link = NULL;
    while (1) {
        esp_gmf_pipeline_handle_t to = NULL;
        esp_gmf_pipeline_get_linked_pipeline(pipeline, &link, &to);
        if (to == NULL) {
            break;
        }
        link_num++;
    }
    return link_num;
}

void capture_pipeline_get_all_linked_src_num(uint8_t *connect_count, esp_capture_gmf_pipeline_t *pipelines, uint8_t num)
{
    for (int i = 0; i < num; i++) {
        const void *link = NULL;
        while (1) {
            esp_gmf_pipeline_handle_t to = NULL;
            esp_gmf_pipeline_get_linked_pipeline(pipelines[i].pipeline, &link, &to);
            if (to == NULL) {
                break;
            }
            for (int j = 0; j < num; j++) {
                if (j != i && pipelines[j].pipeline == to) {
                    connect_count[j]++;
                    break;
                }
            }
        }
    }
}

bool capture_pipeline_is_src(esp_gmf_pipeline_handle_t pipeline, esp_capture_gmf_pipeline_t *pipelines, uint8_t num)
{
    // No one link to it
    for (int i = 0; i < num; i++) {
        esp_gmf_pipeline_handle_t cur = pipelines[i].pipeline;
        if (cur == pipeline) {
            continue;
        }
        const void *link = NULL;
        esp_gmf_pipeline_handle_t to = NULL;
        do {
            esp_gmf_pipeline_get_linked_pipeline(cur, &link, &to);
            if (to == pipeline) {
                return false;
            }
        } while (to);
    }
    return true;
}

// Topological sort using linked relationships
esp_capture_err_t capture_pipeline_sort(esp_capture_gmf_pipeline_t *pipelines, uint8_t num)
{
    esp_capture_gmf_pipeline_t *sorted_pipe = NULL;
    bool *visited = NULL;
    uint8_t *connect_count = NULL;
    int ret = ESP_CAPTURE_ERR_NO_MEM;
    do {
        sorted_pipe = (esp_capture_gmf_pipeline_t *)capture_calloc(1, num * sizeof(esp_capture_gmf_pipeline_t));
        if (sorted_pipe == NULL) {
            break;
        }
        visited = capture_calloc(1, num * sizeof(bool));
        if (visited == NULL) {
            break;
        }
        connect_count = capture_calloc(1, num * sizeof(uint8_t));
        if (connect_count == NULL) {
            break;
        }
        uint8_t sorted_idx = 0;
        // Add source pipeline firstly
        capture_pipeline_get_all_linked_src_num(connect_count, pipelines, num);
        for (int i = 0; i < num; i++) {
            if (connect_count[i] == 0) {
                sorted_pipe[sorted_idx++] = pipelines[i];
                visited[i] = true;
            }
        }
        uint8_t check_start = 0;
        while (check_start < sorted_idx) {
            esp_gmf_pipeline_handle_t cur = sorted_pipe[check_start].pipeline;
            check_start++;
            const void *link = NULL;
            while (1) {
                esp_gmf_pipeline_handle_t to = NULL;
                esp_gmf_pipeline_get_linked_pipeline(cur, &link, &to);
                if (to == NULL) {
                    break;
                }
                for (int j = 0; j < num; j++) {
                    if (visited[j] == false && pipelines[j].pipeline == to) {
                        if (connect_count[j] > 0) {
                            connect_count[j]--;
                        }
                        if (connect_count[j] == 0) {
                            sorted_pipe[sorted_idx++] = pipelines[j];
                            visited[j] = true;
                        }
                    }
                }
            }
        }
        if (sorted_idx != num) {
            ESP_LOGE(TAG, "Pipeline wrongly configured");
            ret = ESP_CAPTURE_ERR_NOT_FOUND;
            break;
        }
        memcpy(pipelines, sorted_pipe, sorted_idx * sizeof(esp_capture_gmf_pipeline_t));
        ret = ESP_CAPTURE_ERR_OK;
    } while (0);
    if (sorted_pipe) {
        capture_free(sorted_pipe);
    }
    if (visited) {
        capture_free(visited);
    }
    if (connect_count) {
        capture_free(connect_count);
    }
    return ret;
}

bool capture_pipeline_verify(esp_capture_gmf_pipeline_t *pipelines, uint8_t num, uint8_t path)
{
    uint8_t sink_num = 0;
    uint8_t path_mask = (1 << path);
    // Path should only have one sink
    for (int i = 0; i < num; i++) {
        if ((pipelines[i].path_mask & path_mask) == 0) {
            continue;
        }
        if (capture_pipeline_is_sink(pipelines[i].pipeline)) {
            sink_num++;
        }
    }
    return (sink_num == 1);
}

esp_capture_gmf_pipeline_t *capture_pipeline_get_matched(esp_gmf_pipeline_handle_t h, esp_capture_gmf_pipeline_t *pipelines, uint8_t num)
{
    for (int i = 0; i < num; i++) {
        if (pipelines[i].pipeline == h) {
            return &pipelines[i];
        }
    }
    return NULL;
}

esp_gmf_element_handle_t capture_get_element_by_caps(esp_gmf_pipeline_handle_t pipeline, uint64_t caps_cc)
{
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_pipeline_get_head_el(pipeline, &element);
    for (; element; esp_gmf_pipeline_get_next_el(pipeline, element, &element)) {
        const esp_gmf_cap_t *caps = NULL;
        esp_gmf_element_get_caps(element, &caps);
        for (; caps; caps = caps->next) {
            if (caps->cap_eightcc == caps_cc) {
                return element;
            }
        }
    }
    return NULL;
}
