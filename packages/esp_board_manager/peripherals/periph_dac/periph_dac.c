/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_log.h"
#include "esp_board_manager_defs.h"
#include "esp_board_periph.h"
#include "periph_dac.h"

static const char *TAG = "PERIPH_DAC";

int periph_dac_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size != sizeof(periph_dac_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    periph_dac_config_t *dac_cfg = (periph_dac_config_t *)cfg;
    periph_dac_handle_t *handle = (periph_dac_handle_t *)calloc(1, sizeof(periph_dac_handle_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate memory for periph_dac_handle_t");
        return -1;
    }

    esp_err_t err = ESP_FAIL;
    if (dac_cfg->role == ESP_BOARD_PERIPH_ROLE_ONESHOT) {
        err = dac_oneshot_new_channel(&dac_cfg->oneshot_cfg, &handle->oneshot);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "DAC oneshot channel %d initialized successfully", dac_cfg->oneshot_cfg.chan_id);
        }
    } else if (dac_cfg->role == ESP_BOARD_PERIPH_ROLE_CONTINUOUS) {
        err = dac_continuous_new_channels(&dac_cfg->continuous_cfg, &handle->continuous);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "DAC continuous channels initialized successfully, mask: 0x%x", dac_cfg->continuous_cfg.chan_mask);
        }
    } else if (dac_cfg->role == ESP_BOARD_PERIPH_ROLE_COSINE) {
        err = dac_cosine_new_channel(&dac_cfg->cosine_cfg, &handle->cosine);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "DAC cosine channel %d initialized successfully, freq: %d Hz",
                     dac_cfg->cosine_cfg.chan_id, dac_cfg->cosine_cfg.freq_hz);
        }
    } else {
        ESP_LOGE(TAG, "Invalid DAC role: %d", dac_cfg->role);
        free(handle);
        return -1;
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DAC initialization failed: %s", esp_err_to_name(err));
        free(handle);
        return -1;
    }

    *periph_handle = handle;
    return 0;
}

int periph_dac_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }

    periph_dac_handle_t *handle = (periph_dac_handle_t *)periph_handle;
    esp_err_t err = ESP_FAIL;
    const char *periph_name = NULL;
    err = esp_board_periph_get_name_by_handle(periph_handle, &periph_name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get periph name: %s", esp_err_to_name(err));
        goto cleanup;
    }
    periph_dac_config_t *cfg = NULL;
    err = esp_board_periph_get_config(periph_name, (void **)&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get config: %s", esp_err_to_name(err));
        goto cleanup;
    }
    if (cfg->role == ESP_BOARD_PERIPH_ROLE_ONESHOT) {
        err = dac_oneshot_del_channel(handle->oneshot);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "DAC oneshot channel deinitialized successfully");
        }
    } else if (cfg->role == ESP_BOARD_PERIPH_ROLE_CONTINUOUS) {
        err = dac_continuous_del_channels(handle->continuous);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "DAC continuous channels deinitialized successfully");
        }
    } else if (cfg->role == ESP_BOARD_PERIPH_ROLE_COSINE) {
        err = dac_cosine_del_channel(handle->cosine);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "DAC cosine channel deinitialized successfully");
        }
    } else {
        ESP_LOGE(TAG, "Invalid DAC role: %d", cfg->role);
        goto cleanup;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "DAC deinitialization failed: %s", esp_err_to_name(err));
        goto cleanup;
    }
    return 0;
cleanup:
    free(handle);
    return -1;
}
