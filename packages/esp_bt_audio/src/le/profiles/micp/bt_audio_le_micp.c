/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "esp_ble_audio_aics_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_audio_micp_api.h"

#include "bt_audio_le_micp.h"

static const char *TAG = "BT_AUD_LE_MICP";

static void bt_audio_le_micp_mute_cb(uint8_t mute_value)
{
    ESP_LOGI(TAG, "MICP mute value: %u", mute_value);
}

esp_err_t bt_audio_le_micp_init(bt_audio_le_adv_builder_t adv_builder)
{
    static esp_ble_audio_micp_mic_dev_cb_t micp_cbs = {
        .mute = bt_audio_le_micp_mute_cb,
    };
    static esp_ble_audio_aics_register_param_t aics_param[CONFIG_BT_MICP_MIC_DEV_AICS_INSTANCE_COUNT];
    static char aics_desc[CONFIG_BT_MICP_MIC_DEV_AICS_INSTANCE_COUNT][32];
    esp_ble_audio_micp_mic_dev_register_param_t micp_param = {0};

    memset(aics_param, 0, sizeof(aics_param));
    micp_param.aics_param = aics_param;
    micp_param.cb = &micp_cbs;
    for (size_t i = 0; i < CONFIG_BT_MICP_MIC_DEV_AICS_INSTANCE_COUNT; i++) {
        aics_param[i].gain_mode = ESP_BLE_AUDIO_AICS_MODE_MANUAL;
        aics_param[i].units = 1;
        aics_param[i].min_gain = -100;
        aics_param[i].max_gain = 100;
        aics_param[i].type = ESP_BLE_AUDIO_AICS_INPUT_TYPE_UNSPECIFIED;
        aics_param[i].status = true;
        aics_param[i].desc_writable = true;
        snprintf(aics_desc[i], sizeof(aics_desc[i]), "Input %u", (unsigned)(i + 1));
        aics_param[i].description = aics_desc[i];
    }

    if (adv_builder) {
        esp_err_t ret = bt_audio_le_adv_builder_add_service_uuid16(adv_builder, ESP_BLE_AUDIO_UUID_MICS_VAL);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Init MICP failed: add MICS UUID error %s", esp_err_to_name(ret));
            return ret;
        }
    }
    esp_err_t ret = esp_ble_audio_micp_mic_dev_register(&micp_param);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init MICP failed: register microphone device error %s", esp_err_to_name(ret));
    }
    return ret;
}

void bt_audio_le_micp_deinit(void)
{
    ESP_LOGD(TAG, "MICP microphone device deinit");
}
