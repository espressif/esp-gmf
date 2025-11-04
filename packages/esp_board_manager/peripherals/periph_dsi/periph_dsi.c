/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_err.h"
#include "esp_log.h"
#include "periph_dsi.h"
#include "esp_lcd_mipi_dsi.h"

static const char *TAG = "PERIPH_DSI";

int periph_dsi_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size != sizeof(esp_lcd_dsi_bus_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    esp_lcd_dsi_bus_handle_t handle = NULL;
    esp_err_t err = esp_lcd_new_dsi_bus((esp_lcd_dsi_bus_config_t *)cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MIPI DSI bus initialize failed: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "MIPI DSI bus initialize success");
    *periph_handle = handle;
    return 0;
}

int periph_dsi_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }
    ESP_LOGI(TAG, "MIPI DSI bus deinitialize");
    esp_lcd_dsi_bus_handle_t handle = (esp_lcd_dsi_bus_handle_t)periph_handle;
    esp_err_t err = esp_lcd_del_dsi_bus(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MIPI DSI bus del failed: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}
