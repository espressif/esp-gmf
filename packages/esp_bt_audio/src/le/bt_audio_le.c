/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "bt_audio_host_ops.h"
#include "bt_audio_ops.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_defs.h"
#if CONFIG_BT_TMAP
#include "esp_ble_audio_tmap_api.h"
#endif  /* CONFIG_BT_TMAP */

#include "bt_audio_le_adv_builder.h"
#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_le.h"
#if CONFIG_BT_BAP_BROADCAST_SINK
#include "bt_audio_le_broadcast_sink.h"
#endif  /* CONFIG_BT_BAP_BROADCAST_SINK */
#if CONFIG_BT_BAP_BROADCAST_SOURCE
#include "bt_audio_le_broadcast_source.h"
#endif  /* CONFIG_BT_BAP_BROADCAST_SOURCE */
#if CONFIG_BT_TBS_CLIENT
#include "bt_audio_le_ccp.h"
#endif  /* CONFIG_BT_TBS_CLIENT */
#if CONFIG_BT_CSIP_SET_MEMBER
#include "bt_audio_le_csip.h"
#endif  /* CONFIG_BT_CSIP_SET_MEMBER */
#if CONFIG_BT_MCC
#include "bt_audio_le_mcc.h"
#endif  /* CONFIG_BT_MCC */
#if CONFIG_BT_MICP_MIC_DEV
#include "bt_audio_le_micp.h"
#endif  /* CONFIG_BT_MICP_MIC_DEV */
#include "bt_audio_le_pacs.h"
#if CONFIG_BT_BAP_SCAN_DELEGATOR
#include "bt_audio_le_scan_delegator.h"
#endif  /* CONFIG_BT_BAP_SCAN_DELEGATOR */
#if CONFIG_BT_BAP_UNICAST_SERVER
#include "bt_audio_le_unicast_server.h"
#endif  /* CONFIG_BT_BAP_UNICAST_SERVER */
#if CONFIG_BT_VCP_VOL_REND
#include "bt_audio_le_vcp_rend.h"
#endif  /* CONFIG_BT_VCP_VOL_REND */

#define BT_AUDIO_LE_ADV_HANDLE               0x00
#define BT_AUDIO_LE_ADV_BUFFER_SIZE          128
#define BT_AUDIO_LE_SCAN_TIMEOUT_MS          10000
/* Broadcast Audio Announcement: Broadcast_Name AD type (assigned number 0x30) */
#define BT_AUDIO_LE_ADV_TYPE_BROADCAST_NAME  0x30U

/**
 * @brief  Runtime context for the LE Audio coordinator.
 */
typedef struct {
    esp_ble_audio_start_info_t  start_info;                             /*!< Common BLE Audio start parameters */
    bt_audio_le_adv_builder_t   adv_builder;                            /*!< Extended advertising data builder */
    uint8_t                     adv_data[BT_AUDIO_LE_ADV_BUFFER_SIZE];  /*!< Extended advertising data buffer */
    uint8_t                     connect_target[6];                      /*!< Pending scan target address */
    uint16_t                    conn_handle;                            /*!< Current LE ACL connection handle */
    bool                        scan_running            : 1;            /*!< True when GAP discovery is active */
    bool                        inited_common           : 1;            /*!< Common BLE Audio layer has been initialized */
    bool                        inited_pacs             : 1;            /*!< PACS module has been initialized */
    bool                        inited_unicast_server   : 1;            /*!< Unicast server module has been initialized */
    bool                        inited_broadcast_sink   : 1;            /*!< Broadcast sink module has been initialized */
    bool                        inited_broadcast_source : 1;            /*!< Broadcast source module has been initialized */
    bool                        inited_scan_delegator   : 1;            /*!< Scan delegator module has been initialized */
    bool                        inited_csip             : 1;            /*!< CSIP module has been initialized */
    bool                        inited_mcc              : 1;            /*!< MCC module has been initialized */
    bool                        inited_micp             : 1;            /*!< MICP module has been initialized */
    bool                        inited_ccp              : 1;            /*!< CCP module has been initialized */
    bool                        inited_vcp_rend         : 1;            /*!< VCP renderer module has been initialized */
    bool                        started                 : 1;            /*!< Common BLE Audio layer has been started */
    bool                        broadcast_source_started : 1;           /*!< Broadcast source audio path has been started */
    bool                        adv_configured          : 1;            /*!< Extended advertising set has been configured */
    bool                        adv_enabled             : 1;            /*!< Extended advertising is allowed to run */
    bool                        adv_running             : 1;            /*!< Extended advertising is running */
    bool                        periodic_adv_running    : 1;            /*!< Periodic advertising is running */
    bool                        user_connected_notified : 1;            /*!< User has been notified of the current LE connection */
    esp_bt_audio_le_cfg_t       cfg;                                    /*!< Cached user configuration */
} bt_audio_le_ctx_t;

static const char *TAG = "BT_AUD_LE";
static bt_audio_le_ctx_t *s_le;
static esp_err_t bt_audio_le_start_ext_adv(void);

static esp_err_t bt_audio_le_start_periodic_adv(void)
{
#if CONFIG_BT_BAP_BROADCAST_SOURCE
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    if (!s_le->inited_broadcast_source || s_le->periodic_adv_running) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(bt_audio_le_broadcast_source_start_periodic_adv(BT_AUDIO_LE_ADV_HANDLE),
                        TAG, "Failed to start broadcast source periodic advertising");
    s_le->periodic_adv_running = true;
#endif  /* CONFIG_BT_BAP_BROADCAST_SOURCE */
    return ESP_OK;
}

static esp_err_t bt_audio_le_stop_periodic_adv(void)
{
#if CONFIG_BT_BAP_BROADCAST_SOURCE
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    if (!s_le->inited_broadcast_source || !s_le->periodic_adv_running) {
        return ESP_OK;
    }

    esp_err_t err = bt_audio_host_periodic_adv_stop(BT_AUDIO_LE_ADV_HANDLE);
    ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to stop broadcast source periodic advertising");
    s_le->periodic_adv_running = false;
#endif  /* CONFIG_BT_BAP_BROADCAST_SOURCE */
    return ESP_OK;
}

static inline void bt_audio_le_stop_ext_adv(void)
{
    if (!s_le->adv_running) {
        return;
    }

    esp_err_t err = bt_audio_host_ext_adv_stop(BT_AUDIO_LE_ADV_HANDLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop extended advertising: %s", esp_err_to_name(err));
        return;
    }
    s_le->adv_running = false;
}

static inline uint16_t bt_audio_le_get_u16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static inline uint32_t bt_audio_le_get_u24(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16);
}

static inline bool bt_audio_le_pacs_enabled(const esp_bt_audio_le_pacs_cfg_t *pacs)
{
    return pacs->sink_enabled || pacs->source_enabled ||
           pacs->sink_locations || pacs->source_locations ||
           pacs->sink_context_mask || pacs->source_context_mask;
}

#if CONFIG_BT_MCC && CONFIG_BT_TBS_CLIENT
static void bt_audio_le_mcc_discover_after_ccp(uint16_t conn_handle, void *user_ctx)
{
    if (s_le && s_le->inited_mcc) {
        bt_audio_le_mcc_discover(conn_handle);
    }
}
#endif  /* CONFIG_BT_MCC && CONFIG_BT_TBS_CLIENT */

#if CONFIG_BT_TMAP
static void bt_audio_le_tmap_discovery_complete(esp_ble_audio_tmap_role_t peer_role,
                                                esp_ble_conn_t *conn,
                                                int err)
{
    if (!s_le || !conn) {
        return;
    }

    ESP_LOGI(TAG, "TMAP discovery complete, err %d, conn_handle %u, peer_role 0x%X",
             err, conn->handle, peer_role);
    if (err) {
        return;
    }

#if CONFIG_BT_TBS_CLIENT
    if (s_le->inited_ccp) {
        bt_audio_le_ccp_discover(conn->handle);
        return;
    }
#endif  /* CONFIG_BT_TBS_CLIENT */
#if CONFIG_BT_MCC
    if (s_le->inited_mcc) {
        bt_audio_le_mcc_discover(conn->handle);
    }
#endif  /* CONFIG_BT_MCC */
}
#endif  /* CONFIG_BT_TMAP */

static inline void bt_audio_le_parse_ad(const uint8_t *ad, uint8_t ad_len,
                                        bool (*func)(uint8_t type, const uint8_t *data, uint8_t data_len, void *user_data),
                                        void *user_data)
{
    uint8_t offset = 0;

    while (offset < ad_len) {
        uint8_t len = ad[offset];
        if (len == 0) {
            return;
        }
        if (len > ad_len - offset) {
            ESP_LOGW(TAG, "Malformed advertising data");
            return;
        }
        if (!func(ad[offset + 1], ad + offset + 2, len - 1, user_data)) {
            return;
        }
        offset += len + 1;
    }
}

static bool bt_audio_le_device_found(uint8_t type, const uint8_t *data, uint8_t data_len, void *user_data)
{
    esp_bt_audio_event_device_discovered_t *disc = user_data;

    switch (type) {
        case BT_AUDIO_AD_TYPE_INCOMP_NAME:
        case BT_AUDIO_AD_TYPE_COMP_NAME:
            memcpy(disc->name, data, MIN(data_len, sizeof(disc->name) - 1));
            break;
        case BT_AUDIO_LE_ADV_TYPE_BROADCAST_NAME: {
            size_t bn_cap = sizeof(disc->disc_data.le.broadcast_name) - 1;
            memcpy(disc->disc_data.le.broadcast_name, data, MIN((size_t)data_len, bn_cap));
            break;
        }
        case BT_AUDIO_AD_TYPE_SVC_DATA_UUID16:
            if (data_len >= ESP_BLE_AUDIO_UUID_SIZE_16 + ESP_BLE_AUDIO_BROADCAST_ID_SIZE &&
                bt_audio_le_get_u16(data) == ESP_BLE_AUDIO_UUID_BROADCAST_AUDIO_VAL) {
                disc->disc_data.le.broadcast_id = bt_audio_le_get_u24(data + ESP_BLE_AUDIO_UUID_SIZE_16);
            }
            break;
        case BT_AUDIO_AD_TYPE_INCOMP_UUIDS16:
        case BT_AUDIO_AD_TYPE_COMP_UUIDS16:
            for (uint8_t i = 0; i + ESP_BLE_AUDIO_UUID_SIZE_16 <= data_len; i += ESP_BLE_AUDIO_UUID_SIZE_16) {
                uint16_t uuid = bt_audio_le_get_u16(data + i);
                if (uuid == ESP_BLE_AUDIO_UUID_BASS_VAL) {
                    disc->disc_data.le.bass_included = true;
                } else if (uuid == ESP_BLE_AUDIO_UUID_PACS_VAL) {
                    disc->disc_data.le.pacs_included = true;
                }
            }
            break;
        default:
            break;
    }
    return true;
}

static esp_err_t bt_audio_le_start_ext_adv(void)
{
    bt_audio_ext_adv_params_t params = {0};
    size_t adv_len = 0;
    esp_err_t err;

    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    if (s_le->adv_running) {
        return ESP_OK;
    }

    params.connectable = !s_le->inited_broadcast_source;
    params.scannable = false;
    params.legacy_pdu = false;
    params.own_addr_type = BT_AUDIO_OWN_ADDR_PUBLIC;
    params.primary_phy = BT_AUDIO_LE_PHY_1M;
    params.secondary_phy = BT_AUDIO_LE_PHY_2M;
    params.tx_power = 127;
    params.sid = 0;
    params.itvl_min = BT_AUDIO_ADV_ITVL_MS(200);
    params.itvl_max = BT_AUDIO_ADV_ITVL_MS(200);

    if (!s_le->adv_configured) {
        err = bt_audio_host_ext_adv_configure(BT_AUDIO_LE_ADV_HANDLE, &params);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to configure extended advertising");

        ESP_RETURN_ON_ERROR(bt_audio_le_adv_builder_get_buffer(s_le->adv_builder, s_le->adv_data,
                                                               sizeof(s_le->adv_data), &adv_len),
                            TAG, "Failed to build extended advertising data");

        err = bt_audio_host_ext_adv_set_data(BT_AUDIO_LE_ADV_HANDLE, s_le->adv_data, adv_len);
        ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to set extended advertising data");
        s_le->adv_configured = true;
    }

    err = bt_audio_host_ext_adv_start(BT_AUDIO_LE_ADV_HANDLE, 0, 0);
    ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to start extended advertising");
    s_le->adv_running = true;
    return ESP_OK;
}

static esp_err_t bt_audio_le_start_adv(void)
{
    ESP_RETURN_ON_ERROR(bt_audio_le_start_ext_adv(), TAG, "Failed to start LE advertising");

    esp_err_t err = bt_audio_le_start_periodic_adv();
    if (err != ESP_OK) {
        bt_audio_le_stop_ext_adv();
        ESP_LOGE(TAG, "Failed to start LE periodic advertising: %s", esp_err_to_name(err));
        return err;
    }
    return ESP_OK;
}

esp_err_t esp_bt_audio_le_set_advertising(bool enable)
{
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    ESP_RETURN_ON_FALSE(s_le->started, ESP_ERR_INVALID_STATE, TAG, "LE Audio not started");

    s_le->adv_enabled = enable;
    if (enable) {
        return bt_audio_le_start_adv();
    }

    if (!s_le->adv_running) {
        return bt_audio_le_stop_periodic_adv();
    }

    ESP_RETURN_ON_ERROR(bt_audio_le_stop_periodic_adv(), TAG, "Failed to stop LE periodic advertising");
    esp_err_t err = bt_audio_host_ext_adv_stop(BT_AUDIO_LE_ADV_HANDLE);
    ESP_RETURN_ON_FALSE(err == ESP_OK, ESP_FAIL, TAG, "Failed to stop extended advertising");
    s_le->adv_running = false;
    return ESP_OK;
}

static esp_err_t bt_audio_le_start_scan(const uint8_t *target, uint32_t timeout_ms)
{
    bt_audio_scan_params_t params = {0};
    uint8_t own_addr_type = 0;

    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    ESP_RETURN_ON_FALSE(!s_le->scan_running, ESP_ERR_INVALID_STATE, TAG, "Scan already running");

    if (target) {
        memcpy(s_le->connect_target, target, sizeof(s_le->connect_target));
    }
    if (timeout_ms == 0) {
        timeout_ms = BT_AUDIO_LE_SCAN_TIMEOUT_MS;
    }

    esp_err_t ret = bt_audio_host_id_infer_auto(0, &own_addr_type);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "Failed to infer own addr type");

    params.passive = true;
    params.itvl = 160;
    params.window = 160;
    params.filter_duplicates = true;
    ret = bt_audio_host_disc(own_addr_type, &params, timeout_ms);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "Failed to start scan");

    s_le->scan_running = true;
    esp_bt_audio_event_discovery_st_t event = {
        .tech = ESP_BT_AUDIO_TECH_LE,
        .discovering = true,
    };
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_DISCOVERY_STATE_CHG, &event);
    return ESP_OK;
}

static esp_err_t bt_audio_le_stop_scan(void)
{
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");

    esp_err_t err = bt_audio_host_disc_cancel();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Stop scan failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    s_le->scan_running = false;
    memset(s_le->connect_target, 0, sizeof(s_le->connect_target));
    esp_bt_audio_event_discovery_st_t event = {
        .tech = ESP_BT_AUDIO_TECH_LE,
        .discovering = false,
    };
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_DISCOVERY_STATE_CHG, &event);
    return ESP_OK;
}

static void bt_audio_le_iso_gap_cb(esp_ble_audio_gap_app_event_t *event)
{
    if (!s_le || !event) {
        return;
    }

    switch (event->type) {
        case ESP_BLE_AUDIO_GAP_EVENT_EXT_SCAN_RECV: {
            esp_bt_audio_event_device_discovered_t disc = {
                .tech = ESP_BT_AUDIO_TECH_LE,
                .rssi = event->ext_scan_recv.rssi,
            };
            memcpy(disc.addr, event->ext_scan_recv.addr.val, sizeof(disc.addr));
            disc.disc_data.le.addr_type = event->ext_scan_recv.addr.type;
            disc.disc_data.le.sid = event->ext_scan_recv.sid;
            disc.disc_data.le.broadcast_id = ESP_BLE_AUDIO_BAP_INVALID_BROADCAST_ID;
            bt_audio_le_parse_ad(event->ext_scan_recv.data,
                                 event->ext_scan_recv.data_len,
                                 bt_audio_le_device_found,
                                 &disc);
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_DEVICE_DISCOVERED, &disc);
#if CONFIG_BT_BAP_BROADCAST_SINK
            bt_audio_le_broadcast_sink_on_device(&disc);
#endif  /* CONFIG_BT_BAP_BROADCAST_SINK */
            break;
        }
        case ESP_BLE_AUDIO_GAP_EVENT_PA_SYNC:
            if (event->pa_sync.status == 0 && s_le->scan_running) {
                bt_audio_le_stop_scan();
            }
            break;
        case ESP_BLE_AUDIO_GAP_EVENT_ACL_CONNECT: {
            if (event->acl_connect.status == 0) {
                bt_audio_addr_t peer = {
                    .type = event->acl_connect.dst.type,
                };
                memcpy(peer.val, event->acl_connect.dst.val, sizeof(peer.val));
                bt_audio_host_acl_connected(event->acl_connect.conn_handle, &peer);
                if (s_le) {
                    s_le->conn_handle = event->acl_connect.conn_handle;
                    s_le->adv_running = false;
                    s_le->user_connected_notified = false;
                    memcpy(s_le->connect_target, event->acl_connect.dst.val, sizeof(s_le->connect_target));
                }
                ESP_LOGI(TAG, "LE connected, conn_handle %u", event->acl_connect.conn_handle);
                bt_audio_host_security_initiate(event->acl_connect.conn_handle);
            }
            break;
        }
        case ESP_BLE_AUDIO_GAP_EVENT_SECURITY_CHANGE: {
            if (event->security_change.status == 0) {
                esp_bt_audio_event_connection_st_t conn = {
                    .tech = ESP_BT_AUDIO_TECH_LE,
                    .connected = true,
                };
                if (s_le) {
                    memcpy(conn.addr, s_le->connect_target, sizeof(conn.addr));
                    s_le->user_connected_notified = true;
                }
                ESP_LOGI(TAG, "LE security established, conn_handle %u", event->security_change.conn_handle);
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG, &conn);
            }
            break;
        }
        case ESP_BLE_AUDIO_GAP_EVENT_ACL_DISCONNECT: {
            bool notify_user = s_le && s_le->user_connected_notified;
            esp_bt_audio_event_connection_st_t conn = {
                .tech = ESP_BT_AUDIO_TECH_LE,
                .connected = false,
            };
            if (s_le) {
                bt_audio_host_acl_disconnected(s_le->conn_handle);
#if CONFIG_BT_MCC
                if (s_le->inited_mcc) {
                    bt_audio_le_mcc_on_disconnect();
                }
#endif  /* CONFIG_BT_MCC */
#if CONFIG_BT_TBS_CLIENT
                if (s_le->inited_ccp) {
                    bt_audio_le_ccp_on_disconnect();
                }
#endif  /* CONFIG_BT_TBS_CLIENT */
                memcpy(conn.addr, s_le->connect_target, sizeof(conn.addr));
                s_le->user_connected_notified = false;
                s_le->conn_handle = 0;
                memset(s_le->connect_target, 0, sizeof(s_le->connect_target));
            }
            ESP_LOGI(TAG, "LE disconnected, reason 0x%02x", event->acl_disconnect.reason);
            if (notify_user) {
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG, &conn);
            }
            if (s_le && s_le->started && s_le->adv_enabled) {
                esp_err_t ret = bt_audio_le_start_adv();
                if (ret != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to restart LE advertising: %s", esp_err_to_name(ret));
                }
            }
            break;
        }
        default:
            break;
    }

#if CONFIG_BT_BAP_BROADCAST_SINK
    bt_audio_le_broadcast_sink_on_gap_event(event);
#endif  /* CONFIG_BT_BAP_BROADCAST_SINK */
}

static void bt_audio_le_iso_gatt_cb(esp_ble_audio_gatt_app_event_t *event)
{
    if (!event) {
        return;
    }

    switch (event->type) {
        case ESP_BLE_AUDIO_GATT_EVENT_GATT_MTU_CHANGE: {
            ESP_LOGI(TAG, "GATT MTU change, conn_handle %u, mtu %u",
                     event->gatt_mtu_change.conn_handle,
                     event->gatt_mtu_change.mtu);
            if (event->gatt_mtu_change.mtu < ESP_BLE_AUDIO_ATT_MTU_MIN) {
                ESP_LOGW(TAG, "Invalid new MTU %u, shall be at least %u",
                         event->gatt_mtu_change.mtu,
                         ESP_BLE_AUDIO_ATT_MTU_MIN);
                break;
            }
            esp_err_t ret = esp_ble_audio_gattc_disc_start(event->gatt_mtu_change.conn_handle);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start GATT discovery: %s", esp_err_to_name(ret));
                break;
            }
            ESP_LOGI(TAG, "Start discovering GATT services");
            break;
        }
        case ESP_BLE_AUDIO_GATT_EVENT_GATTC_DISC_CMPL:
            ESP_LOGI(TAG, "GATT discovery complete, status %u, conn_handle %u",
                     event->gattc_disc_cmpl.status,
                     event->gattc_disc_cmpl.conn_handle);
            if (event->gattc_disc_cmpl.status) {
                ESP_LOGE(TAG, "GATT discovery failed, status %u", event->gattc_disc_cmpl.status);
                break;
            }
            if (s_le && s_le->cfg.user_case == ESP_BT_AUDIO_LE_USER_CASE_TMAP) {
#if CONFIG_BT_TMAP
                static esp_ble_audio_tmap_cb_t tmap_cbs = {
                    .discovery_complete = bt_audio_le_tmap_discovery_complete,
                };
                esp_ble_audio_tmap_discover(event->gattc_disc_cmpl.conn_handle, &tmap_cbs);
#else
                ESP_LOGW(TAG, "TMAP discovery skipped because CONFIG_BT_TMAP is disabled");
#endif  /* CONFIG_BT_TMAP */
            }
            break;
        default:
            break;
    }
}

static esp_err_t bt_audio_le_connect(uint8_t addr_type, const uint8_t *bt_dev_addr, uint32_t timeout_ms)
{
    bt_audio_conn_params_t params = {0};
    bt_audio_addr_t peer = {0};
    uint8_t own_addr_type = 0;

    ESP_RETURN_ON_FALSE(s_le && bt_dev_addr, ESP_ERR_INVALID_ARG, TAG, "Invalid connect args");

    esp_err_t ret = bt_audio_host_id_infer_auto(0, &own_addr_type);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "Failed to infer own addr type");

    peer.type = addr_type;
    memcpy(peer.val, bt_dev_addr, sizeof(peer.val));
    params.scan_itvl = 0x0010;
    params.scan_window = 0x0010;
    params.itvl_min = 0x0018;
    params.itvl_max = 0x0018;
    params.latency = 0;
    params.supervision_timeout = 400;

    if (s_le->scan_running) {
        bt_audio_host_disc_cancel();
        s_le->scan_running = false;
    }
    ret = bt_audio_host_connect(own_addr_type, &peer, &params,
                                timeout_ms ? timeout_ms : BT_AUDIO_LE_SCAN_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t bt_audio_le_disconnect(const uint8_t *bt_dev_addr)
{
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    ESP_RETURN_ON_FALSE(s_le->conn_handle, ESP_ERR_INVALID_STATE, TAG, "No LE ACL connection");
    return bt_audio_host_disconnect(s_le->conn_handle, BT_AUDIO_ERR_REM_USER_CONN_TERM);
}

static esp_err_t bt_audio_le_broadcast_sync(const uint8_t *broadcast_name,
                                            const uint8_t *broadcast_code,
                                            uint32_t bit_field,
                                            uint32_t timeout_ms)
{
#if CONFIG_BT_BAP_BROADCAST_SINK
    ESP_RETURN_ON_ERROR(bt_audio_le_broadcast_sink_sync(broadcast_name, broadcast_code, bit_field, timeout_ms),
                        TAG, "Failed to prepare broadcast sync");
    return bt_audio_le_start_scan(NULL, timeout_ms);
#else
    ESP_LOGE(TAG, "Broadcast sync failed: broadcast sink is not supported");
    return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_BROADCAST_SINK */
}

static esp_err_t bt_audio_le_pa_sync_terminate(void)
{
#if CONFIG_BT_BAP_BROADCAST_SINK
    return bt_audio_le_broadcast_sink_pa_sync_terminate();
#else
    ESP_LOGE(TAG, "PA sync terminate failed: broadcast sink is not supported");
    return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_BROADCAST_SINK */
}

static esp_err_t bt_audio_le_broadcast_source_start_audio(void)
{
#if CONFIG_BT_BAP_BROADCAST_SOURCE
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    ESP_RETURN_ON_FALSE(s_le->inited_broadcast_source, ESP_ERR_INVALID_STATE, TAG, "Broadcast source not initialized");
    ESP_RETURN_ON_FALSE(s_le->started, ESP_ERR_INVALID_STATE, TAG, "LE Audio not started");
    if (s_le->broadcast_source_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(bt_audio_le_broadcast_source_start(BT_AUDIO_LE_ADV_HANDLE),
                        TAG, "Failed to start broadcast source");
    s_le->broadcast_source_started = true;
    return ESP_OK;
#else
    ESP_LOGE(TAG, "Broadcast source start failed: broadcast source is not supported");
    return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_BROADCAST_SOURCE */
}

static esp_err_t bt_audio_le_broadcast_source_stop_audio(void)
{
#if CONFIG_BT_BAP_BROADCAST_SOURCE
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    ESP_RETURN_ON_FALSE(s_le->inited_broadcast_source, ESP_ERR_INVALID_STATE, TAG, "Broadcast source not initialized");
    if (!s_le->broadcast_source_started) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(bt_audio_le_broadcast_source_stop(),
                        TAG, "Failed to stop broadcast source");
    s_le->broadcast_source_started = false;
    return ESP_OK;
#else
    ESP_LOGE(TAG, "Broadcast source stop failed: broadcast source is not supported");
    return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_BROADCAST_SOURCE */
}

static inline esp_err_t bt_audio_le_register_ops(void)
{
    esp_bt_audio_le_ops_t le_ops = {
        .start_scan = bt_audio_le_start_scan,
        .stop_scan = bt_audio_le_stop_scan,
        .connect = bt_audio_le_connect,
        .disconnect = bt_audio_le_disconnect,
        .broadcast_source_start = bt_audio_le_broadcast_source_start_audio,
        .broadcast_source_stop = bt_audio_le_broadcast_source_stop_audio,
        .broadcast_sync = bt_audio_le_broadcast_sync,
        .pa_sync_terminate = bt_audio_le_pa_sync_terminate,
    };
    return bt_audio_ops_set_le(&le_ops);
}

static esp_err_t bt_audio_le_prepare_adv_builder(const esp_bt_audio_le_cfg_t *cfg)
{
    ESP_RETURN_ON_ERROR(bt_audio_le_adv_builder_init(BT_AUDIO_LE_ADV_BUFFER_SIZE, &s_le->adv_builder),
                        TAG, "Failed to create advertising builder");

    bt_audio_le_adv_builder_add_flags(s_le->adv_builder, 0x06);
    bt_audio_le_adv_builder_add_appearance(s_le->adv_builder, ESP_BLE_AUDIO_APPEARANCE_WEARABLE_AUDIO_DEVICE_EARBUD);
    bt_audio_le_adv_builder_add_service_uuid16(s_le->adv_builder, ESP_BLE_AUDIO_UUID_PACS_VAL);

    if (cfg->user_case == ESP_BT_AUDIO_LE_USER_CASE_TMAP) {
#if CONFIG_BT_TMAP
        bt_audio_le_adv_builder_add_service_uuid16(s_le->adv_builder, ESP_BLE_AUDIO_UUID_CAS_VAL);
        bt_audio_le_adv_builder_add_service_uuid16(s_le->adv_builder, ESP_BLE_AUDIO_UUID_TMAS_VAL);
        uint8_t cap_data[] = {
            (uint8_t)(ESP_BLE_AUDIO_UUID_CAS_VAL & 0xFF),
            (uint8_t)((ESP_BLE_AUDIO_UUID_CAS_VAL >> 8) & 0xFF),
            ESP_BLE_AUDIO_UNICAST_ANNOUNCEMENT_TARGETED,
        };
        uint8_t tmap_data[] = {
            (uint8_t)(ESP_BLE_AUDIO_UUID_TMAS_VAL & 0xFF),
            (uint8_t)((ESP_BLE_AUDIO_UUID_TMAS_VAL >> 8) & 0xFF),
            (uint8_t)(cfg->roles & 0xFF),
            (uint8_t)((cfg->roles >> 8) & 0xFF),
        };
        bt_audio_le_adv_builder_add_service_data(s_le->adv_builder, cap_data, sizeof(cap_data));
        bt_audio_le_adv_builder_add_service_data(s_le->adv_builder, tmap_data, sizeof(tmap_data));
#else
        ESP_LOGE(TAG, "Prepare advertising failed: TMAP is not supported");
        return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_TMAP */
    }

    const char *name = bt_audio_host_svc_gap_device_name();
    if (name) {
        bt_audio_le_adv_builder_add_name(s_le->adv_builder, name);
    }
    return ESP_OK;
}

static esp_err_t bt_audio_le_load_user_case_tmap(const esp_bt_audio_le_cfg_t *cfg)
{
#if CONFIG_BT_TMAP
    uint32_t roles = cfg->roles;

    ESP_RETURN_ON_FALSE(roles, ESP_ERR_INVALID_ARG, TAG, "TMAP roles are not configured");
    ESP_LOGI(TAG, "TMAP roles: 0x%08lx, PACS sink(loc 0x%08lx, ctx 0x%08lx), source(loc 0x%08lx, ctx 0x%08lx)",
             roles,
             cfg->pacs.sink_locations, cfg->pacs.sink_context_mask,
             cfg->pacs.source_locations, cfg->pacs.source_context_mask);

    if (bt_audio_le_pacs_enabled(&cfg->pacs)) {
        ESP_RETURN_ON_ERROR(bt_audio_le_pacs_register(&cfg->pacs), TAG, "Failed to register PACS");
        s_le->inited_pacs = true;
    }

    if (roles & (ESP_BLE_AUDIO_TMAP_ROLE_CT | ESP_BLE_AUDIO_TMAP_ROLE_UMR)) {
#if CONFIG_BT_BAP_UNICAST_SERVER
        ESP_RETURN_ON_ERROR(bt_audio_le_unicast_server_init(cfg, s_le->adv_builder),
                            TAG, "Failed to init unicast server");
        s_le->inited_unicast_server = true;
#else
        ESP_LOGE(TAG, "TMAP unicast roles require CONFIG_BT_BAP_UNICAST_SERVER");
        return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_UNICAST_SERVER */
    }

    if (roles & ESP_BLE_AUDIO_TMAP_ROLE_BMR) {
#if CONFIG_BT_BAP_SCAN_DELEGATOR && CONFIG_BT_BAP_BROADCAST_SINK
        ESP_RETURN_ON_ERROR(bt_audio_le_scan_delegator_init(s_le->adv_builder),
                            TAG, "Failed to init scan delegator");
        s_le->inited_scan_delegator = true;

        ESP_RETURN_ON_ERROR(bt_audio_le_broadcast_sink_init(cfg->pacs.sink_locations),
                            TAG, "Failed to init broadcast sink");
        s_le->inited_broadcast_sink = true;
#else
        ESP_LOGE(TAG, "Load TMAP failed: broadcast media receiver role requires scan delegator and broadcast sink");
        return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_SCAN_DELEGATOR && CONFIG_BT_BAP_BROADCAST_SINK */
    }

#if CONFIG_BT_VCP_VOL_REND
    if (roles & (ESP_BLE_AUDIO_TMAP_ROLE_UMR | ESP_BLE_AUDIO_TMAP_ROLE_CT)) {
        ESP_RETURN_ON_ERROR(bt_audio_le_vcp_rend_init(&cfg->vcp_rend, s_le->adv_builder),
                            TAG, "Failed to init VCP renderer");
        s_le->inited_vcp_rend = true;
    }
#endif  /* CONFIG_BT_VCP_VOL_REND */

#if CONFIG_BT_MCC
    if (roles & ESP_BLE_AUDIO_TMAP_ROLE_UMR) {
        ESP_RETURN_ON_ERROR(bt_audio_le_mcc_init(), TAG, "Failed to init MCC media control client");
        s_le->inited_mcc = true;
    }
#endif  /* CONFIG_BT_MCC */

    if (roles & ESP_BLE_AUDIO_TMAP_ROLE_CT) {
#if CONFIG_BT_MICP_MIC_DEV
        ESP_RETURN_ON_ERROR(bt_audio_le_micp_init(s_le->adv_builder), TAG, "Failed to init MICP microphone device");
        s_le->inited_micp = true;
#endif  /* CONFIG_BT_MICP_MIC_DEV */
#if CONFIG_BT_TBS_CLIENT
#if CONFIG_BT_MCC
        ESP_RETURN_ON_ERROR(bt_audio_le_ccp_init(bt_audio_le_mcc_discover_after_ccp, NULL),
                            TAG, "Failed to init CCP call control client");
#else
        ESP_RETURN_ON_ERROR(bt_audio_le_ccp_init(NULL, NULL), TAG, "Failed to init CCP call control client");
#endif  /* CONFIG_BT_MCC */
        s_le->inited_ccp = true;
#endif  /* CONFIG_BT_TBS_CLIENT */
    }

    if (roles & ESP_BLE_AUDIO_TMAP_ROLE_BMS) {
#if CONFIG_BT_BAP_BROADCAST_SOURCE
        ESP_RETURN_ON_ERROR(bt_audio_le_broadcast_source_init(cfg, s_le->adv_builder),
                            TAG, "Failed to init broadcast source");
        s_le->inited_broadcast_source = true;
#else
        ESP_LOGE(TAG, "TMAP broadcast media sender role requires CONFIG_BT_BAP_BROADCAST_SOURCE");
        return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_BROADCAST_SOURCE */
    }

    esp_err_t ret = esp_ble_audio_tmap_register(roles);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Load TMAP failed: register roles 0x%08lx failed: %s", roles, esp_err_to_name(ret));
    }
    return ret;
#else
    ESP_LOGE(TAG, "Load TMAP failed: TMAP is not supported");
    return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_TMAP */
}

static esp_err_t bt_audio_le_load_user_case(const esp_bt_audio_le_cfg_t *cfg)
{
    switch (cfg->user_case) {
        case ESP_BT_AUDIO_LE_USER_CASE_TMAP:
            return bt_audio_le_load_user_case_tmap(cfg);
        case ESP_BT_AUDIO_LE_USER_CASE_UNKNOWN:
            ESP_LOGW(TAG, "LE user case is unknown, falling back to explicit LE role flags");
            break;
        default:
            ESP_LOGW(TAG, "LE user case %u is not implemented, falling back to explicit LE role flags", cfg->user_case);
            break;
    }

    if (bt_audio_le_pacs_enabled(&cfg->pacs)) {
        ESP_RETURN_ON_ERROR(bt_audio_le_pacs_register(&cfg->pacs), TAG, "Failed to register PACS");
        s_le->inited_pacs = true;
    }

    if (cfg->roles & ESP_BT_AUDIO_LE_ROLE_UNICAST_SERVER) {
#if CONFIG_BT_BAP_UNICAST_SERVER
        ESP_RETURN_ON_ERROR(bt_audio_le_unicast_server_init(cfg, s_le->adv_builder),
                            TAG, "Failed to init unicast server");
        s_le->inited_unicast_server = true;
#else
        ESP_LOGE(TAG, "Load LE role failed: unicast server is not supported");
        return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_UNICAST_SERVER */
    }
    if (cfg->roles & ESP_BT_AUDIO_LE_ROLE_BROADCAST_SINK) {
#if CONFIG_BT_BAP_BROADCAST_SINK
        ESP_RETURN_ON_ERROR(bt_audio_le_broadcast_sink_init(cfg->pacs.sink_locations),
                            TAG, "Failed to init broadcast sink");
        s_le->inited_broadcast_sink = true;
#else
        ESP_LOGE(TAG, "Load LE role failed: broadcast sink is not supported");
        return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_BROADCAST_SINK */
    }
    if (cfg->roles & ESP_BT_AUDIO_LE_ROLE_BROADCAST_SOURCE) {
#if CONFIG_BT_BAP_BROADCAST_SOURCE
        ESP_RETURN_ON_ERROR(bt_audio_le_broadcast_source_init(cfg, s_le->adv_builder),
                            TAG, "Failed to init broadcast source");
        s_le->inited_broadcast_source = true;
#else
        ESP_LOGE(TAG, "Load LE role failed: broadcast source is not supported");
        return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_BROADCAST_SOURCE */
    }
    if (cfg->roles & ESP_BT_AUDIO_LE_ROLE_SCAN_DELEGATOR) {
#if CONFIG_BT_BAP_SCAN_DELEGATOR
        ESP_RETURN_ON_ERROR(bt_audio_le_scan_delegator_init(s_le->adv_builder),
                            TAG, "Failed to init scan delegator");
        s_le->inited_scan_delegator = true;
#else
        ESP_LOGE(TAG, "Load LE role failed: scan delegator is not supported");
        return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_SCAN_DELEGATOR */
    }

    return ESP_OK;
}

esp_err_t bt_audio_le_init(const esp_bt_audio_le_cfg_t *cfg)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "LE config is NULL");
    ESP_RETURN_ON_FALSE(!s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio already initialized");

    s_le = heap_caps_calloc_prefer(1, sizeof(*s_le), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_NO_MEM, TAG, "No memory for LE context");
    memcpy(&s_le->cfg, cfg, sizeof(s_le->cfg));

    esp_ble_audio_init_info_t init_info = {
        .gap_cb = bt_audio_le_iso_gap_cb,
        .gatt_cb = bt_audio_le_iso_gatt_cb,
    };
    ESP_GOTO_ON_ERROR(bt_audio_host_register_event_cb(), fail, TAG, "Failed to register LE host event callback");
    ESP_GOTO_ON_ERROR(esp_ble_audio_common_init(&init_info), fail, TAG, "Failed to init BLE Audio common");
    s_le->inited_common = true;

    ESP_GOTO_ON_ERROR(bt_audio_le_prepare_adv_builder(cfg), fail, TAG, "Failed to prepare advertising data");
    ESP_GOTO_ON_ERROR(bt_audio_le_load_user_case(cfg), fail, TAG, "Failed to load LE user case");

#if CONFIG_BT_CSIP_SET_MEMBER
    if (cfg->csip.coordinate_set_size > 1) {
        uint8_t csis_rsi[ESP_BLE_AUDIO_CSIP_RSI_SIZE] = {0};

        s_le->start_info.csis_insts[0].svc_inst = NULL;
        s_le->start_info.csis_insts[0].included_by_cas = true;
        ESP_GOTO_ON_ERROR(bt_audio_le_csip_init(&cfg->csip,
                                                &s_le->start_info.csis_insts[0].svc_inst,
                                                csis_rsi,
                                                s_le->start_info.csis_insts[0].included_by_cas,
                                                s_le->adv_builder),
                          fail, TAG, "Failed to init CSIP set member");
        s_le->inited_csip = true;
    }
#endif  /* CONFIG_BT_CSIP_SET_MEMBER */

    esp_ble_audio_start_info_t *start_info = s_le->inited_csip ? &s_le->start_info : NULL;
    ESP_GOTO_ON_ERROR(esp_ble_audio_common_start(start_info), fail, TAG, "Failed to start BLE Audio common");
    s_le->started = true;
    s_le->adv_enabled = true;
    ESP_GOTO_ON_ERROR(bt_audio_le_start_adv(), fail, TAG, "Failed to start LE advertising");
    ESP_GOTO_ON_ERROR(bt_audio_le_register_ops(), fail, TAG, "Failed to register LE ops");

    return ESP_OK;

fail:
    bt_audio_le_deinit();
    ESP_LOGE(TAG, "Init LE failed: %s", esp_err_to_name(ret));
    return ret;
}

void bt_audio_le_deinit(void)
{
    if (!s_le) {
        return;
    }

    bt_audio_ops_set_le(NULL);
    bt_audio_le_stop_periodic_adv();
    bt_audio_le_stop_ext_adv();

#if CONFIG_BT_VCP_VOL_REND
    if (s_le->inited_vcp_rend) {
        bt_audio_le_vcp_rend_deinit();
    }
#endif  /* CONFIG_BT_VCP_VOL_REND */

#if CONFIG_BT_BAP_UNICAST_SERVER
    if (s_le->inited_unicast_server) {
        bt_audio_le_unicast_server_deinit();
    }
#endif  /* CONFIG_BT_BAP_UNICAST_SERVER */
#if CONFIG_BT_BAP_BROADCAST_SINK
    if (s_le->inited_broadcast_sink) {
        bt_audio_le_broadcast_sink_deinit();
    }
#endif  /* CONFIG_BT_BAP_BROADCAST_SINK */
#if CONFIG_BT_BAP_BROADCAST_SOURCE
    if (s_le->inited_broadcast_source) {
        bt_audio_le_broadcast_source_deinit();
    }
#endif  /* CONFIG_BT_BAP_BROADCAST_SOURCE */
#if CONFIG_BT_BAP_SCAN_DELEGATOR
    if (s_le->inited_scan_delegator) {
        bt_audio_le_scan_delegator_deinit();
    }
#endif  /* CONFIG_BT_BAP_SCAN_DELEGATOR */
#if CONFIG_BT_CSIP_SET_MEMBER
    if (s_le->inited_csip) {
        bt_audio_le_csip_deinit();
    }
#endif  /* CONFIG_BT_CSIP_SET_MEMBER */
#if CONFIG_BT_MICP_MIC_DEV
    if (s_le->inited_micp) {
        bt_audio_le_micp_deinit();
    }
#endif  /* CONFIG_BT_MICP_MIC_DEV */
#if CONFIG_BT_TBS_CLIENT
    if (s_le->inited_ccp) {
        bt_audio_le_ccp_deinit();
    }
#endif  /* CONFIG_BT_TBS_CLIENT */
#if CONFIG_BT_MCC
    if (s_le->inited_mcc) {
        bt_audio_le_mcc_deinit();
    }
#endif  /* CONFIG_BT_MCC */
    if (s_le->inited_pacs) {
        bt_audio_le_pacs_unregister();
    }
    if (s_le->adv_builder) {
        bt_audio_le_adv_builder_deinit(s_le->adv_builder);
    }
    heap_caps_free(s_le);
    s_le = NULL;
}
