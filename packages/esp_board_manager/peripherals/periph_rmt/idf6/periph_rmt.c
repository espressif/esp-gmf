/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_board_manager_defs.h"
#include "periph_rmt.h"

static const char *TAG = "PERIPH_RMT_V2";

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
    gpio_num_t gpio_num = GPIO_NUM_NC;

    if (config->role == ESP_BOARD_PERIPH_ROLE_TX) {
        rmt_channel_handle_t chan = NULL;
        err = rmt_new_tx_channel(&config->channel_config.tx, &chan);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Call to rmt_new_tx_channel failed: %s", esp_err_to_name(err));
            free(handle);
            return -1;
        }
        handle->channel = chan;
        gpio_num = (gpio_num_t)config->channel_config.tx.gpio_num;
    } else if (config->role == ESP_BOARD_PERIPH_ROLE_RX) {
        rmt_channel_handle_t chan = NULL;
        err = rmt_new_rx_channel(&config->channel_config.rx, &chan);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Call to rmt_new_rx_channel failed: %s", esp_err_to_name(err));
            free(handle);
            return -1;
        }
        handle->channel = chan;
        gpio_num = (gpio_num_t)config->channel_config.rx.gpio_num;
    } else {
        ESP_LOGE(TAG, "Unknown role %d", config->role);
        free(handle);
        return -1;
    }

    // Apply extra flags for GPIO configuration (IDF 6.0+)
    if (gpio_num >= 0) {
        if (config->extra_flags.io_od_mode && config->role == ESP_BOARD_PERIPH_ROLE_TX) {
            gpio_od_enable(gpio_num);
        }
        // Loopback logic skipped as noted before
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
            return -1;
        }
        handle->channel = NULL;
    }
    free(handle);
    return 0;
}
