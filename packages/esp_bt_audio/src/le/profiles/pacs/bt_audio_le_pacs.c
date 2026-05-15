/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_check.h"
#include "esp_log.h"

#include "esp_ble_iso_common_api.h"
#include "esp_ble_audio_codec_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_audio_lc3_defs.h"
#include "esp_ble_audio_pacs_api.h"

#include "bt_audio_le_pacs.h"

static const char *TAG = "BT_AUD_LE_PACS";

static const uint16_t s_sink_ctx_mask = ESP_BLE_AUDIO_CONTEXT_TYPE_ANY;
static const uint16_t s_source_ctx_mask = ESP_BLE_AUDIO_CONTEXT_TYPE_ANY;

static uint8_t s_sink_codec_data[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_DATA(
    ESP_BLE_AUDIO_CODEC_CAP_FREQ_ANY,
    ESP_BLE_AUDIO_CODEC_CAP_DURATION_10 | ESP_BLE_AUDIO_CODEC_CAP_DURATION_7_5,
    ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_1 | ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_2,
    26,
    155,
    1);
static uint8_t s_sink_codec_meta[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_META(s_sink_ctx_mask);
static const esp_ble_audio_codec_cap_t s_sink_codec_cap =
    ESP_BLE_AUDIO_CODEC_CAP_LC3(s_sink_codec_data, s_sink_codec_meta);

static uint8_t s_source_codec_data[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_DATA(
    ESP_BLE_AUDIO_CODEC_CAP_FREQ_ANY,
    ESP_BLE_AUDIO_CODEC_CAP_DURATION_10 | ESP_BLE_AUDIO_CODEC_CAP_DURATION_7_5,
    ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_1 | ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_2,
    26,
    155,
    1);
static uint8_t s_source_codec_meta[] = ESP_BLE_AUDIO_CODEC_CAP_LC3_META(s_source_ctx_mask);
static const esp_ble_audio_codec_cap_t s_source_codec_cap =
    ESP_BLE_AUDIO_CODEC_CAP_LC3(s_source_codec_data, s_source_codec_meta);

static esp_ble_audio_pacs_cap_t s_sink_cap = {
    .codec_cap = &s_sink_codec_cap,
};
static esp_ble_audio_pacs_cap_t s_source_cap = {
    .codec_cap = &s_source_codec_cap,
};

static bool s_sink_registered;
static bool s_source_registered;
static bool s_pacs_registered;

static inline bool bt_audio_le_pacs_sink_enabled(const esp_bt_audio_le_pacs_cfg_t *cfg)
{
    return cfg->sink_enabled || cfg->sink_locations || cfg->sink_context_mask;
}

static inline bool bt_audio_le_pacs_source_enabled(const esp_bt_audio_le_pacs_cfg_t *cfg)
{
    return cfg->source_enabled || cfg->source_locations || cfg->source_context_mask;
}

esp_err_t bt_audio_le_pacs_register(const esp_bt_audio_le_pacs_cfg_t *cfg)
{
    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "PACS config is NULL");

    bool sink_enabled = bt_audio_le_pacs_sink_enabled(cfg);
    bool source_enabled = bt_audio_le_pacs_source_enabled(cfg);
    esp_ble_audio_pacs_register_param_t pacs_param = {
        .snk_pac = sink_enabled,
        .snk_loc = sink_enabled,
        .src_pac = source_enabled,
        .src_loc = source_enabled,
    };

    ESP_RETURN_ON_ERROR(esp_ble_audio_pacs_register(&pacs_param), TAG, "Failed to register PACS service");
    s_pacs_registered = true;

    if (sink_enabled) {
        ESP_RETURN_ON_ERROR(esp_ble_audio_pacs_cap_register(ESP_BLE_AUDIO_DIR_SINK, &s_sink_cap),
                            TAG, "Failed to register sink PAC");
        s_sink_registered = true;
        ESP_RETURN_ON_ERROR(esp_ble_audio_pacs_set_location(ESP_BLE_AUDIO_DIR_SINK, cfg->sink_locations),
                            TAG, "Failed to set sink location");
        ESP_RETURN_ON_ERROR(
            esp_ble_audio_pacs_set_supported_contexts(ESP_BLE_AUDIO_DIR_SINK, cfg->sink_context_mask),
            TAG, "Failed to set sink supported contexts");
        ESP_RETURN_ON_ERROR(
            esp_ble_audio_pacs_set_available_contexts(ESP_BLE_AUDIO_DIR_SINK, cfg->sink_context_mask),
            TAG, "Failed to set sink available contexts");
    }

    if (source_enabled) {
        ESP_RETURN_ON_ERROR(esp_ble_audio_pacs_cap_register(ESP_BLE_AUDIO_DIR_SOURCE, &s_source_cap),
                            TAG, "Failed to register source PAC");
        s_source_registered = true;
        ESP_RETURN_ON_ERROR(esp_ble_audio_pacs_set_location(ESP_BLE_AUDIO_DIR_SOURCE, cfg->source_locations),
                            TAG, "Failed to set source location");
        ESP_RETURN_ON_ERROR(
            esp_ble_audio_pacs_set_supported_contexts(ESP_BLE_AUDIO_DIR_SOURCE, cfg->source_context_mask),
            TAG, "Failed to set source supported contexts");
        ESP_RETURN_ON_ERROR(
            esp_ble_audio_pacs_set_available_contexts(ESP_BLE_AUDIO_DIR_SOURCE, cfg->source_context_mask),
            TAG, "Failed to set source available contexts");
    }

    return ESP_OK;
}

void bt_audio_le_pacs_unregister(void)
{
    if (s_sink_registered) {
        esp_ble_audio_pacs_cap_unregister(ESP_BLE_AUDIO_DIR_SINK, &s_sink_cap);
        s_sink_registered = false;
    }
    if (s_source_registered) {
        esp_ble_audio_pacs_cap_unregister(ESP_BLE_AUDIO_DIR_SOURCE, &s_source_cap);
        s_source_registered = false;
    }
    if (s_pacs_registered) {
        esp_ble_audio_pacs_unregister();
        s_pacs_registered = false;
    }
}
