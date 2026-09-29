/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_bt.h"
#include "esp_bt_audio_event.h"
#include "esp_bt_audio_host.h"
#include "esp_bt_device.h"
#include "esp_gap_bt_api.h"
#include "esp_err.h"
#include "esp_log.h"
#if CONFIG_BT_AUDIO && CONFIG_BT_ISO
#include "esp_ble_audio_common_api.h"
#include "esp_gap_ble_api.h"
#include "esp_gattc_api.h"
#include "tinycrypt/aes.h"
#include "tinycrypt/constants.h"
#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO */

#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_host_ops.h"
#include "bt_audio_ops.h"

#define BT_AUDIO_BDROID_MAX_LE_PEERS  2

/**
 * @brief  Connected or pending LE peer tracked by the BlueDroid host.
 */
typedef struct {
    esp_bd_addr_t  addr;          /*!< Peer identity address */
    uint16_t       conn_handle;   /*!< LE ACL connection handle */
    bool           valid;         /*!< True while the slot is in use */
} bt_audio_bluedroid_peer_t;

/* Runtime context for BLE Audio / ISO on BlueDroid. */
typedef struct {
    bt_audio_bluedroid_peer_t ble_peers[BT_AUDIO_BDROID_MAX_LE_PEERS];  /*!< Connected/pending LE peers */
    char              ble_dev_name[ESP_BT_AUDIO_HOST_MAX_DEV_NAME_LEN]; /*!< Local BLE device name */
    SemaphoreHandle_t ble_gap_op_sem;                                 /*!< Signals completion of a blocking GAP operation */
    SemaphoreHandle_t ble_gap_op_mutex;                               /*!< Serializes concurrent blocking GAP operations */
    esp_bt_status_t   ble_gap_op_status;                              /*!< Result of the last GAP operation */
    bool              gap_cb_registered;                              /*!< Whether the GAP callback is registered */
} bt_audio_host_bluedroid_t;

static bt_audio_host_bluedroid_t *s_host;
static const char *TAG = "BT_AUD_HOST_BDROID";

#if CONFIG_BT_AUDIO && CONFIG_BT_ISO

#define BT_AUDIO_BDROID_GAP_OP_TIMEOUT_MS      5000
#define BT_AUDIO_BDROID_SCAN_DURATION_UNIT_MS  10

static bt_audio_bluedroid_peer_t *bdroid_peer_find_handle(uint16_t conn_handle)
{
    if (!s_host) {
        return NULL;
    }
    for (size_t i = 0; i < BT_AUDIO_BDROID_MAX_LE_PEERS; i++) {
        if (s_host->ble_peers[i].valid && s_host->ble_peers[i].conn_handle == conn_handle) {
            return &s_host->ble_peers[i];
        }
    }
    return NULL;
}

static bt_audio_bluedroid_peer_t *bdroid_peer_find_addr(const uint8_t addr[6])
{
    if (!s_host || !addr) {
        return NULL;
    }
    for (size_t i = 0; i < BT_AUDIO_BDROID_MAX_LE_PEERS; i++) {
        if (s_host->ble_peers[i].valid &&
            memcmp(s_host->ble_peers[i].addr, addr, sizeof(s_host->ble_peers[i].addr)) == 0) {
            return &s_host->ble_peers[i];
        }
    }
    return NULL;
}

static bt_audio_bluedroid_peer_t *bdroid_peer_alloc(const uint8_t addr[6])
{
    bt_audio_bluedroid_peer_t *peer = bdroid_peer_find_addr(addr);
    if (peer) {
        return peer;
    }
    if (!s_host) {
        return NULL;
    }
    for (size_t i = 0; i < BT_AUDIO_BDROID_MAX_LE_PEERS; i++) {
        if (!s_host->ble_peers[i].valid) {
            return &s_host->ble_peers[i];
        }
    }
    return NULL;
}

static uint32_t bdroid_disc_timeout_to_duration(uint32_t timeout_ms)
{
    if (timeout_ms == 0) {
        return 0;
    }
    return (timeout_ms / BT_AUDIO_BDROID_SCAN_DURATION_UNIT_MS) +
           ((timeout_ms % BT_AUDIO_BDROID_SCAN_DURATION_UNIT_MS) ? 1 : 0);
}

static esp_err_t bdroid_acl_connected(uint16_t conn_handle, const bt_audio_addr_t *peer)
{
    if (!s_host || !peer) {
        return ESP_ERR_INVALID_ARG;
    }

    bt_audio_bluedroid_peer_t *slot = bdroid_peer_alloc(peer->val);
    if (!slot) {
        ESP_LOGE(TAG, "No free LE peer slot for conn_handle %u", conn_handle);
        return ESP_ERR_NO_MEM;
    }
    memcpy(slot->addr, peer->val, sizeof(slot->addr));
    slot->conn_handle = conn_handle;
    slot->valid = true;
    return ESP_OK;
}

static esp_err_t bdroid_acl_disconnected(uint16_t conn_handle)
{
    bt_audio_bluedroid_peer_t *peer = bdroid_peer_find_handle(conn_handle);
    if (!peer) {
        return ESP_OK;
    }

    memset(peer, 0, sizeof(*peer));
    return ESP_OK;
}

static esp_err_t bdroid_gap_op_begin(void)
{
    if (!s_host || !s_host->ble_gap_op_sem || !s_host->ble_gap_op_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_host->ble_gap_op_mutex,
                       pdMS_TO_TICKS(BT_AUDIO_BDROID_GAP_OP_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Acquire GAP operation lock timed out");
        return ESP_ERR_TIMEOUT;
    }
    while (xSemaphoreTake(s_host->ble_gap_op_sem, 0) == pdTRUE) {
    }
    s_host->ble_gap_op_status = ESP_BT_STATUS_FAIL;
    return ESP_OK;
}

static esp_err_t bdroid_gap_op_end(esp_err_t err)
{
    if (!s_host || !s_host->ble_gap_op_sem || !s_host->ble_gap_op_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret;
    if (err != ESP_OK) {
        ret = err;
    } else if (xSemaphoreTake(s_host->ble_gap_op_sem,
                              pdMS_TO_TICKS(BT_AUDIO_BDROID_GAP_OP_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "GAP operation completion timed out");
        ret = ESP_ERR_TIMEOUT;
    } else {
        ret = s_host->ble_gap_op_status == ESP_BT_STATUS_SUCCESS ? ESP_OK : ESP_FAIL;
    }
    xSemaphoreGive(s_host->ble_gap_op_mutex);
    return ret;
}

static void bdroid_gap_op_done(esp_bt_status_t status)
{
    if (!s_host || !s_host->ble_gap_op_sem) {
        return;
    }
    s_host->ble_gap_op_status = status;
    xSemaphoreGive(s_host->ble_gap_op_sem);
}

static void bluedroid_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    if (!param) {
        return;
    }

    switch (event) {
        case ESP_GAP_BLE_SET_LOCAL_PRIVACY_COMPLETE_EVT:
            bdroid_gap_op_done(param->local_privacy_cmpl.status);
            break;

        case ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT:
            bdroid_gap_op_done(param->set_ext_scan_params.status);
            break;

        case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT:
            bdroid_gap_op_done(param->ext_scan_start.status);
            break;

        case ESP_GAP_BLE_EXT_SCAN_STOP_COMPLETE_EVT:
            bdroid_gap_op_done(param->ext_scan_stop.status);
            break;

        case ESP_GAP_BLE_EXT_ADV_SET_PARAMS_COMPLETE_EVT:
            bdroid_gap_op_done(param->ext_adv_set_params.status);
            break;

        case ESP_GAP_BLE_EXT_ADV_DATA_SET_COMPLETE_EVT:
            bdroid_gap_op_done(param->ext_adv_data_set.status);
            break;

        case ESP_GAP_BLE_EXT_ADV_START_COMPLETE_EVT:
            bdroid_gap_op_done(param->ext_adv_start.status);
            break;

        case ESP_GAP_BLE_EXT_ADV_STOP_COMPLETE_EVT:
            bdroid_gap_op_done(param->ext_adv_stop.status);
            break;

        case ESP_GAP_BLE_PERIODIC_ADV_SET_PARAMS_COMPLETE_EVT:
            bdroid_gap_op_done(param->peroid_adv_set_params.status);
            break;

        case ESP_GAP_BLE_PERIODIC_ADV_DATA_SET_COMPLETE_EVT:
            bdroid_gap_op_done(param->period_adv_data_set.status);
            break;

        case ESP_GAP_BLE_PERIODIC_ADV_START_COMPLETE_EVT:
            bdroid_gap_op_done(param->period_adv_start.status);
            break;

        case ESP_GAP_BLE_PERIODIC_ADV_STOP_COMPLETE_EVT:
            bdroid_gap_op_done(param->period_adv_stop.status);
            break;

#if CONFIG_BT_BLE_FEAT_PERIODIC_ADV_SYNC_TRANSFER
        case ESP_GAP_BLE_SET_PAST_PARAMS_COMPLETE_EVT:
            bdroid_gap_op_done(param->set_past_params.status);
            break;
#endif  /* CONFIG_BT_BLE_FEAT_PERIODIC_ADV_SYNC_TRANSFER */

        case ESP_GAP_BLE_SEC_REQ_EVT:
            esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            break;

        case ESP_GAP_BLE_NC_REQ_EVT:
            esp_ble_confirm_reply(param->ble_security.ble_req.bd_addr, true);
            break;

        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            esp_ble_audio_gap_app_post_event(event, param);
            break;

        default:
            break;
    }
}

static esp_err_t bdroid_ext_adv_configure(uint8_t handle, const bt_audio_ext_adv_params_t *params)
{
    esp_ble_gap_ext_adv_params_t ep = {0};

    uint8_t props = 0;
    if (params->connectable) {
        props |= ESP_BLE_GAP_SET_EXT_ADV_PROP_CONNECTABLE;
    }
    if (params->scannable) {
        props |= ESP_BLE_GAP_SET_EXT_ADV_PROP_SCANNABLE;
    }
    if (params->legacy_pdu) {
        props |= ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY;
    }
    ep.type = props;
    ep.own_addr_type = params->own_addr_type;
    ep.primary_phy = params->primary_phy;
    ep.secondary_phy = params->secondary_phy;
    ep.tx_power = params->tx_power;
    ep.sid = params->sid;
    ep.interval_min = params->itvl_min;
    ep.interval_max = params->itvl_max;
    ep.channel_map = 0x07;  /* Channels 37, 38, and 39 */

    esp_err_t ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_ext_adv_set_params(handle, &ep));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set ext adv params failed: handle %u, %s", handle, esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

static esp_err_t bdroid_ext_adv_set_data(uint8_t handle, const uint8_t *data, size_t len)
{
    esp_err_t ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_config_ext_adv_data_raw(handle, (uint16_t)len, data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set ext adv data failed: handle %u, %s", handle, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_ext_adv_start(uint8_t handle, int duration, int max_events)
{
    esp_ble_gap_ext_adv_t adv = {
        .instance = handle,
        .duration = duration,
        .max_events = max_events,
    };
    esp_err_t ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_ext_adv_start(1, &adv));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start ext adv failed: handle %u, %s", handle, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_ext_adv_stop(uint8_t handle)
{
    esp_err_t ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_ext_adv_stop(1, &handle));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Stop ext adv failed: handle %u, %s", handle, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_disc(uint8_t own_addr_type, const bt_audio_scan_params_t *params,
                             uint32_t timeout_ms)
{
    esp_ble_ext_scan_params_t scan_params = {
        .own_addr_type = (esp_ble_addr_type_t)own_addr_type,
        .filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
        .scan_duplicate = params->filter_duplicates ? BLE_SCAN_DUPLICATE_ENABLE : BLE_SCAN_DUPLICATE_DISABLE,
        .cfg_mask = ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK,
        .uncoded_cfg = {
            .scan_type = params->passive ? BLE_SCAN_TYPE_PASSIVE : BLE_SCAN_TYPE_ACTIVE,
            .scan_interval = params->itvl,
            .scan_window = params->window,
        },
    };

    esp_err_t ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_set_ext_scan_params(&scan_params));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set ext scan params failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_start_ext_scan(bdroid_disc_timeout_to_duration(timeout_ms), 0));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start ext scan failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_disc_cancel(void)
{
    esp_err_t ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_stop_ext_scan());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Stop ext scan failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_connect(uint8_t own_addr_type, const bt_audio_addr_t *peer,
                                const bt_audio_conn_params_t *params, uint32_t timeout_ms)
{
    if (!s_host) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_bd_addr_t remote_addr = {0};
    esp_ble_gap_conn_params_t conn_params = {
        .scan_interval = params->scan_itvl,
        .scan_window = params->scan_window,
        .interval_min = params->itvl_min,
        .interval_max = params->itvl_max,
        .latency = params->latency,
        .supervision_timeout = params->supervision_timeout,
        .min_ce_len = params->min_ce_len,
        .max_ce_len = params->max_ce_len,
    };

    memcpy(remote_addr, peer->val, sizeof(remote_addr));

    esp_err_t ret = esp_ble_gap_prefer_ext_connect_params_set(remote_addr,
                                                             ESP_BLE_GAP_PHY_1M_PREF_MASK,
                                                             &conn_params, NULL, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set preferred ext connect params failed: %s", esp_err_to_name(ret));
        return ret;
    }

    esp_gatt_if_t gattc_if = (esp_gatt_if_t)esp_ble_audio_bluedroid_get_gattc_if();
    if (gattc_if == ESP_GATT_IF_NONE) {
        ESP_LOGE(TAG, "BLE Audio GATTC interface is not ready");
        return ESP_ERR_INVALID_STATE;
    }

    ret = esp_ble_gattc_aux_open(gattc_if, remote_addr, (esp_ble_addr_type_t)peer->type, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Open GATTC aux connection failed: %s", esp_err_to_name(ret));
        return ret;
    }

    return ESP_OK;
}

static esp_err_t bdroid_connect_cancel(const uint8_t *bt_dev_addr)
{
    if (!bt_dev_addr) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_gatt_if_t gattc_if = (esp_gatt_if_t)esp_ble_audio_bluedroid_get_gattc_if();
    if (gattc_if == ESP_GATT_IF_NONE) {
        ESP_LOGE(TAG, "BLE Audio GATTC interface is not ready");
        return ESP_ERR_INVALID_STATE;
    }
    esp_ble_gattc_cancel_open_params_t params = {
        .gattc_if = gattc_if,
    };
    memcpy(params.remote_bda, bt_dev_addr, sizeof(params.remote_bda));
    esp_err_t ret = esp_ble_gattc_cancel_open(&params);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Cancel pending connect failed: %s", esp_err_to_name(ret));
        return ret;
    }
    return ESP_OK;
}

static esp_err_t bdroid_disconnect(uint16_t conn_handle, uint8_t reason)
{
    bt_audio_bluedroid_peer_t *peer = bdroid_peer_find_handle(conn_handle);
    if (!peer) {
        ESP_LOGE(TAG, "Disconnect failed: no peer address cached for conn_handle %u", conn_handle);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = esp_ble_gap_disconnect(peer->addr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Disconnect GAP link failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_security_initiate(uint16_t conn_handle)
{
    bt_audio_bluedroid_peer_t *peer = bdroid_peer_find_handle(conn_handle);
    if (!peer) {
        ESP_LOGE(TAG, "Security initiate failed: no peer address cached for conn_handle %u", conn_handle);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = esp_ble_set_encryption(peer->addr, ESP_BLE_SEC_ENCRYPT_NO_MITM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set encryption failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_periodic_adv_configure(uint8_t adv_handle,
                                               const bt_audio_periodic_adv_params_t *params)
{
    esp_ble_gap_periodic_adv_params_t pp = {0};
    pp.interval_min = (uint16_t)params->itvl_min;
    pp.interval_max = (uint16_t)params->itvl_max;
    pp.properties = params->include_tx_power ? 0x40  /* Include TxPower */ : 0;

    esp_err_t ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_periodic_adv_set_params(adv_handle, &pp));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set periodic adv params failed: handle %u, %s", adv_handle, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_periodic_adv_set_data(uint8_t adv_handle, const uint8_t *data, size_t len)
{
    esp_err_t ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_config_periodic_adv_data_raw(adv_handle, (uint16_t)len, data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set periodic adv data failed: handle %u, %s", adv_handle, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_periodic_adv_start(uint8_t adv_handle)
{
    esp_err_t ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_periodic_adv_start(adv_handle));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Start periodic adv failed: handle %u, %s", adv_handle, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_periodic_adv_stop(uint8_t adv_handle)
{
    esp_err_t ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_periodic_adv_stop(adv_handle));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Stop periodic adv failed: handle %u, %s", adv_handle, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_pa_sync_create(const bt_audio_addr_t *addr, uint8_t sid,
                                       const bt_audio_periodic_sync_params_t *params)
{
    esp_ble_gap_periodic_adv_sync_params_t sp = {0};
    sp.filter_policy = 0;  /* Use the advertiser address and SID directly */
    sp.sid = sid;
    sp.addr_type = addr->type;
    memcpy(sp.addr, addr->val, sizeof(sp.addr));
    sp.skip = params->skip;
    sp.sync_timeout = params->sync_timeout;

    esp_err_t ret = esp_ble_gap_periodic_adv_create_sync(&sp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Create periodic adv sync failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_pa_sync_terminate(uint16_t sync_handle)
{
    esp_err_t ret = esp_ble_gap_periodic_adv_sync_terminate(sync_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Terminate PA sync failed: sync_handle %u, %s", sync_handle, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_pa_sync_create_cancel(void)
{
    esp_err_t ret = esp_ble_gap_periodic_adv_sync_cancel();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Cancel PA sync create failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bdroid_pa_sync_receive(uint16_t conn_handle,
                                        const bt_audio_periodic_sync_params_t *params)
{
#if CONFIG_BT_BLE_FEAT_PERIODIC_ADV_SYNC_TRANSFER
    bt_audio_bluedroid_peer_t *peer = bdroid_peer_find_handle(conn_handle);
    if (!peer) {
        ESP_LOGE(TAG, "PAST receive failed: no peer address cached for conn_handle %u", conn_handle);
        return ESP_ERR_NOT_FOUND;
    }

    esp_ble_gap_past_params_t past = {
        .mode = ESP_BLE_GAP_PAST_MODE_DUP_FILTER_DISABLED,
        .skip = params ? params->skip : 0,
        .sync_timeout = params ? params->sync_timeout : 1000,
        .cte_type = 0,
    };

    esp_err_t ret = bdroid_gap_op_begin();
    if (ret != ESP_OK) {
        return ret;
    }
    ret = bdroid_gap_op_end(esp_ble_gap_set_periodic_adv_sync_trans_params(peer->addr, &past));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Set PAST receive params failed: conn_handle %u, %s",
                 conn_handle, esp_err_to_name(ret));
    }
    return ret;
#else
    ESP_LOGW(TAG, "PAST receive is disabled in Bluedroid config");
    return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BLE_FEAT_PERIODIC_ADV_SYNC_TRANSFER */
}

static esp_err_t bdroid_id_infer_auto(int privacy, uint8_t *out_addr_type)
{
    *out_addr_type = privacy ? BLE_ADDR_TYPE_RPA_PUBLIC : BLE_ADDR_TYPE_PUBLIC;
    return ESP_OK;
}

static size_t bdroid_bond_count(void)
{
    int count = esp_ble_get_bond_device_num();
    return count > 0 ? (size_t)count : 0;
}

static bool bdroid_rpa_matches_irk(const uint8_t rpa[6], const uint8_t irk[16])
{
    uint8_t key[16];
    uint8_t plaintext[16] = {0};
    uint8_t ciphertext[16];
    struct tc_aes_key_sched_struct sched;

    for (size_t i = 0; i < sizeof(key); i++) {
        key[i] = irk[sizeof(key) - 1 - i];
    }
    plaintext[13] = rpa[0];
    plaintext[14] = rpa[1];
    plaintext[15] = rpa[2];

    if (tc_aes128_set_encrypt_key(&sched, key) == TC_CRYPTO_FAIL ||
        tc_aes_encrypt(ciphertext, plaintext, &sched) == TC_CRYPTO_FAIL) {
        memset(&sched, 0, sizeof(sched));
        return false;
    }
    memset(&sched, 0, sizeof(sched));
    return ciphertext[15] == rpa[5] &&
           ciphertext[14] == rpa[4] &&
           ciphertext[13] == rpa[3];
}

static bool bdroid_bond_exists(const bt_audio_addr_t *addr)
{
    esp_ble_bond_dev_t *peers = NULL;
    bool found = false;
    int count;

    if (!addr) {
        return false;
    }
    count = esp_ble_get_bond_device_num();
    if (count <= 0) {
        return false;
    }
    peers = heap_caps_calloc(count, sizeof(*peers), MALLOC_CAP_DEFAULT);
    if (!peers) {
        return false;
    }
    if (esp_ble_get_bond_device_list(&count, peers) == ESP_OK) {
        for (int i = 0; i < count; i++) {
            bool identity_match =
                memcmp(peers[i].bd_addr, addr->val, sizeof(addr->val)) == 0 &&
                peers[i].bd_addr_type == addr->type;
            bool rpa_match =
                addr->type == BLE_ADDR_TYPE_RANDOM &&
                (addr->val[0] & 0xc0U) == 0x40U &&
                (peers[i].bond_key.key_mask & ESP_BLE_ID_KEY_MASK) &&
                bdroid_rpa_matches_irk(addr->val, peers[i].bond_key.pid_key.irk);
            if (identity_match || rpa_match) {
                found = true;
                break;
            }
        }
    }
    heap_caps_free(peers);
    return found;
}

static const char *bdroid_svc_gap_device_name(void)
{
    if (!s_host) {
        return NULL;
    }
    return s_host->ble_dev_name[0] ? s_host->ble_dev_name : NULL;
}

static esp_err_t bdroid_register_event_cb(void)
{
    if (s_host && s_host->gap_cb_registered) {
        return ESP_OK;
    }
    esp_err_t ret = esp_ble_gap_register_callback(bluedroid_gap_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Register GAP callback failed: %s", esp_err_to_name(ret));
        return ret;
    }
    if (s_host) {
        s_host->gap_cb_registered = true;
    }
    ESP_RETURN_ON_ERROR(bdroid_gap_op_begin(), TAG, "Prepare local privacy configuration failed");
    return bdroid_gap_op_end(esp_ble_gap_config_local_privacy(true));
}

static void bdroid_post_gap_event(uint8_t gap_event_type, void *event)
{
    /* BlueDroid sources LE Audio GAP/GATT events inside the esp_ble_audio
     * middleware and delivers them to the registered app callback directly,
     * so no application-level forwarding is required here.
     */
}

static esp_err_t bdroid_ble_stack_setup(esp_bt_audio_host_bluedroid_cfg_t *host_cfg)
{
    esp_ble_auth_req_t auth_req = ESP_LE_AUTH_REQ_SC_BOND;
    esp_ble_io_cap_t ble_iocap = ESP_IO_CAP_NONE;
    uint8_t key_size = 16;
    uint8_t init_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rsp_key = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth_req, sizeof(auth_req)),
                        TAG, "Set BLE auth req failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &ble_iocap, sizeof(ble_iocap)),
                        TAG, "Set BLE IO cap failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size, sizeof(key_size)),
                        TAG, "Set BLE key size failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key, sizeof(init_key)),
                        TAG, "Set BLE init key failed");
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rsp_key, sizeof(rsp_key)),
                        TAG, "Set BLE rsp key failed");

    int bond_count = esp_ble_get_bond_device_num();
    if (bond_count > 0) {
        esp_ble_bond_dev_t *bonds = heap_caps_calloc(bond_count, sizeof(*bonds), MALLOC_CAP_DEFAULT);
        ESP_RETURN_ON_FALSE(bonds != NULL, ESP_ERR_NO_MEM, TAG, "No memory for BLE bond list");
        if (esp_ble_get_bond_device_list(&bond_count, bonds) == ESP_OK) {
            for (int i = 0; i < bond_count; i++) {
                if (!(bonds[i].bond_key.key_mask & ESP_BLE_ID_KEY_MASK)) {
                    continue;
                }
                esp_err_t ret = esp_ble_gap_add_device_to_resolving_list(
                    bonds[i].bond_key.pid_key.static_addr,
                    bonds[i].bond_key.pid_key.addr_type,
                    bonds[i].bond_key.pid_key.irk);
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Add bonded peer to resolving list failed: %s",
                             esp_err_to_name(ret));
                }
            }
        }
        heap_caps_free(bonds);
    }

    snprintf(s_host->ble_dev_name, sizeof(s_host->ble_dev_name), "%s", host_cfg->dev_name);
    ESP_RETURN_ON_ERROR(esp_ble_gap_set_device_name(s_host->ble_dev_name), TAG, "Set BLE device name failed");

    if (!s_host->ble_gap_op_sem) {
        s_host->ble_gap_op_sem = xSemaphoreCreateBinary();
        ESP_RETURN_ON_FALSE(s_host->ble_gap_op_sem != NULL, ESP_ERR_NO_MEM, TAG, "Create BLE GAP op semaphore failed");
    }
    if (!s_host->ble_gap_op_mutex) {
        s_host->ble_gap_op_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_host->ble_gap_op_mutex != NULL, ESP_ERR_NO_MEM, TAG, "Create BLE GAP op mutex failed");
    }

    static const bt_audio_host_ops_t ops = {
        .ext_adv_configure = bdroid_ext_adv_configure,
        .ext_adv_set_data = bdroid_ext_adv_set_data,
        .ext_adv_start = bdroid_ext_adv_start,
        .ext_adv_stop = bdroid_ext_adv_stop,
        .disc = bdroid_disc,
        .disc_cancel = bdroid_disc_cancel,
        .connect = bdroid_connect,
        .connect_cancel = bdroid_connect_cancel,
        .disconnect = bdroid_disconnect,
        .acl_connected = bdroid_acl_connected,
        .acl_disconnected = bdroid_acl_disconnected,
        .security_initiate = bdroid_security_initiate,
        .periodic_adv_configure = bdroid_periodic_adv_configure,
        .periodic_adv_set_data = bdroid_periodic_adv_set_data,
        .periodic_adv_start = bdroid_periodic_adv_start,
        .periodic_adv_stop = bdroid_periodic_adv_stop,
        .pa_sync_create = bdroid_pa_sync_create,
        .pa_sync_terminate = bdroid_pa_sync_terminate,
        .pa_sync_create_cancel = bdroid_pa_sync_create_cancel,
        .pa_sync_receive = bdroid_pa_sync_receive,
        .id_infer_auto = bdroid_id_infer_auto,
        .bond_count = bdroid_bond_count,
        .bond_exists = bdroid_bond_exists,
        .svc_gap_device_name = bdroid_svc_gap_device_name,
        .register_event_cb = bdroid_register_event_cb,
        .post_gap_event = bdroid_post_gap_event,
    };
    bt_audio_ops_set_host(&ops);
    return ESP_OK;
}

static void bdroid_ble_stack_teardown(void)
{
    if (s_host->ble_gap_op_sem) {
        vSemaphoreDelete(s_host->ble_gap_op_sem);
        s_host->ble_gap_op_sem = NULL;
    }
    if (s_host->ble_gap_op_mutex) {
        vSemaphoreDelete(s_host->ble_gap_op_mutex);
        s_host->ble_gap_op_mutex = NULL;
    }
}
#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO */

static void bt_audio_host_bluedroid_init(void)
{
    if (s_host == NULL) {
        s_host = heap_caps_calloc_prefer(1, sizeof(*s_host), 2,
                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
        if (s_host == NULL) {
            ESP_LOGE(TAG, "Allocate host context failed");
            return;
        }
    }
}

static char *bda2str(esp_bd_addr_t bda, char *str, size_t size)
{
    if (bda == NULL || str == NULL || size < 18) {
        return NULL;
    }
    snprintf(str, size, "%02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return str;
}

static bool get_name_from_eir(uint8_t *eir, uint8_t *bdname, size_t bdname_size,
                              uint8_t *bdname_len)
{
    uint8_t *rmt_bdname = NULL;
    uint8_t rmt_bdname_len = 0;
    size_t copy_len = 0;

    if (!eir) {
        return false;
    }

    rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &rmt_bdname_len);
    if (!rmt_bdname) {
        rmt_bdname = esp_bt_gap_resolve_eir_data(eir, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &rmt_bdname_len);
    }

    if (rmt_bdname) {
        copy_len = rmt_bdname_len;
        if (copy_len > ESP_BT_GAP_MAX_BDNAME_LEN) {
            copy_len = ESP_BT_GAP_MAX_BDNAME_LEN;
        }
        if (bdname && bdname_size > 0 && copy_len >= bdname_size) {
            copy_len = bdname_size - 1;
        }

        if (bdname && bdname_size > 0) {
            memcpy(bdname, rmt_bdname, copy_len);
            bdname[copy_len] = '\0';
        }
        if (bdname_len) {
            *bdname_len = copy_len;
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
        if (get_name_from_eir(eir, (uint8_t *)event_data.name, sizeof(event_data.name),
                              &bdname_len)) {
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

esp_err_t bt_audio_host_bluedroid_stack_init(void *cfg)
{
    esp_err_t ret = ESP_OK;
    bool bluedroid_inited = false;
    bool bluedroid_enabled = false;

    ESP_RETURN_ON_FALSE(cfg != NULL, ESP_ERR_INVALID_ARG, TAG, "cfg is NULL");
    esp_bt_audio_host_bluedroid_cfg_t *host_cfg = (esp_bt_audio_host_bluedroid_cfg_t *)cfg;

    bt_audio_host_bluedroid_init();
    ESP_RETURN_ON_FALSE(s_host != NULL, ESP_ERR_NO_MEM, TAG, "Allocate host context failed");

    ESP_GOTO_ON_ERROR(esp_bluedroid_init_with_cfg(&host_cfg->bluedroid_cfg), error, TAG, "Bluedroid init failed");
    bluedroid_inited = true;
    ESP_GOTO_ON_ERROR(esp_bluedroid_enable(), error, TAG, "Bluedroid enable failed");
    bluedroid_enabled = true;
    if (host_cfg->bluedroid_cfg.ssp_en) {
        ESP_GOTO_ON_ERROR(esp_bt_gap_set_security_param(host_cfg->sp_param, &host_cfg->iocap, sizeof(uint8_t)),
                          error, TAG, "Set security param failed");
    } else {
        ESP_GOTO_ON_ERROR(esp_bt_gap_set_pin(host_cfg->pin_type, 4, host_cfg->pin_code), error, TAG, "Set PIN code failed");
    }
    ESP_GOTO_ON_ERROR(esp_bt_gap_set_device_name(host_cfg->dev_name), error, TAG, "Set device name failed");
    ESP_GOTO_ON_ERROR(esp_bt_gap_register_callback(bluedroid_host_gap_cb), error, TAG, "Register GAP callback failed");

#if CONFIG_BT_AUDIO && CONFIG_BT_ISO
    ESP_GOTO_ON_ERROR(bdroid_ble_stack_setup(host_cfg), error, TAG, "BLE stack setup failed");
#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO */

    ESP_GOTO_ON_ERROR(bluedroid_host_set_ops(), error, TAG, "Set host ops failed");

    return ESP_OK;

error:
    if (bluedroid_enabled) {
        esp_bluedroid_disable();
    }
    if (bluedroid_inited) {
        esp_bluedroid_deinit();
    }
    if (s_host) {
#if CONFIG_BT_AUDIO && CONFIG_BT_ISO
        bdroid_ble_stack_teardown();
#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO */
        heap_caps_free(s_host);
        s_host = NULL;
    }
    return ret;
}

void bt_audio_host_bluedroid_stack_deinit(void)
{
#if CONFIG_BT_CLASSIC_ENABLED
    esp_bt_gap_cancel_discovery();
#endif  /* CONFIG_BT_CLASSIC_ENABLED */
    esp_bluedroid_disable();
    esp_bluedroid_deinit();
    if (s_host) {
#if CONFIG_BT_AUDIO && CONFIG_BT_ISO
        bdroid_ble_stack_teardown();
#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO */
        heap_caps_free(s_host);
        s_host = NULL;
    }
}
