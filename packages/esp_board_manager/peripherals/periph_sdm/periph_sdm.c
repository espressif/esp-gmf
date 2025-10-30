/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_log.h"
#include "periph_sdm.h"

static const char *TAG = "PERIPH_SDM";

int periph_sdm_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size != sizeof(sdm_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    periph_sdm_handle_t *handle = (periph_sdm_handle_t *)calloc(1, sizeof(periph_sdm_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for periph_sdm_handle_t");
        return -1;
    }

    sdm_config_t *sdm_cfg = (sdm_config_t *)cfg;
    esp_err_t err = sdm_new_channel(sdm_cfg, &handle->sdm_chan);
    if (err != ESP_OK) {
        free(handle);
        ESP_LOGE(TAG, "Call to sdm_new_channel failed: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "SDM channel initialized successfully, GPIO: %d, sample rate: %d Hz",
             sdm_cfg->gpio_num, sdm_cfg->sample_rate_hz);

    *periph_handle = handle;
    return 0;
}

int periph_sdm_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }

    periph_sdm_handle_t *handle = (periph_sdm_handle_t *)periph_handle;

    esp_err_t err = sdm_del_channel(handle->sdm_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Call to sdm_del_channel failed: %s", esp_err_to_name(err));
        return -1;
    }

    free(handle);
    ESP_LOGI(TAG, "SDM channel deinitialized successfully");
    return 0;
}
