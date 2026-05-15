/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_check.h"
#include "esp_log.h"

#include "esp_ble_audio_cap_api.h"
#include "esp_ble_audio_csip_api.h"
#include "esp_ble_audio_defs.h"

#include "bt_audio_le_csip.h"

/**
 * @brief  Runtime context for the CSIP set member.
 */
typedef struct {
    esp_ble_audio_csip_set_member_svc_inst_t *inst;                              /*!< Registered CSIP set member instance */
    uint8_t                                   rsi[ESP_BLE_AUDIO_CSIP_RSI_SIZE];  /*!< Generated RSI value */
} bt_audio_le_csip_ctx_t;

static const char *TAG = "BT_AUD_LE_CSIP";
static bt_audio_le_csip_ctx_t s_csip;

static void bt_audio_le_csip_lock_changed(esp_ble_conn_t *conn,
                                          esp_ble_audio_csip_set_member_svc_inst_t *inst,
                                          bool locked)
{
    (void)conn;
    (void)inst;
    ESP_LOGI(TAG, "CSIP lock %s", locked ? "set" : "released");
}

static uint8_t bt_audio_le_csip_sirk_read_req(esp_ble_conn_t *conn,
                                              esp_ble_audio_csip_set_member_svc_inst_t *inst)
{
    (void)conn;
    (void)inst;
    ESP_LOGD(TAG, "CSIP SIRK read request");
    return ESP_BLE_AUDIO_CSIP_READ_SIRK_REQ_RSP_ACCEPT;
}

static esp_ble_audio_csip_set_member_cb_t s_csip_cbs = {
    .lock_changed  = bt_audio_le_csip_lock_changed,
    .sirk_read_req = bt_audio_le_csip_sirk_read_req,
};

esp_err_t bt_audio_le_csip_init(const esp_bt_audio_le_csip_cfg_t *cfg,
                                esp_ble_audio_csip_set_member_svc_inst_t **inst,
                                uint8_t *rsi,
                                bool included_by_cas,
                                bt_audio_le_adv_builder_t adv_builder)
{
    esp_err_t ret = ESP_OK;
    esp_ble_audio_csip_set_member_register_param_t param = {0};

    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "CSIP config is NULL");
    ESP_RETURN_ON_FALSE(inst, ESP_ERR_INVALID_ARG, TAG, "CSIP instance output is NULL");
    ESP_RETURN_ON_FALSE(rsi, ESP_ERR_INVALID_ARG, TAG, "CSIP RSI output is NULL");
    ESP_RETURN_ON_FALSE(!s_csip.inst, ESP_ERR_INVALID_STATE, TAG, "CSIP already initialized");
    ESP_RETURN_ON_FALSE(cfg->coordinate_set_size > 0, ESP_ERR_INVALID_ARG, TAG, "CSIP set size is zero");
    uint8_t rank = cfg->rank ? cfg->rank : 1;
    ESP_RETURN_ON_FALSE(rank <= cfg->coordinate_set_size, ESP_ERR_INVALID_ARG, TAG,
                        "CSIP rank is out of range");

    param.set_size = cfg->coordinate_set_size;
    param.lockable = true;
    param.rank = rank;
    param.cb = &s_csip_cbs;
    memcpy(param.sirk, cfg->sirk, sizeof(param.sirk));

    if (included_by_cas) {
        ESP_GOTO_ON_ERROR(esp_ble_audio_cap_acceptor_register(&param, &s_csip.inst),
                          fail, TAG, "Failed to register CAP acceptor");
    } else {
        ESP_GOTO_ON_ERROR(esp_ble_audio_csip_set_member_register(&param, &s_csip.inst),
                          fail, TAG, "Failed to register CSIP set member");
    }
    ESP_GOTO_ON_ERROR(esp_ble_audio_csip_set_member_generate_rsi(s_csip.inst, rsi),
                      fail, TAG, "Failed to generate CSIP RSI");
    memcpy(s_csip.rsi, rsi, sizeof(s_csip.rsi));
    *inst = s_csip.inst;

    if (adv_builder) {
        ESP_GOTO_ON_ERROR(bt_audio_le_adv_builder_add_service_uuid16(adv_builder, ESP_BLE_AUDIO_UUID_CSIS_VAL),
                          fail, TAG, "Failed to add CSIS UUID");
        if (included_by_cas) {
            ESP_GOTO_ON_ERROR(bt_audio_le_adv_builder_add_service_uuid16(adv_builder, ESP_BLE_AUDIO_UUID_CAS_VAL),
                              fail, TAG, "Failed to add CAS UUID");
        }
        ESP_GOTO_ON_ERROR(bt_audio_le_adv_builder_add_field(adv_builder, 0x2E, s_csip.rsi, sizeof(s_csip.rsi)),
                          fail, TAG, "Failed to add CSIP RSI");
    }
    return ESP_OK;

fail:
    bt_audio_le_csip_deinit();
    ESP_LOGE(TAG, "Init CSIP failed: %s", esp_err_to_name(ret));
    return ret;
}

void bt_audio_le_csip_deinit(void)
{
    if (!s_csip.inst) {
        return;
    }
    esp_ble_audio_csip_set_member_unregister(s_csip.inst);
    memset(&s_csip, 0, sizeof(s_csip));
}
