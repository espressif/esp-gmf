/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "periph_i2c.h"
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "PERIPH_I2C";

int periph_i2c_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size < sizeof(i2c_master_bus_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    i2c_master_bus_handle_t handle = NULL;
    esp_err_t err = i2c_new_master_bus((i2c_master_bus_config_t *)cfg, &handle);
    if (err != ESP_OK) {
        free(handle);
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "i2c_new_master_bus initialize success");
    *periph_handle = handle;
    return 0;
}

int periph_i2c_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }
    ESP_LOGI(TAG, "i2c_del_master_bus deinitialize");
    i2c_master_bus_handle_t handle = (i2c_master_bus_handle_t)periph_handle;
    esp_err_t err = i2c_del_master_bus(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_del_master_bus failed: %s", esp_err_to_name(err));
        return -1;
    }
    return (err == ESP_OK) ? 0 : -1;
}
