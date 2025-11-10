/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_log.h"
#include "periph_pcnt.h"

static const char *TAG = "PERIPH_PCNT";

int periph_pcnt_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size < sizeof(periph_pcnt_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    esp_err_t err = ESP_FAIL;
    periph_pcnt_config_t *config = (periph_pcnt_config_t *)cfg;
    periph_pcnt_handle_t *handle = (periph_pcnt_handle_t *)calloc(1, sizeof(periph_pcnt_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for periph_pcnt_handle_t");
        return -1;
    }

    /* create unit */
    err = pcnt_new_unit(&config->unit_config, &handle->pcnt_unit);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Creat pcnt unit failed: %s", esp_err_to_name(err));
        free(handle);
        return -1;
    }

    /* set glitch filter */
    err = pcnt_unit_set_glitch_filter(handle->pcnt_unit, &config->filter_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set pcnt glitch filter failed: %s", esp_err_to_name(err));
        pcnt_del_unit(handle->pcnt_unit);
        free(handle);
        return -1;
    }

    /* create channels */
    uint8_t ch_count = config->channel_count;
    if (ch_count > SOC_PCNT_CHANNELS_PER_UNIT) {
        ESP_LOGW(TAG, "Channel count %d exceeds max %d, limiting to max", ch_count, SOC_PCNT_CHANNELS_PER_UNIT);
        ch_count = SOC_PCNT_CHANNELS_PER_UNIT;
    }
    for (uint8_t i = 0; i < ch_count; i++) {
        periph_pcnt_channel_config_t *ch_cfg = &config->channel_list[i];
        pcnt_channel_handle_t ch = NULL;
        err = pcnt_new_channel(handle->pcnt_unit, &ch_cfg->channel_config, &ch);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Apply pcnt channel %d failed: %s", i, esp_err_to_name(err));
            /* cleanup created channels + unit */
            for (uint8_t j = 0; j < i; j++) {
                if (handle->channels[j]) {
                    pcnt_del_channel(handle->channels[j]);
                    handle->channels[j] = NULL;
                }
            }
            pcnt_del_unit(handle->pcnt_unit);
            free(handle);
            return -1;
        }
        handle->channels[i] = ch;

        /* set edge and level actions if provided */
        pcnt_channel_set_edge_action(ch, ch_cfg->pos_act, ch_cfg->neg_act);
        pcnt_channel_set_level_action(ch, ch_cfg->high_act, ch_cfg->low_act);
    }

    /* add watch points if any */
    if (config->watch_point_count > 0) {
        uint8_t wp_cnt = config->watch_point_count;
        if (wp_cnt > SOC_PCNT_THRES_POINT_PER_UNIT + 3) {
            wp_cnt = SOC_PCNT_THRES_POINT_PER_UNIT + 3;
            ESP_LOGW(TAG, "Watch point count exceeds max %d, limiting to max", SOC_PCNT_THRES_POINT_PER_UNIT + 3);
        }
        for (uint8_t i = 0; i < wp_cnt; i++) {
            pcnt_unit_add_watch_point(handle->pcnt_unit, config->watch_point_list[i]);
        }
    }

    *periph_handle = (void *)handle;
    ESP_LOGI(TAG, "Periph pcnt init success: unit=%p", handle->pcnt_unit);
    return 0;
}

int periph_pcnt_deinit(void *periph_handle)
{
    if (periph_handle == NULL) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }
    periph_pcnt_handle_t *handle = (periph_pcnt_handle_t *)periph_handle;

    esp_err_t err = ESP_FAIL;
    /* delete channels */
    for (size_t i = 0; i < SOC_PCNT_CHANNELS_PER_UNIT; i++) {
        if (handle->channels[i]) {
            err = pcnt_del_channel(handle->channels[i]);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to delete channel %d: %s", i, esp_err_to_name(err));
                return -1;
            }
            handle->channels[i] = NULL;
        }
    }

    /* delete unit */
    if (handle->pcnt_unit) {
        err = pcnt_del_unit(handle->pcnt_unit);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to delete pcnt unit: %s", esp_err_to_name(err));
            return -1;
        }
        handle->pcnt_unit = NULL;
    }

    free(handle);
    ESP_LOGI(TAG, "Periph pcnt deinit success");
    return 0;
}
