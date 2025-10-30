/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_log.h"
#include "periph_anacmpr.h"

static const char *TAG = "PERIPH_ANACMPR";

int periph_anacmpr_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size != sizeof(ana_cmpr_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    ana_cmpr_config_t *anacmpr_cfg = (ana_cmpr_config_t *)cfg;
    periph_anacmpr_handle_t *handle = (periph_anacmpr_handle_t *)calloc(1, sizeof(periph_anacmpr_handle_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate memory for periph_anacmpr_handle_t from internal memory");
        return -1;
    }

    // Create analog comparator unit
    esp_err_t err = ana_cmpr_new_unit(anacmpr_cfg, &handle->cmpr_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create analog comparator unit: %s", esp_err_to_name(err));
        free(handle);
        return -1;
    }

    handle->unit = anacmpr_cfg->unit;
    ESP_LOGI(TAG, "Analog comparator unit %d initialized", handle->unit);

    *periph_handle = handle;
    return 0;
}

int periph_anacmpr_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }
    periph_anacmpr_handle_t *handle = (periph_anacmpr_handle_t *)periph_handle;
    // Delete analog comparator unit
    esp_err_t err = ana_cmpr_del_unit(handle->cmpr_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete analog comparator unit: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "Analog comparator unit %d deinitialized", handle->unit);
    free(handle);
    return 0;
}
