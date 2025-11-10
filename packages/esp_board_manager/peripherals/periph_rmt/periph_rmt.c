/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_board_manager_defs.h"
#include "periph_rmt.h"

static const char *TAG = "PERIPH_RMT";

int periph_rmt_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || cfg_size < sizeof(periph_rmt_config_t) || (periph_handle == NULL)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    periph_rmt_config_t *config = (periph_rmt_config_t *)cfg;
    periph_rmt_handle_t *handle = (periph_rmt_handle_t *)calloc(1, sizeof(periph_rmt_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for periph_rmt_handle_t");
        return -1;
    }

    esp_err_t err = ESP_FAIL;
    if (strcmp(config->role, ESP_BOARD_PERIPH_ROLE_TX) == 0) {
        rmt_channel_handle_t chan = NULL;
        err = rmt_new_tx_channel(&config->channel_config.tx, &chan);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Call to rmt_new_tx_channel failed: %s", esp_err_to_name(err));
            free(handle);
            return -1;
        }
        handle->channel = chan;
    } else if (strcmp(config->role, ESP_BOARD_PERIPH_ROLE_RX) == 0) {
        rmt_channel_handle_t chan = NULL;
        err = rmt_new_rx_channel(&config->channel_config.rx, &chan);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Call to rmt_new_rx_channel failed: %s", esp_err_to_name(err));
            free(handle);
            return -1;
        }
        handle->channel = chan;
    } else {
        ESP_LOGE(TAG, "Unknown role %s", config->role);
        free(handle);
        return -1;
    }

    *periph_handle = handle;
    return 0;
}

int periph_rmt_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }
    periph_rmt_handle_t *handle = (periph_rmt_handle_t *)periph_handle;

    if (handle->channel) {
        esp_err_t err = rmt_del_channel(handle->channel);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "RMT channel del failed: %s", esp_err_to_name(err));
            free(handle);
            return -1;
        }
    }
    free(handle);
    return 0;
}
