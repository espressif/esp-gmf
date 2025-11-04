/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_err.h"
#include "esp_log.h"
#include "periph_ldo.h"
#include "esp_ldo_regulator.h"

static const char *TAG = "PERIPH_LDO";

int periph_ldo_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size != sizeof(esp_ldo_channel_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    esp_ldo_channel_handle_t ldo_phy_chan = NULL;
    esp_err_t err = esp_ldo_acquire_channel((esp_ldo_channel_config_t *)cfg, &ldo_phy_chan);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LDO acquire channel failed: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "LDO initialize success");
    *periph_handle = ldo_phy_chan;
    return 0;
}

int periph_ldo_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }
    ESP_LOGI(TAG, "LDO deinitialize");
    esp_ldo_channel_handle_t handle = (esp_ldo_channel_handle_t)periph_handle;
    esp_err_t err = esp_ldo_release_channel(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LDO del failed: %s", esp_err_to_name(err));
        return -1;
    }
    return 0;
}
