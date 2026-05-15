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
#include "esp_heap_caps.h"

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

static esp_err_t bt_audio_host_bluedroid_init(void *cfg)
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

static void bt_audio_host_bluedroid_deinit(void)
{
    (void)esp_bt_gap_cancel_discovery();
    (void)esp_bluedroid_disable();
    (void)esp_bluedroid_deinit();
}

#endif  /* CONFIG_BT_BLUEDROID_ENABLED */

#ifdef CONFIG_BT_NIMBLE_ENABLED

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"

extern void ble_store_config_init(void);

static bool s_nimble_host_inited = false;

static const char *bt_audio_nimble_uuid16_name(const ble_uuid_t *uuid)
{
    if (uuid->type != BLE_UUID_TYPE_16) {
        return NULL;
    }

    switch (ble_uuid_u16(uuid)) {
        /* GAP / GATT */
        case 0x1800:
            return "GAP";
        case 0x1801:
            return "GATT";
        case 0x2a00:
            return "Device Name";
        case 0x2a01:
            return "Appearance";
        case 0x2a05:
            return "Service Changed";
        case 0x2b29:
            return "Client Sup Features";
        case 0x2b3a:
            return "Server Sup Features";

        /* LE Audio services */
        case 0x1843:
            return "AICS";
        case 0x1844:
            return "VCS";
        case 0x1845:
            return "VOCS";
        case 0x1846:
            return "CSIS";
        case 0x1848:
            return "MCS";
        case 0x1849:
            return "GMCS";
        case 0x184b:
            return "TBS";
        case 0x184c:
            return "GTBS";
        case 0x184d:
            return "MICS";
        case 0x184e:
            return "ASCS";
        case 0x184f:
            return "BASS";
        case 0x1850:
            return "PACS";
        case 0x1851:
            return "Basic Audio Ann";
        case 0x1852:
            return "Broadcast Audio Ann";
        case 0x1853:
            return "CAS";
        case 0x1854:
            return "HAS";
        case 0x1855:
            return "TMAS";
        case 0x1856:
            return "Public Broadcast Ann";
        case 0x1858:
            return "GMAS";

        /* Common */
        case 0x2b51:
            return "TMAP Role";
        case 0x2bba:
            return "CCID";

        /* AICS chars */
        case 0x2b77:
            return "AICS State";
        case 0x2b78:
            return "AICS Gain Settings";
        case 0x2b79:
            return "AICS Input Type";
        case 0x2b7a:
            return "AICS Input Status";
        case 0x2b7b:
            return "AICS Control";
        case 0x2b7c:
            return "AICS Description";

        /* VCS chars */
        case 0x2b7d:
            return "VCS State";
        case 0x2b7e:
            return "VCS Control";
        case 0x2b7f:
            return "VCS Flags";

        /* VOCS chars */
        case 0x2b80:
            return "VOCS State";
        case 0x2b81:
            return "VOCS Location";
        case 0x2b82:
            return "VOCS Control";
        case 0x2b83:
            return "VOCS Description";

        /* CSIS chars */
        case 0x2b84:
            return "CSIS SIRK";
        case 0x2b85:
            return "CSIS Set Size";
        case 0x2b86:
            return "CSIS Set Lock";
        case 0x2b87:
            return "CSIS Rank";

        /* MCS chars */
        case 0x2b93:
            return "MCS Player Name";
        case 0x2b94:
            return "MCS Icon Obj ID";
        case 0x2b95:
            return "MCS Icon URL";
        case 0x2b96:
            return "MCS Track Changed";
        case 0x2b97:
            return "MCS Track Title";
        case 0x2b98:
            return "MCS Track Duration";
        case 0x2b99:
            return "MCS Track Position";
        case 0x2b9a:
            return "MCS Playback Speed";
        case 0x2b9b:
            return "MCS Seeking Speed";
        case 0x2b9c:
            return "MCS Track Seg Obj ID";
        case 0x2b9d:
            return "MCS Cur Track Obj ID";
        case 0x2b9e:
            return "MCS Next Track Obj ID";
        case 0x2b9f:
            return "MCS Parent Grp Obj ID";
        case 0x2ba0:
            return "MCS Cur Grp Obj ID";
        case 0x2ba1:
            return "MCS Playing Order";
        case 0x2ba2:
            return "MCS Playing Orders";
        case 0x2ba3:
            return "MCS Media State";
        case 0x2ba4:
            return "MCS Media CP";
        case 0x2ba5:
            return "MCS Media CP Opcodes";
        case 0x2ba6:
            return "MCS Search Res Obj ID";
        case 0x2ba7:
            return "MCS Search CP";

        /* TBS chars */
        case 0x2bb3:
            return "TBS Provider Name";
        case 0x2bb4:
            return "TBS UCI";
        case 0x2bb5:
            return "TBS Technology";
        case 0x2bb6:
            return "TBS URI List";
        case 0x2bb7:
            return "TBS Signal Strength";
        case 0x2bb8:
            return "TBS Signal Interval";
        case 0x2bb9:
            return "TBS List Cur Calls";
        case 0x2bbb:
            return "TBS Status Flags";
        case 0x2bbc:
            return "TBS Incoming URI";
        case 0x2bbd:
            return "TBS Call State";
        case 0x2bbe:
            return "TBS Call CP";
        case 0x2bbf:
            return "TBS Optional Opcodes";
        case 0x2bc0:
            return "TBS Term Reason";
        case 0x2bc1:
            return "TBS Incoming Call";
        case 0x2bc2:
            return "TBS Friendly Name";

        /* MICS / ASCS / BASS chars */
        case 0x2bc3:
            return "MICS Mute";
        case 0x2bc4:
            return "ASCS ASE Snk";
        case 0x2bc5:
            return "ASCS ASE Src";
        case 0x2bc6:
            return "ASCS ASE CP";
        case 0x2bc7:
            return "BASS CP";
        case 0x2bc8:
            return "BASS Recv State";

        /* PACS chars */
        case 0x2bc9:
            return "PACS Snk";
        case 0x2bca:
            return "PACS Snk Loc";
        case 0x2bcb:
            return "PACS Src";
        case 0x2bcc:
            return "PACS Src Loc";
        case 0x2bcd:
            return "PACS Avail Ctx";
        case 0x2bce:
            return "PACS Sup Ctx";

        default:
            return NULL;
    }
}

static void bt_audio_nimble_gatt_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    char *buf = heap_caps_malloc_prefer(BLE_UUID_STR_LEN, 2,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                        MALLOC_CAP_DEFAULT);
    if (!buf) {
        ESP_LOGE(TAG, "No memory for UUID string buffer");
        return;
    }
    const char *name;

    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_SVC:
            name = bt_audio_nimble_uuid16_name(ctxt->svc.svc_def->uuid);
            ESP_LOGI(TAG, "svc %-20s (%s), handle %u",
                     name ? name : "?",
                     ble_uuid_to_str(ctxt->svc.svc_def->uuid, buf),
                     ctxt->svc.handle);
            break;

        case BLE_GATT_REGISTER_OP_CHR:
            name = bt_audio_nimble_uuid16_name(ctxt->chr.chr_def->uuid);
            ESP_LOGI(TAG, "chr %-20s (%s), handles %u/%u",
                     name ? name : "?",
                     ble_uuid_to_str(ctxt->chr.chr_def->uuid, buf),
                     ctxt->chr.def_handle, ctxt->chr.val_handle);
            break;

        case BLE_GATT_REGISTER_OP_DSC:
            name = bt_audio_nimble_uuid16_name(ctxt->dsc.dsc_def->uuid);
            ESP_LOGI(TAG, "dsc %-20s (%s), handle %u",
                     name ? name : "?",
                     ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, buf),
                     ctxt->dsc.handle);
            break;

        default:
            assert(0);
            break;
    }
    heap_caps_free(buf);
}

static void bt_audio_nimble_on_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE reset: reason=%d", reason);
}

static void bt_audio_nimble_on_sync(void)
{
    if (ble_hs_util_ensure_addr(0) != 0) {
        ESP_LOGE(TAG, "NimBLE sync failed: ensure address failed");
    }
}

static void bt_audio_nimble_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static esp_err_t bt_audio_host_nimble_init(void *cfg)
{
    esp_bt_audio_host_nimble_cfg_t *host_cfg = (esp_bt_audio_host_nimble_cfg_t *)cfg;
    esp_err_t ret;

    ESP_RETURN_ON_FALSE(host_cfg != NULL, ESP_ERR_INVALID_ARG, TAG, "NimBLE host init failed: cfg is NULL");
    ESP_RETURN_ON_FALSE(!s_nimble_host_inited, ESP_ERR_INVALID_STATE, TAG,
                        "NimBLE host init failed: already initialized");

    ret = esp_nimble_init();
    ESP_RETURN_ON_ERROR(ret, TAG, "NimBLE host init failed: esp_nimble_init");

    ble_hs_cfg.gatts_register_cb = bt_audio_nimble_gatt_register_cb;
    ble_hs_cfg.reset_cb = bt_audio_nimble_on_reset;
    ble_hs_cfg.sync_cb = bt_audio_nimble_on_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO;

    ret = ble_svc_gap_device_name_set(host_cfg->dev_name);
    if (ret != 0) {
        ESP_LOGE(TAG, "NimBLE host init failed: set device name (%d)", ret);
        (void)esp_nimble_deinit();
        return ESP_FAIL;
    }

    ble_store_config_init();
    nimble_port_freertos_init(bt_audio_nimble_host_task);
    s_nimble_host_inited = true;
    return ESP_OK;
}

static void bt_audio_host_nimble_deinit(void)
{
    if (!s_nimble_host_inited) {
        return;
    }
    (void)esp_nimble_deinit();
    s_nimble_host_inited = false;
}

#endif  /* CONFIG_BT_NIMBLE_ENABLED */

esp_err_t esp_bt_audio_host_init(void *cfg)
{
#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO
    return bt_audio_host_nimble_init(cfg);
#elif CONFIG_BT_BLUEDROID_ENABLED && CONFIG_BT_CLASSIC_ENABLED
    return bt_audio_host_bluedroid_init(cfg);
#elif CONFIG_BT_NIMBLE_ENABLED
    return bt_audio_host_nimble_init(cfg);
#else
    (void)cfg;
    ESP_LOGE(TAG, "Host init failed: no supported BT host is enabled");
    return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO */
}

void esp_bt_audio_host_deinit(void)
{
#if CONFIG_BT_NIMBLE_ENABLED
    bt_audio_host_nimble_deinit();
#endif  /* CONFIG_BT_NIMBLE_ENABLED */
#if CONFIG_BT_BLUEDROID_ENABLED
    bt_audio_host_bluedroid_deinit();
#endif  /* CONFIG_BT_BLUEDROID_ENABLED */
}
