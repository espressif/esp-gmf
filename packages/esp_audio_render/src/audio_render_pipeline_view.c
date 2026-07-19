/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include "sdkconfig.h"

#if CONFIG_ESP_AUDIO_RENDER_PIPELINE_VIEW

#include "audio_render_pipeline_view.h"
#include "esp_gmf_pipeline_view.h"

#define AUDIO_RENDER_PIPELINE_NAME_MAX (64)

static esp_gmf_pipeline_view_t *s_pipeline_view;

static esp_gmf_err_t format_pipeline_name(uint8_t pipeline_num, uint8_t index, char *name, size_t name_size)
{
    if (pipeline_num == 0 || index >= pipeline_num || name == NULL || name_size == 0) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    int length = 0;
    if (pipeline_num > 1 && index == pipeline_num - 1) {
        length = snprintf(name, name_size, "audio_render/mixed");
    } else {
        length = snprintf(name, name_size, "audio_render/stream-%u", (unsigned int)index);
    }
    return length > 0 && length < (int)name_size ? ESP_GMF_ERR_OK : ESP_GMF_ERR_INVALID_ARG;
}

esp_gmf_err_t audio_render_pipeline_view_init(uint8_t pipeline_num)
{
    if (pipeline_num == 0) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (s_pipeline_view != NULL) {
        return ESP_GMF_ERR_ALREADY_EXISTS;
    }
    esp_gmf_err_t ret = esp_gmf_pipeline_view_init(&s_pipeline_view);
    if (ret != ESP_GMF_ERR_OK) {
        return ret;
    }
    char name[AUDIO_RENDER_PIPELINE_NAME_MAX];
    for (int i = 0; i < pipeline_num; i++) {
        ret = format_pipeline_name(pipeline_num, i, name, sizeof(name));
        if (ret != ESP_GMF_ERR_OK) {
            goto cleanup;
        }
        ret = esp_gmf_pipeline_view_add_pipeline(s_pipeline_view, name, NULL);
        if (ret != ESP_GMF_ERR_OK) {
            goto cleanup;
        }
    }
    if (pipeline_num > 1) {
        char mixed[AUDIO_RENDER_PIPELINE_NAME_MAX];
        ret = format_pipeline_name(pipeline_num, pipeline_num - 1, mixed, sizeof(mixed));
        if (ret != ESP_GMF_ERR_OK) {
            goto cleanup;
        }
        for (int i = 0; i < pipeline_num - 1; i++) {
            ret = format_pipeline_name(pipeline_num, i, name, sizeof(name));
            if (ret != ESP_GMF_ERR_OK) {
                goto cleanup;
            }
            ret = esp_gmf_pipeline_view_add_connection(s_pipeline_view, name, mixed);
            if (ret != ESP_GMF_ERR_OK) {
                goto cleanup;
            }
        }
    }
    return ESP_GMF_ERR_OK;

cleanup:
    esp_gmf_pipeline_view_deinit(s_pipeline_view);
    s_pipeline_view = NULL;
    return ret;
}

esp_gmf_err_t audio_render_pipeline_view_update(const esp_gmf_pipeline_handle_t pipelines[], uint8_t pipeline_num)
{
    if (pipelines == NULL || pipeline_num == 0 || s_pipeline_view == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    char name[AUDIO_RENDER_PIPELINE_NAME_MAX];
    for (int i = 0; i < pipeline_num; i++) {
        esp_gmf_err_t ret = format_pipeline_name(pipeline_num, i, name, sizeof(name));
        if (ret != ESP_GMF_ERR_OK) {
            return ret;
        }
        ret = esp_gmf_pipeline_view_set_pipeline(s_pipeline_view, name, pipelines[i]);
        if (ret != ESP_GMF_ERR_OK) {
            return ret;
        }
    }
    return ESP_GMF_ERR_OK;
}

void audio_render_pipeline_view_deinit(void)
{
    if (s_pipeline_view != NULL) {
        esp_gmf_pipeline_view_deinit(s_pipeline_view);
        s_pipeline_view = NULL;
    }
}

static esp_gmf_err_t audio_render_pipeline_view_get(const esp_gmf_pipeline_view_t **view)
{
    if (view == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    *view = NULL;
    if (s_pipeline_view == NULL) {
        return ESP_GMF_ERR_NOT_FOUND;
    }
    *view = s_pipeline_view;
    return ESP_GMF_ERR_OK;
}

ESP_GMF_PIPELINE_VIEW_CONNECTOR_REGISTER(s_audio_render_pipeline_view_connector, audio_render_pipeline_view_get);

#endif  /* CONFIG_ESP_AUDIO_RENDER_PIPELINE_VIEW */
