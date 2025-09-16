/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"

#include "esp_bt_audio_host.h"
#include "esp_bt_audio_event.h"
#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_ops.h"

static const char *TAG = "BT_AUD_HOST";

#ifdef CONFIG_BT_BLUEDROID_ENABLED
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"

static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }
    sprintf(str, "%02x:%02x:%02x:%02x:%02x:%02x",
            bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}

static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;

    if (!eir) {
        return false;
    }

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname) {
        if (rmt_bdname_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            rmt_bdname_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }

        if (bdname) {
            memcpy(bdname, rmt_bdname, rmt_bdname_len);
            bdname[rmt_bdname_len] = '\0';
        }
        if (bdname_len) {
            *bdname_len = rmt_bdname_len;
        }
        return true;
    }

    return false;
}

static void filter_inquiry_scan_result(esp_bt_gap_cb_param_t *param)
{
    char bda_str[18] = {0};
    uint32_t cod = 0;
    int32_t rssi = -129;
    uint8_t *eir = NULL;
    esp_bt_gap_dev_prop_t *p = NULL;
    esp_bt_audio_event_device_discovered_t event_data = {0};

    ESP_LOGI(TAG, "Scanned device: %s", bda2str(param->disc_res.bda, bda_str, 18));
    memcpy(&(event_data.addr), param->disc_res.bda, ESP_BD_ADDR_LEN);
    event_data.tech = ESP_BT_AUDIO_TECH_CLASSIC;

    for (int i = 0; i < param->disc_res.num_prop; i++) {
        p = param->disc_res.prop + i;
        switch (p->type) {
            case ESP_BT_GAP_DEV_PROP_COD:
                cod = *(uint32_t *)(p->val);
                event_data.disc_data.classic.cod = cod;
                ESP_LOGD(TAG, "--Class of Device: 0x%" PRIx32, cod);
                break;
            case ESP_BT_GAP_DEV_PROP_RSSI:
                rssi = *(int8_t *)(p->val);
                event_data.rssi = rssi;
                ESP_LOGD(TAG, "--RSSI: %" PRId32, rssi);
                break;
            case ESP_BT_GAP_DEV_PROP_EIR:
                eir = (uint8_t *)(p->val);
                break;
            case ESP_BT_GAP_DEV_PROP_BDNAME:
            default:
                break;
        }
    }
    if (eir) {
        uint8_t bdname_len = 0;
        if (get_name_from_eir(eir, (uint8_t *)event_data.name, &bdname_len)) {
            ESP_LOGD(TAG, "Found a target device, address %s, name %s", bda_str, event_data.name);
        }
    }
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_DEVICE_DISCOVERED, &event_data);
}

static esp_err_t bluedroid_start_discovery()
{
    ESP_LOGI(TAG, "Starting classic discovery");
    esp_err_t ret = esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start discovery: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bluedroid_stop_discovery()
{
    ESP_LOGI(TAG, "Stopping classic discovery");
    esp_err_t ret = esp_bt_gap_cancel_discovery();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop discovery: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bluedroid_set_scan_mode(bool connectable, bool discoverable)
{
    ESP_LOGI(TAG, "Setting bluedroid scan mode: connectable %s, discoverable %s",
             connectable ? "true" : "false", discoverable ? "true" : "false");
    return esp_bt_gap_set_scan_mode(
        connectable ? ESP_BT_CONNECTABLE : ESP_BT_NON_CONNECTABLE,
        discoverable ? ESP_BT_GENERAL_DISCOVERABLE : ESP_BT_NON_DISCOVERABLE);
}

static esp_err_t bluedroid_host_set_ops()
{
    esp_bt_audio_classic_ops_t bluedroid_classic_ops = {0};
    bt_audio_ops_get_classic(&bluedroid_classic_ops);
    bluedroid_classic_ops.start_discovery = bluedroid_start_discovery;
    bluedroid_classic_ops.stop_discovery = bluedroid_stop_discovery;
    bluedroid_classic_ops.set_scan_mode = bluedroid_set_scan_mode;
    ESP_LOGI(TAG, "Setting bluedroid discovery operations");
    return bt_audio_ops_set_classic(&bluedroid_classic_ops);
}

static void bluedroid_host_gap_cb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t *param)
{
    switch (event) {
        case ESP_BT_GAP_DISC_RES_EVT: {
            filter_inquiry_scan_result(param);
            break;
        }
        case ESP_BT_GAP_DISC_STATE_CHANGED_EVT: {
            esp_bt_audio_event_discovery_st_t event_data = {0};
            event_data.tech = ESP_BT_AUDIO_TECH_CLASSIC;
            event_data.discovering = false;

            if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
                ESP_LOGI(TAG, "Device discovery stopped.");
                event_data.discovering = false;
            } else if (param->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED) {
                ESP_LOGI(TAG, "Discovery started.");
                event_data.discovering = true;
            }
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_DISCOVERY_STATE_CHG, &event_data);
            break;
        }
        case ESP_BT_GAP_AUTH_CMPL_EVT: {
            if (param->auth_cmpl.stat == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "Authentication successful: %s", param->auth_cmpl.device_name);
                ESP_LOG_BUFFER_HEX(TAG, param->auth_cmpl.bda, ESP_BD_ADDR_LEN);
            } else {
                ESP_LOGE(TAG, "Authentication failed, status: %d", param->auth_cmpl.stat);
            }
            break;
        }
        case ESP_BT_GAP_PIN_REQ_EVT: {
            ESP_LOGI(TAG, "ESP_BT_GAP_PIN_REQ_EVT min_16_digit: %d", param->pin_req.min_16_digit);
            if (param->pin_req.min_16_digit) {
                ESP_LOGI(TAG, "Input pin code: 0000 0000 0000 0000");
                esp_bt_pin_code_t pin_code = {0};
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 16, pin_code);
            } else {
                ESP_LOGI(TAG, "Input pin code: 1234");
                esp_bt_pin_code_t pin_code;
                pin_code[0] = '1';
                pin_code[1] = '2';
                pin_code[2] = '3';
                pin_code[3] = '4';
                esp_bt_gap_pin_reply(param->pin_req.bda, true, 4, pin_code);
            }
            break;
        }
        case ESP_BT_GAP_MODE_CHG_EVT:
            ESP_LOGI(TAG, "ESP_BT_GAP_MODE_CHG_EVT mode: %d", param->mode_chg.mode);
            break;
        default: {
            ESP_LOGI(TAG, "GAP event: %d", event);
            break;
        }
    }
}

esp_err_t esp_bt_audio_host_init(void *cfg)
{
    ESP_RETURN_ON_FALSE(cfg != NULL, ESP_ERR_INVALID_ARG, TAG, "cfg is NULL");
    esp_bt_audio_host_bluedroid_cfg_t *host_cfg = (esp_bt_audio_host_bluedroid_cfg_t *)cfg;

    ESP_RETURN_ON_ERROR(esp_bluedroid_init_with_cfg(&host_cfg->bluedroid_cfg), TAG, "Bluedroid init failed");
    ESP_RETURN_ON_ERROR(esp_bluedroid_enable(), TAG, "Bluedroid enable failed");
    if (host_cfg->bluedroid_cfg.ssp_en) {
        ESP_RETURN_ON_ERROR(esp_bt_gap_set_security_param(host_cfg->sp_param, &host_cfg->iocap, sizeof(uint8_t)), TAG, "Set security param failed");
    } else {
        ESP_RETURN_ON_ERROR(esp_bt_gap_set_pin(host_cfg->pin_type, 4, host_cfg->pin_code), TAG, "Set PIN code failed");
    }
    ESP_RETURN_ON_ERROR(esp_bt_gap_set_device_name(host_cfg->dev_name), TAG, "Set device name failed");
    ESP_RETURN_ON_ERROR(esp_bt_gap_register_callback(bluedroid_host_gap_cb), TAG, "Register GAP callback failed");

    return bluedroid_host_set_ops();
}

void esp_bt_audio_host_deinit(void)
{
    (void)esp_bt_gap_cancel_discovery();
    (void)esp_bluedroid_disable();
    (void)esp_bluedroid_deinit();
}

#endif  /* CONFIG_BT_BLUEDROID_ENABLED */
