/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_defs.h"
#if CONFIG_BT_TMAP
#include "esp_ble_audio_tmap_api.h"
#endif  /* CONFIG_BT_TMAP */
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR && CONFIG_BT_NIMBLE_ENABLED
#include "host/ble_gatt.h"
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR && CONFIG_BT_NIMBLE_ENABLED */
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR && CONFIG_BT_MCS
#include "esp_ble_audio_media_proxy_api.h"
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR && CONFIG_BT_MCS */

#include "bt_audio_host_ops.h"
#include "bt_audio_ops.h"
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
#include "bt_audio_le_csip_set_member.h"
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
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
#include "bt_audio_le_unicast_client.h"
#include "bt_audio_le_csip_coordinator.h"
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
#if CONFIG_BT_VCP_VOL_REND
#include "bt_audio_le_vcp_rend.h"
#endif  /* CONFIG_BT_VCP_VOL_REND */
#if CONFIG_BT_VCP_VOL_CTLR && CONFIG_BT_CAP_COMMANDER
#include "bt_audio_le_vcp_ctlr.h"
#endif  /* CONFIG_BT_VCP_VOL_CTLR && CONFIG_BT_CAP_COMMANDER */

#define BT_AUDIO_LE_ADV_HANDLE               0x00
#define BT_AUDIO_LE_ADV_BUFFER_SIZE          128
#define BT_AUDIO_LE_SCAN_TIMEOUT_MS          10000
#define BT_AUDIO_LE_SCAN_TIMEOUT_GUARD_MS    200
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
#define BT_AUDIO_LE_CSIP_SCAN_ON_MS          2500
#define BT_AUDIO_LE_CSIP_SCAN_OFF_MS         2500
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
#define BT_AUDIO_LE_MAX_CONNECTIONS          2
#define BT_AUDIO_LE_ADV_TYPE_CSIS_RSI        0x2e
#define BT_AUDIO_LE_ADV_PROP_CONNECTABLE     0x0001U

/**
 * @brief  State of one connected LE Audio peer.
 */
typedef struct {
    uint8_t   addr[6];                 /*!< Peer identity address */
    uint16_t  conn_handle;             /*!< LE ACL connection handle */
    bool      used                    : 1;  /*!< True while the slot is in use */
    bool      security_established    : 1;  /*!< Security has been established */
    bool      mtu_exchanged           : 1;  /*!< ATT MTU exchange has completed */
    bool      gatt_disc_started       : 1;  /*!< GATT discovery has been started */
    bool      gatt_ready              : 1;  /*!< GATT discovery has completed */
    bool      user_connected_notified : 1;  /*!< CONNECTED event went to the user */
} bt_audio_le_peer_t;

/**
 * @brief  Runtime context for the LE Audio coordinator.
 */
typedef struct {
    esp_ble_audio_start_info_t  start_info;                             /*!< Common BLE Audio start parameters */
    bt_audio_le_adv_builder_t   adv_builder;                            /*!< Extended advertising data builder */
    uint8_t                     adv_data[BT_AUDIO_LE_ADV_BUFFER_SIZE];  /*!< Extended advertising data buffer */
    uint8_t                     connect_target[6];                      /*!< Pending scan target address */
    esp_timer_handle_t          scan_timer;                             /*!< Tracks the end of a time-limited scan */
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
    esp_timer_handle_t          csip_search_timer;                      /*!< Delay between CSIP RSI scan bursts */
    TaskHandle_t                csip_connect_task;                      /*!< Coordinated-set member connect task */
    uint32_t                    csip_connect_generation;                /*!< SIRK generation of the queued RSI connect */
    bool                        csip_scan_connect_pending;              /*!< Next ACL connect came from an RSI match */
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
    bt_audio_le_peer_t          peers[BT_AUDIO_LE_MAX_CONNECTIONS];     /*!< Connected LE peers */
    uint16_t                    primary_conn_handle;                    /*!< First/current LE ACL connection handle */
    bool                        scan_running            : 1;            /*!< True when GAP discovery is active */
    bool                        scan_user_visible       : 1;            /*!< Scan state was announced to the user */
    bool                        inited_pacs             : 1;            /*!< PACS module has been initialized */
    bool                        inited_unicast_server   : 1;            /*!< Unicast server initialized */
    bool                        inited_unicast_client   : 1;            /*!< Unicast client initialized */
    bool                        inited_broadcast_sink   : 1;            /*!< Broadcast sink initialized */
    bool                        inited_broadcast_source : 1;            /*!< Broadcast source initialized */
    bool                        inited_scan_delegator   : 1;            /*!< Scan delegator initialized */
    bool                        inited_csip_set_member  : 1;            /*!< CSIP set member initialized */
    bool                        inited_mcc              : 1;            /*!< MCC module has been initialized */
    bool                        inited_micp             : 1;            /*!< MICP module has been initialized */
    bool                        inited_ccp              : 1;            /*!< CCP module has been initialized */
    bool                        inited_vcp_rend         : 1;            /*!< VCP renderer initialized */
    bool                        inited_vcp_ctlr         : 1;            /*!< VCP volume controller initialized */
    bool                        started                 : 1;            /*!< Common BLE Audio layer has been started */
    bool                        broadcast_source_started : 1;            /*!< Broadcast source audio path started */
    bool                        adv_configured          : 1;            /*!< Extended advertising set configured */
    bool                        adv_enabled             : 1;            /*!< Extended advertising is allowed to run */
    bool                        adv_running             : 1;            /*!< Extended advertising is running */
    bool                        periodic_adv_running    : 1;            /*!< Periodic advertising is running */
    bool                        csip_coordinator_search : 1;            /*!< Scanning for remaining CSIP members */
    bool                        csip_set_connecting     : 1;            /*!< A matched member connect is queued */
    bool                        csip_connect_abort      : 1;            /*!< Coordinated-set connect task should exit */
    esp_bt_audio_le_cfg_t       cfg;                                    /*!< Cached user configuration */
} bt_audio_le_ctx_t;

static const char *TAG = "BT_AUD_LE";
static bt_audio_le_ctx_t *s_le;
static esp_err_t bt_audio_le_stop_scan_ex(bool resume_csip);

static bt_audio_le_peer_t *bt_audio_le_peer_find(uint16_t conn_handle)
{
    if (!s_le) {
        return NULL;
    }
    for (size_t i = 0; i < BT_AUDIO_LE_MAX_CONNECTIONS; i++) {
        if (s_le->peers[i].used && s_le->peers[i].conn_handle == conn_handle) {
            return &s_le->peers[i];
        }
    }
    return NULL;
}

static bt_audio_le_peer_t *bt_audio_le_peer_find_addr(const uint8_t addr[6])
{
    if (!s_le || !addr) {
        return NULL;
    }
    for (size_t i = 0; i < BT_AUDIO_LE_MAX_CONNECTIONS; i++) {
        if (s_le->peers[i].used && memcmp(s_le->peers[i].addr, addr, sizeof(s_le->peers[i].addr)) == 0) {
            return &s_le->peers[i];
        }
    }
    return NULL;
}

static bt_audio_le_peer_t *bt_audio_le_peer_alloc(uint16_t conn_handle, const bt_audio_addr_t *addr)
{
    bt_audio_le_peer_t *peer = bt_audio_le_peer_find(conn_handle);
    if (peer) {
        return peer;
    }
    for (size_t i = 0; s_le && i < BT_AUDIO_LE_MAX_CONNECTIONS; i++) {
        if (!s_le->peers[i].used) {
            peer = &s_le->peers[i];
            memset(peer, 0, sizeof(*peer));
            peer->used = true;
            peer->conn_handle = conn_handle;
            if (addr) {
                memcpy(peer->addr, addr->val, sizeof(peer->addr));
            }
            return peer;
        }
    }
    return NULL;
}

static size_t bt_audio_le_peer_count(void)
{
    size_t count = 0;
    for (size_t i = 0; s_le && i < BT_AUDIO_LE_MAX_CONNECTIONS; i++) {
        count += s_le->peers[i].used;
    }
    return count;
}

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

static esp_err_t bt_audio_le_stop_ext_adv(void)
{
    if (!s_le->adv_running) {
        return ESP_OK;
    }

    esp_err_t err = bt_audio_host_ext_adv_stop(BT_AUDIO_LE_ADV_HANDLE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to stop extended advertising: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }
    s_le->adv_running = false;
    return ESP_OK;
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

#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
static bool bt_audio_le_csip_member_is_current(uint16_t conn_handle);
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */

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

#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
    if (s_le->inited_unicast_client) {
        bool local_ums = s_le->cfg.user_case == ESP_BT_AUDIO_LE_USER_CASE_TMAP &&
                         (s_le->cfg.roles & ESP_BLE_AUDIO_TMAP_ROLE_UMS) != 0;
        if (local_ums && (peer_role & ESP_BLE_AUDIO_TMAP_ROLE_UMR) == 0) {
            ESP_LOGW(TAG, "Reject unicast member conn_handle %u: peer_role 0x%X does not include UMR",
                     conn->handle, peer_role);
        } else {
            esp_err_t add_err = ESP_OK;
            if (bt_audio_le_csip_member_is_current(conn->handle)) {
                add_err = bt_audio_le_unicast_client_add_member(conn->handle);
            }
            if (add_err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start CAP discovery for member: %s",
                         esp_err_to_name(add_err));
            }
        }
    }
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */

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
        case BT_AUDIO_AD_TYPE_BROADCAST_NAME: {
            size_t bn_cap = sizeof(disc->disc_data.le.broadcast_name) - 1;
            memcpy(disc->disc_data.le.broadcast_name, data, MIN((size_t)data_len, bn_cap));
            break;
        }
        case BT_AUDIO_AD_TYPE_SVC_DATA_UUID16:
            if (data_len >= ESP_BLE_AUDIO_UUID_SIZE_16 + ESP_BLE_AUDIO_BROADCAST_ID_SIZE &&
                bt_audio_le_get_u16(data) == ESP_BLE_AUDIO_UUID_BROADCAST_AUDIO_VAL) {
                disc->disc_data.le.broadcast_id = bt_audio_le_get_u24(data + ESP_BLE_AUDIO_UUID_SIZE_16);
            } else if (data_len >= ESP_BLE_AUDIO_UUID_SIZE_16 + sizeof(uint16_t) &&
                       bt_audio_le_get_u16(data) == ESP_BLE_AUDIO_UUID_TMAS_VAL) {
                disc->disc_data.le.tmap_role = bt_audio_le_get_u16(data + ESP_BLE_AUDIO_UUID_SIZE_16);
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

static void bt_audio_le_start_adv_if_enabled(void)
{
    if (!s_le || !s_le->started || !s_le->adv_enabled) {
        return;
    }

    esp_err_t ret = bt_audio_le_start_adv();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to restart LE advertising: %s", esp_err_to_name(ret));
    }
}

esp_err_t esp_bt_audio_le_set_advertising(bool enable)
{
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    ESP_RETURN_ON_FALSE(s_le->started, ESP_ERR_INVALID_STATE, TAG, "LE Audio not started");

    s_le->adv_enabled = enable;
    if (enable) {
        return bt_audio_le_start_adv();
    }

    ESP_RETURN_ON_ERROR(bt_audio_le_stop_periodic_adv(), TAG, "Failed to stop LE periodic advertising");
    return bt_audio_le_stop_ext_adv();
}

bool esp_bt_audio_le_is_advertising(void)
{
    return s_le && s_le->started && s_le->adv_running;
}

size_t esp_bt_audio_le_get_bond_count(void)
{
    return bt_audio_host_bond_count();
}

static void bt_audio_le_scan_state_clear(void)
{
    s_le->scan_running = false;
    memset(s_le->connect_target, 0, sizeof(s_le->connect_target));
    if (s_le->scan_user_visible) {
        s_le->scan_user_visible = false;
        esp_bt_audio_event_discovery_st_t event = {
            .tech = ESP_BT_AUDIO_TECH_LE,
            .discovering = false,
        };
        bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_DISCOVERY_STATE_CHG, &event);
    }
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

    esp_err_t ret = bt_audio_host_id_infer_auto(1, &own_addr_type);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "Failed to infer own addr type");

    params.passive = true;
    params.itvl = 160;
    params.window = 160;
    params.filter_duplicates = true;
    ret = bt_audio_host_disc(own_addr_type, &params, timeout_ms);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "Failed to start scan");

    s_le->scan_running = true;
    s_le->scan_user_visible = true;
    if (s_le->scan_timer) {
        esp_timer_stop(s_le->scan_timer);
        esp_timer_start_once(s_le->scan_timer,
                             ((uint64_t)timeout_ms + BT_AUDIO_LE_SCAN_TIMEOUT_GUARD_MS) * 1000ULL);
    }
    esp_bt_audio_event_discovery_st_t event = {
        .tech = ESP_BT_AUDIO_TECH_LE,
        .discovering = true,
    };
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_DISCOVERY_STATE_CHG, &event);
    return ESP_OK;
}

static esp_err_t bt_audio_le_connect(uint8_t addr_type, const uint8_t *bt_dev_addr, uint32_t timeout_ms)
{
    bt_audio_conn_params_t params = {0};
    bt_audio_addr_t peer = {0};
    uint8_t own_addr_type = 0;

    ESP_RETURN_ON_FALSE(s_le && bt_dev_addr, ESP_ERR_INVALID_ARG, TAG, "Invalid connect args");

    int local_privacy = (addr_type == 1);
    esp_err_t ret = bt_audio_host_id_infer_auto(local_privacy, &own_addr_type);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ESP_FAIL, TAG, "Failed to infer own addr type");

    peer.type = addr_type;
    memcpy(peer.val, bt_dev_addr, sizeof(peer.val));
    params.scan_itvl = 0x0010;
    params.scan_window = 0x0010;
    params.itvl_min = 0x0018;
    params.itvl_max = 0x0018;
    params.latency = 0;
    params.supervision_timeout = 400;
    params.min_ce_len = 0;
    params.max_ce_len = 0;

    if (s_le->scan_running) {
        bt_audio_le_stop_scan_ex(false);
    }
    bt_audio_le_stop_ext_adv();
    memcpy(s_le->connect_target, bt_dev_addr, sizeof(s_le->connect_target));
    ret = bt_audio_host_connect(own_addr_type, &peer, &params,
                                timeout_ms ? timeout_ms : BT_AUDIO_LE_SCAN_TIMEOUT_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Connect failed: %s", esp_err_to_name(ret));
        bt_audio_le_start_adv_if_enabled();
        return ESP_FAIL;
    }
    return ESP_OK;
}

#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
static void bt_audio_le_csip_search_timer_stop(void)
{
    if (s_le && s_le->csip_search_timer) {
        esp_timer_stop(s_le->csip_search_timer);
    }
}

static esp_err_t bt_audio_le_start_csip_coordinator_scan(void)
{
    bt_audio_scan_params_t params = {
        .passive = true,
        .itvl = 160,
        .window = 160,
        .filter_duplicates = false,
    };
    uint8_t own_addr_type = 0;

    if (!s_le || bt_audio_le_peer_count() >= BT_AUDIO_LE_MAX_CONNECTIONS) {
        return ESP_OK;
    }
    s_le->csip_coordinator_search = true;
    if (s_le->csip_set_connecting || s_le->scan_running) {
        return ESP_OK;
    }
    bt_audio_le_csip_search_timer_stop();
    bt_audio_le_stop_ext_adv();
    ESP_RETURN_ON_ERROR(bt_audio_host_id_infer_auto(1, &own_addr_type), TAG,
                        "Failed to infer own address type");
    ESP_RETURN_ON_ERROR(bt_audio_host_disc(own_addr_type, &params, BT_AUDIO_LE_CSIP_SCAN_ON_MS), TAG,
                        "Failed to start CSIP RSI scan");
    s_le->scan_running = true;
    if (s_le->scan_timer) {
        esp_timer_stop(s_le->scan_timer);
        esp_timer_start_once(s_le->scan_timer,
                             ((uint64_t)BT_AUDIO_LE_CSIP_SCAN_ON_MS +
                              BT_AUDIO_LE_SCAN_TIMEOUT_GUARD_MS) * 1000ULL);
    }
    ESP_LOGI(TAG, "CSIP RSI scan burst %u ms", (unsigned)BT_AUDIO_LE_CSIP_SCAN_ON_MS);
    return ESP_OK;
}

static void bt_audio_le_csip_search_timer_cb(void *arg)
{
    if (!s_le || !s_le->csip_coordinator_search || s_le->csip_set_connecting) {
        return;
    }
    if (!bt_audio_le_unicast_client_needs_member()) {
        return;
    }
    bt_audio_le_start_csip_coordinator_scan();
}

static void bt_audio_le_csip_stop_rsi_scan(void)
{
    if (s_le->scan_running && !s_le->scan_user_visible) {
        ESP_LOGI(TAG, "Stopping coordinated-set RSI scan");
        bt_audio_le_stop_scan_ex(false);
    }
}

static void bt_audio_le_csip_connect_notify(void);
static void bt_audio_le_csip_abort_stale_connect(void);

static bool bt_audio_le_csip_member_is_current(uint16_t conn_handle)
{
    if (bt_audio_le_csip_coordinator_scan_connect_is_current(conn_handle)) {
        return true;
    }
    ESP_LOGW(TAG, "Ignoring stale coordinated-set connect on handle %u", conn_handle);
    bt_audio_le_csip_coordinator_forget_scan_connect(conn_handle);
    bt_audio_host_disconnect(conn_handle, BT_AUDIO_ERR_REM_USER_CONN_TERM);
    return false;
}

void bt_audio_le_csip_coordinator_search_update(void)
{
    if (!s_le || !s_le->inited_unicast_client) {
        return;
    }
    bt_audio_le_csip_abort_stale_connect();
    if (bt_audio_le_unicast_client_needs_member()) {
        bt_audio_le_start_csip_coordinator_scan();
        return;
    }
    bt_audio_le_csip_search_timer_stop();
    if (s_le->csip_coordinator_search) {
        s_le->csip_coordinator_search = false;
        bt_audio_le_csip_stop_rsi_scan();
    }
}

static void bt_audio_le_csip_coordinator_search_stop(void)
{
    if (!s_le) {
        return;
    }
    s_le->csip_coordinator_search = false;
    bt_audio_le_csip_search_timer_stop();
    bt_audio_le_csip_stop_rsi_scan();
}

static void bt_audio_le_csip_coordinator_search_resume(void)
{
    if (!s_le || !s_le->inited_unicast_client) {
        return;
    }
    if (!bt_audio_le_unicast_client_needs_member()) {
        bt_audio_le_csip_coordinator_search_update();
        return;
    }
    s_le->csip_coordinator_search = true;
    if (s_le->csip_set_connecting || s_le->scan_running) {
        return;
    }
    if (s_le->csip_search_timer) {
        esp_timer_stop(s_le->csip_search_timer);
        esp_timer_start_once(s_le->csip_search_timer,
                             (uint64_t)BT_AUDIO_LE_CSIP_SCAN_OFF_MS * 1000ULL);
    }
}

typedef struct {
    uint8_t addr[6];
    uint8_t addr_type;
} bt_audio_le_set_candidate_t;

static bool bt_audio_le_csip_connect_wait(uint32_t timeout_ms)
{
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    if (ticks == 0) {
        ticks = 1;
    }
    ulTaskNotifyTake(pdTRUE, ticks);
    return !s_le || s_le->csip_connect_abort;
}

static void bt_audio_le_csip_connect_notify(void)
{
    if (s_le && s_le->csip_connect_task) {
        xTaskNotifyGive(s_le->csip_connect_task);
    }
}

static void bt_audio_le_csip_abort_stale_connect(void)
{
    if (!s_le || !s_le->csip_set_connecting) {
        return;
    }
    if (s_le->csip_connect_generation == bt_audio_le_csip_coordinator_sirk_generation()) {
        return;
    }
    ESP_LOGW(TAG, "Aborting coordinated-set connect after SIRK change");
    s_le->csip_connect_abort = true;
    s_le->csip_set_connecting = false;
    bt_audio_le_csip_connect_notify();
    bt_audio_host_connect_cancel(s_le->connect_target);
}

static void bt_audio_le_set_connect_task(void *arg)
{
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    bt_audio_le_set_candidate_t *candidate = arg;
    if (s_le && candidate && !s_le->csip_connect_abort) {
        s_le->csip_coordinator_search = true;
        esp_err_t err = bt_audio_le_connect(candidate->addr_type, candidate->addr,
                                            BT_AUDIO_LE_SCAN_TIMEOUT_MS);
        if (err != ESP_OK) {
            s_le->csip_set_connecting = false;
            s_le->csip_scan_connect_pending = false;
            bt_audio_le_csip_coordinator_search_update();
        }
        if (err == ESP_OK &&
            !bt_audio_le_csip_connect_wait(BT_AUDIO_LE_SCAN_TIMEOUT_MS) &&
            s_le && s_le->csip_set_connecting) {
            ESP_LOGW(TAG, "Coordinated-set connect attempt timed out; cancel and resume RSI scan");
            if (!bt_audio_le_peer_find_addr(s_le->connect_target)) {
                bt_audio_host_connect_cancel(s_le->connect_target);
            }
            s_le->csip_set_connecting = false;
            s_le->csip_scan_connect_pending = false;
            bt_audio_le_csip_coordinator_search_update();
        }
    } else if (s_le) {
        s_le->csip_set_connecting = false;
        s_le->csip_scan_connect_pending = false;
    }
    if (s_le && s_le->csip_connect_task == self) {
        s_le->csip_connect_task = NULL;
    }
    heap_caps_free(candidate);
    vTaskDelete(NULL);
}

static bool bt_audio_le_match_rsi(uint8_t type, const uint8_t *data, uint8_t data_len, void *user_data)
{
    bool *matched = user_data;
    if (type == BT_AUDIO_LE_ADV_TYPE_CSIS_RSI) {
        if (bt_audio_le_unicast_client_match_rsi(type, data, data_len)) {
            *matched = true;
            return false;
        }
    }
    return true;
}
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */

static void bt_audio_le_scan_stopped(void)
{
    bt_audio_le_scan_state_clear();
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
    bt_audio_le_csip_coordinator_search_resume();
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
}

static void bt_audio_le_scan_timeout_cb(void *arg)
{
    if (!s_le || !s_le->scan_running) {
        return;
    }
    ESP_LOGI(TAG, "LE scan duration elapsed");
    bt_audio_le_scan_stopped();
}

static esp_err_t bt_audio_le_stop_scan_ex(bool resume_csip)
{
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");

    if (s_le->scan_timer) {
        esp_timer_stop(s_le->scan_timer);
    }

    esp_err_t err = bt_audio_host_disc_cancel();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Stop scan failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    if (resume_csip) {
        bt_audio_le_scan_stopped();
    } else {
        bt_audio_le_scan_state_clear();
    }
    return ESP_OK;
}

static esp_err_t bt_audio_le_stop_scan(void)
{
    return bt_audio_le_stop_scan_ex(true);
}

static void bt_audio_le_start_gatt_disc(bt_audio_le_peer_t *peer, uint16_t conn_handle)
{
    if (peer && (peer->gatt_disc_started || peer->gatt_ready)) {
        return;
    }

    esp_err_t ret = esp_ble_audio_gattc_disc_start(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start GATT discovery: %s", esp_err_to_name(ret));
        return;
    }

    if (peer) {
        peer->gatt_disc_started = true;
    }
    ESP_LOGI(TAG, "Start discovering GATT services on conn_handle %u", conn_handle);
}

#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
static void bt_audio_le_try_start_gatt_disc(uint16_t conn_handle)
{
    bt_audio_le_peer_t *peer = bt_audio_le_peer_find(conn_handle);
    if (!s_le || !s_le->inited_unicast_client || !peer ||
        !peer->security_established || !peer->mtu_exchanged) {
        return;
    }
    bt_audio_le_start_gatt_disc(peer, conn_handle);
}
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */

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
            disc.disc_data.le.connectable =
                (event->ext_scan_recv.event_type & BT_AUDIO_LE_ADV_PROP_CONNECTABLE) != 0;
            bt_audio_addr_t peer_addr = {
                .type = event->ext_scan_recv.addr.type,
            };
            memcpy(peer_addr.val, event->ext_scan_recv.addr.val, sizeof(peer_addr.val));
            bt_audio_le_parse_ad(event->ext_scan_recv.data,
                                 event->ext_scan_recv.data_len,
                                 bt_audio_le_device_found,
                                 &disc);
            if (disc.disc_data.le.connectable) {
                disc.disc_data.le.bonded = bt_audio_host_bond_exists(&peer_addr);
            }
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_DEVICE_DISCOVERED, &disc);
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
            if (s_le->csip_coordinator_search && !s_le->csip_set_connecting &&
                !s_le->csip_connect_task &&
                bt_audio_le_unicast_client_needs_member() &&
                !bt_audio_le_peer_find_addr(event->ext_scan_recv.addr.val)) {
                bool matched = false;
                bt_audio_le_parse_ad(event->ext_scan_recv.data, event->ext_scan_recv.data_len,
                                     bt_audio_le_match_rsi, &matched);
                if (matched) {
                    ESP_LOGI(TAG, "Matched coordinated-set RSI at %02x:%02x:%02x:%02x:%02x:%02x",
                             event->ext_scan_recv.addr.val[5], event->ext_scan_recv.addr.val[4],
                             event->ext_scan_recv.addr.val[3], event->ext_scan_recv.addr.val[2],
                             event->ext_scan_recv.addr.val[1], event->ext_scan_recv.addr.val[0]);
                    bt_audio_le_set_candidate_t *candidate =
                        heap_caps_malloc(sizeof(*candidate), MALLOC_CAP_DEFAULT);
                    if (candidate) {
                        memcpy(candidate->addr, event->ext_scan_recv.addr.val,
                               sizeof(candidate->addr));
                        candidate->addr_type = event->ext_scan_recv.addr.type;
                        s_le->csip_connect_generation = bt_audio_le_csip_coordinator_sirk_generation();
                        s_le->csip_scan_connect_pending = true;
                        s_le->csip_set_connecting = true;
                        s_le->csip_connect_abort = false;
                        if (xTaskCreate(bt_audio_le_set_connect_task, "csip_connect", 3072,
                                        candidate, 8, &s_le->csip_connect_task) != pdPASS) {
                            ESP_LOGE(TAG, "Create coordinated-set connect task failed");
                            s_le->csip_set_connecting = false;
                            s_le->csip_scan_connect_pending = false;
                            heap_caps_free(candidate);
                        }
                    }
                }
            }
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
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
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
            bool from_csip_scan = s_le->csip_scan_connect_pending;
            uint32_t scan_generation = s_le->csip_connect_generation;
            s_le->csip_scan_connect_pending = false;
            s_le->csip_set_connecting = false;
            bt_audio_le_csip_connect_notify();
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
            if (event->acl_connect.status == 0) {
                if (s_le->scan_running) {
                    bt_audio_le_stop_scan();
                }
                bt_audio_addr_t peer = {
                    .type = event->acl_connect.dst.type,
                };
                memcpy(peer.val, event->acl_connect.dst.val, sizeof(peer.val));
                bt_audio_host_acl_connected(event->acl_connect.conn_handle, &peer);
                bt_audio_le_peer_t *slot = bt_audio_le_peer_alloc(event->acl_connect.conn_handle, &peer);
                if (slot) {
                    if (s_le->primary_conn_handle == UINT16_MAX) {
                        s_le->primary_conn_handle = event->acl_connect.conn_handle;
                    }
                    if (event->acl_connect.role == 0x01) {
                        s_le->adv_running = false;
                    }
                    memcpy(s_le->connect_target, event->acl_connect.dst.val, sizeof(s_le->connect_target));
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
                    if (from_csip_scan) {
                        bt_audio_le_csip_coordinator_note_scan_connect(
                            event->acl_connect.conn_handle, scan_generation);
                    }
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
                } else {
                    ESP_LOGE(TAG, "No free LE peer slot");
                    bt_audio_host_disconnect(event->acl_connect.conn_handle,
                                             BT_AUDIO_ERR_REM_USER_CONN_TERM);
                    break;
                }
                ESP_LOGI(TAG, "LE connected, conn_handle %u (%u/%u)",
                         event->acl_connect.conn_handle, (unsigned)bt_audio_le_peer_count(),
                         BT_AUDIO_LE_MAX_CONNECTIONS);
                bt_audio_host_security_initiate(event->acl_connect.conn_handle);
            } else {
                ESP_LOGW(TAG, "LE connect failed, status 0x%02x",
                         (unsigned)(event->acl_connect.status & 0xff));
                esp_bt_audio_event_connection_failed_t failed = {
                    .tech = ESP_BT_AUDIO_TECH_LE,
                    .reason = (uint8_t)event->acl_connect.status,
                };
                memcpy(failed.addr, event->acl_connect.dst.val, sizeof(failed.addr));
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR,
                                      ESP_BT_AUDIO_EVENT_CONNECTION_FAILED, &failed);
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
                if (s_le->inited_unicast_client) {
                    bt_audio_le_csip_coordinator_search_update();
                }
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
                bt_audio_le_start_adv_if_enabled();
            }
            break;
        }
        case ESP_BLE_AUDIO_GAP_EVENT_SECURITY_CHANGE: {
            if (event->security_change.status == 0) {
                bt_audio_le_peer_t *peer = bt_audio_le_peer_find(event->security_change.conn_handle);
                esp_bt_audio_event_connection_st_t conn = {
                    .tech = ESP_BT_AUDIO_TECH_LE,
                    .connected = true,
                    .conn_handle = event->security_change.conn_handle,
                };
                if (peer) {
                    memcpy(conn.addr, peer->addr, sizeof(conn.addr));
                    peer->user_connected_notified = true;
                    peer->security_established = true;
                }
                ESP_LOGI(TAG, "LE security established, conn_handle %u", event->security_change.conn_handle);
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG, &conn);
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
                if (s_le && s_le->inited_unicast_client) {
#if CONFIG_BT_NIMBLE_ENABLED
                    int rc = ble_gattc_exchange_mtu(event->security_change.conn_handle, NULL, NULL);
                    if (rc != 0) {
                        ESP_LOGW(TAG, "MTU exchange failed (%d), start GATT discovery directly", rc);
                        if (peer) {
                            peer->mtu_exchanged = true;
                        }
                        bt_audio_le_try_start_gatt_disc(event->security_change.conn_handle);
                    }
#else
                    bt_audio_le_try_start_gatt_disc(event->security_change.conn_handle);
#endif  /* CONFIG_BT_NIMBLE_ENABLED */
                }
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
            }
            break;
        }
        case ESP_BLE_AUDIO_GAP_EVENT_ACL_DISCONNECT: {
            bt_audio_le_peer_t *peer = bt_audio_le_peer_find(event->acl_disconnect.conn_handle);
            bool notify_user = peer && peer->user_connected_notified;
            esp_bt_audio_event_connection_st_t conn = {
                .tech = ESP_BT_AUDIO_TECH_LE,
                .connected = false,
                .conn_handle = event->acl_disconnect.conn_handle,
            };
            if (s_le) {
                bt_audio_host_acl_disconnected(event->acl_disconnect.conn_handle);
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
                if (peer) {
                    memcpy(conn.addr, peer->addr, sizeof(conn.addr));
                    memset(peer, 0, sizeof(*peer));
                } else {
                    memcpy(conn.addr, s_le->connect_target, sizeof(conn.addr));
                }
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
                if (s_le->inited_unicast_client) {
                    bt_audio_le_csip_coordinator_forget_scan_connect(event->acl_disconnect.conn_handle);
                    bt_audio_le_unicast_client_on_disconnect(event->acl_disconnect.conn_handle);
                    bt_audio_le_csip_coordinator_search_update();
                }
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
                if (s_le->primary_conn_handle == event->acl_disconnect.conn_handle) {
                    s_le->primary_conn_handle = UINT16_MAX;
                    for (size_t i = 0; i < BT_AUDIO_LE_MAX_CONNECTIONS; i++) {
                        if (s_le->peers[i].used) {
                            s_le->primary_conn_handle = s_le->peers[i].conn_handle;
                            break;
                        }
                    }
                }
            }
            ESP_LOGI(TAG, "LE disconnected, reason 0x%02x", event->acl_disconnect.reason);
            if (notify_user) {
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG, &conn);
            } else {
                esp_bt_audio_event_connection_failed_t failed = {
                    .tech = ESP_BT_AUDIO_TECH_LE,
                    .reason = event->acl_disconnect.reason,
                };
                memcpy(failed.addr, conn.addr, sizeof(failed.addr));
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR,
                                      ESP_BT_AUDIO_EVENT_CONNECTION_FAILED, &failed);
            }
            bt_audio_le_start_adv_if_enabled();
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
            bt_audio_le_peer_t *peer = bt_audio_le_peer_find(event->gatt_mtu_change.conn_handle);
            ESP_LOGI(TAG, "GATT MTU change, conn_handle %u, mtu %u",
                     event->gatt_mtu_change.conn_handle,
                     event->gatt_mtu_change.mtu);
            if (event->gatt_mtu_change.mtu < ESP_BLE_AUDIO_ATT_MTU_MIN) {
                ESP_LOGW(TAG, "Invalid new MTU %u, shall be at least %u",
                         event->gatt_mtu_change.mtu,
                         ESP_BLE_AUDIO_ATT_MTU_MIN);
                break;
            }
            if (peer) {
                peer->mtu_exchanged = true;
            }
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
            if (s_le && s_le->inited_unicast_client) {
                bt_audio_le_try_start_gatt_disc(event->gatt_mtu_change.conn_handle);
                break;
            }
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
            bt_audio_le_start_gatt_disc(peer, event->gatt_mtu_change.conn_handle);
            break;
        }
        case ESP_BLE_AUDIO_GATT_EVENT_GATTC_DISC_CMPL:
            {
            bt_audio_le_peer_t *peer = bt_audio_le_peer_find(event->gattc_disc_cmpl.conn_handle);
            ESP_LOGI(TAG, "GATT discovery complete, status %u, conn_handle %u",
                     event->gattc_disc_cmpl.status,
                     event->gattc_disc_cmpl.conn_handle);
            if (event->gattc_disc_cmpl.status) {
                ESP_LOGE(TAG, "GATT discovery failed, status %u", event->gattc_disc_cmpl.status);
                if (peer) {
                    peer->gatt_ready = false;
                    peer->gatt_disc_started = false;
                }
                break;
            }
            if (peer) {
                peer->gatt_ready = true;
            }
            bool cap_discovery_deferred = false;
            if (s_le && s_le->cfg.user_case == ESP_BT_AUDIO_LE_USER_CASE_TMAP) {
#if CONFIG_BT_TMAP
                static const esp_ble_audio_tmap_cb_t tmap_cbs = {
                    .discovery_complete = bt_audio_le_tmap_discovery_complete,
                };
                esp_err_t tmap_err =
                    esp_ble_audio_tmap_discover(event->gattc_disc_cmpl.conn_handle, &tmap_cbs);
                if (tmap_err == ESP_OK) {
                    cap_discovery_deferred = true;
                } else {
                    ESP_LOGE(TAG, "Failed to start TMAP discovery: %s",
                             esp_err_to_name(tmap_err));
                }
#else
                ESP_LOGW(TAG, "TMAP discovery skipped because CONFIG_BT_TMAP is disabled");
#endif  /* CONFIG_BT_TMAP */
            }
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
            if (s_le && s_le->inited_unicast_client && !cap_discovery_deferred) {
                bool local_ums = false;
#if CONFIG_BT_TMAP
                local_ums = s_le->cfg.user_case == ESP_BT_AUDIO_LE_USER_CASE_TMAP &&
                            (s_le->cfg.roles & ESP_BLE_AUDIO_TMAP_ROLE_UMS) != 0;
#else
                local_ums = s_le->cfg.user_case == ESP_BT_AUDIO_LE_USER_CASE_TMAP;
#endif  /* CONFIG_BT_TMAP */
                if (!local_ums &&
                        bt_audio_le_csip_member_is_current(event->gattc_disc_cmpl.conn_handle)) {
                    esp_err_t add_err = bt_audio_le_unicast_client_add_member(
                        event->gattc_disc_cmpl.conn_handle);
                    if (add_err != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to start CAP discovery for member: %s",
                                 esp_err_to_name(add_err));
                    }
                }
            }
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
            break;
            }
        default:
            break;
    }
}

static esp_err_t bt_audio_le_connect_cancel(void)
{
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
    if (s_le->csip_set_connecting) {
        s_le->csip_connect_abort = true;
        s_le->csip_set_connecting = false;
        s_le->csip_scan_connect_pending = false;
        bt_audio_le_csip_connect_notify();
    }
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
    if (bt_audio_le_peer_find_addr(s_le->connect_target)) {
        return ESP_OK;
    }
    esp_err_t ret = bt_audio_host_connect_cancel(s_le->connect_target);
    bt_audio_le_start_adv_if_enabled();
    return ret;
}

static esp_err_t bt_audio_le_disconnect(const uint8_t *bt_dev_addr)
{
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    bt_audio_le_peer_t *peer = bt_dev_addr ? bt_audio_le_peer_find_addr(bt_dev_addr) :
                                 bt_audio_le_peer_find(s_le->primary_conn_handle);
    ESP_RETURN_ON_FALSE(peer, ESP_ERR_INVALID_STATE, TAG, "No matching LE ACL connection");
    return bt_audio_host_disconnect(peer->conn_handle, BT_AUDIO_ERR_REM_USER_CONN_TERM);
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

static esp_err_t bt_audio_le_unicast_start(void)
{
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    ESP_RETURN_ON_FALSE(s_le->inited_unicast_client, ESP_ERR_INVALID_STATE, TAG, "Unicast client not initialized");

    uint16_t conn_handle = s_le->primary_conn_handle;
    uint16_t ready_handle = 0;
    esp_err_t ready_err = bt_audio_le_unicast_client_ready_conn(&ready_handle);
    if (ready_err == ESP_OK) {
        conn_handle = ready_handle;
        ESP_LOGI(TAG, "Unicast start uses ASE-ready handle %u", conn_handle);
    } else if (ready_err != ESP_ERR_NOT_FOUND) {
        return ready_err;
    }

    bt_audio_le_peer_t *peer = bt_audio_le_peer_find(conn_handle);
    ESP_RETURN_ON_FALSE(peer, ESP_ERR_INVALID_STATE, TAG, "No LE ACL connection");
    ESP_RETURN_ON_FALSE(peer->gatt_ready, ESP_ERR_INVALID_STATE, TAG, "GATT discovery is not complete");
    return bt_audio_le_unicast_client_start(peer->conn_handle);
#else
    ESP_LOGE(TAG, "Unicast start failed: unicast client is not supported");
    return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
}

static esp_err_t bt_audio_le_unicast_stop(void)
{
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
    ESP_RETURN_ON_FALSE(s_le, ESP_ERR_INVALID_STATE, TAG, "LE Audio not initialized");
    ESP_RETURN_ON_FALSE(s_le->inited_unicast_client, ESP_ERR_INVALID_STATE, TAG, "Unicast client not initialized");
    esp_err_t err = bt_audio_le_unicast_client_stop();
    bt_audio_le_csip_abort_stale_connect();
    bt_audio_le_csip_coordinator_search_stop();
    return err;
#else
    ESP_LOGE(TAG, "Unicast stop failed: unicast client is not supported");
    return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
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
        .connect_cancel = bt_audio_le_connect_cancel,
        .disconnect = bt_audio_le_disconnect,
        .broadcast_source_start = bt_audio_le_broadcast_source_start_audio,
        .broadcast_source_stop = bt_audio_le_broadcast_source_stop_audio,
        .broadcast_sync = bt_audio_le_broadcast_sync,
        .pa_sync_terminate = bt_audio_le_pa_sync_terminate,
        .unicast_start = bt_audio_le_unicast_start,
        .unicast_stop = bt_audio_le_unicast_stop,
    };
    return bt_audio_ops_set_le(&le_ops);
}

static esp_err_t bt_audio_le_prepare_adv_builder(const esp_bt_audio_le_cfg_t *cfg)
{
    ESP_RETURN_ON_ERROR(bt_audio_le_adv_builder_init(BT_AUDIO_LE_ADV_BUFFER_SIZE, &s_le->adv_builder),
                        TAG, "Failed to create advertising builder");

    bt_audio_le_adv_builder_add_flags(s_le->adv_builder, 0x06);
    bt_audio_le_adv_builder_add_appearance(s_le->adv_builder, ESP_BLE_AUDIO_APPEARANCE_WEARABLE_AUDIO_DEVICE_EARBUD);
    if (bt_audio_le_pacs_enabled(&cfg->pacs)) {
        bt_audio_le_adv_builder_add_service_uuid16(s_le->adv_builder, ESP_BLE_AUDIO_UUID_PACS_VAL);
    }

    if (cfg->user_case == ESP_BT_AUDIO_LE_USER_CASE_TMAP) {
#if CONFIG_BT_TMAP
        bool acceptor_roles = (cfg->roles & (ESP_BLE_AUDIO_TMAP_ROLE_CT |
                                             ESP_BLE_AUDIO_TMAP_ROLE_UMR |
                                             ESP_BLE_AUDIO_TMAP_ROLE_BMR)) != 0;
        if (acceptor_roles) {
            bt_audio_le_adv_builder_add_service_uuid16(s_le->adv_builder, ESP_BLE_AUDIO_UUID_CAS_VAL);
            uint8_t cap_data[] = {
                (uint8_t)(ESP_BLE_AUDIO_UUID_CAS_VAL & 0xFF),
                (uint8_t)((ESP_BLE_AUDIO_UUID_CAS_VAL >> 8) & 0xFF),
                ESP_BLE_AUDIO_UNICAST_ANNOUNCEMENT_TARGETED,
            };
            bt_audio_le_adv_builder_add_service_data(s_le->adv_builder, cap_data, sizeof(cap_data));
        }
        bt_audio_le_adv_builder_add_service_uuid16(s_le->adv_builder, ESP_BLE_AUDIO_UUID_TMAS_VAL);
        uint8_t tmap_data[] = {
            (uint8_t)(ESP_BLE_AUDIO_UUID_TMAS_VAL & 0xFF),
            (uint8_t)((ESP_BLE_AUDIO_UUID_TMAS_VAL >> 8) & 0xFF),
            (uint8_t)(cfg->roles & 0xFF),
            (uint8_t)((cfg->roles >> 8) & 0xFF),
        };
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

#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
static esp_err_t bt_audio_le_init_unicast_client(const esp_bt_audio_le_cfg_t *cfg)
{
#if CONFIG_BT_MCS
    ESP_RETURN_ON_ERROR(esp_ble_audio_media_proxy_pl_init(), TAG, "Failed to init MCS/media proxy");
#endif  /* CONFIG_BT_MCS */
    ESP_RETURN_ON_ERROR(bt_audio_le_unicast_client_init(cfg), TAG, "Failed to init unicast client");
    s_le->inited_unicast_client = true;
#if CONFIG_BT_VCP_VOL_CTLR && CONFIG_BT_CAP_COMMANDER
    ESP_RETURN_ON_ERROR(bt_audio_le_vcp_ctlr_init(cfg->max_unicast_members),
                        TAG, "Failed to init VCP volume controller");
    s_le->inited_vcp_ctlr = true;
#endif  /* CONFIG_BT_VCP_VOL_CTLR && CONFIG_BT_CAP_COMMANDER */
    return ESP_OK;
}
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */

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

    if (roles & ESP_BLE_AUDIO_TMAP_ROLE_UMS) {
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
        ESP_RETURN_ON_ERROR(bt_audio_le_init_unicast_client(cfg), TAG,
                            "Failed to init unicast sender");
#else
        ESP_LOGE(TAG, "TMAP UMS requires CONFIG_BT_BAP_UNICAST_CLIENT and CONFIG_BT_CAP_INITIATOR");
        return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
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
        if ((roles & ESP_BLE_AUDIO_TMAP_ROLE_UMS) &&
#if defined(CONFIG_BT_TMAP_UMS_SUPPORTED)
                !CONFIG_BT_TMAP_UMS_SUPPORTED
#else
                1
#endif  /* defined(CONFIG_BT_TMAP_UMS_SUPPORTED) */
                ) {
            ESP_LOGE(TAG, "TMAP UMS requires CONFIG_BT_CAP_COMMANDER, CONFIG_BT_VCP_VOL_CTLR, "
                     "CONFIG_BT_MCS (and CONFIG_BT_CSIP_SET_COORDINATOR / MPL / MCTL remote control)");
        }
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
    if (cfg->roles & ESP_BT_AUDIO_LE_ROLE_UNICAST_CLIENT) {
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
        ESP_RETURN_ON_ERROR(bt_audio_le_init_unicast_client(cfg), TAG,
                            "Failed to init unicast sender");
#else
        ESP_LOGE(TAG, "Load LE role failed: CAP unicast initiator is not supported");
        return ESP_ERR_NOT_SUPPORTED;
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
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
    s_le->primary_conn_handle = UINT16_MAX;
    memcpy(&s_le->cfg, cfg, sizeof(s_le->cfg));

    esp_timer_create_args_t scan_timer_args = {
        .callback = bt_audio_le_scan_timeout_cb,
        .name = "le_scan_timeout",
    };
    ESP_GOTO_ON_ERROR(esp_timer_create(&scan_timer_args, &s_le->scan_timer), fail, TAG,
                      "Failed to create LE scan timer");
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
    esp_timer_create_args_t csip_search_timer_args = {
        .callback = bt_audio_le_csip_search_timer_cb,
        .name = "csip_search",
    };
    ESP_GOTO_ON_ERROR(esp_timer_create(&csip_search_timer_args, &s_le->csip_search_timer), fail, TAG,
                      "Failed to create CSIP search timer");
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */

    esp_ble_audio_init_info_t init_info = {
        .gap_cb = bt_audio_le_iso_gap_cb,
        .gatt_cb = bt_audio_le_iso_gatt_cb,
    };
    ESP_GOTO_ON_ERROR(bt_audio_host_register_event_cb(), fail, TAG, "Failed to register LE host event callback");
    ESP_GOTO_ON_ERROR(esp_ble_audio_common_init(&init_info), fail, TAG, "Failed to init BLE Audio common");

    ESP_GOTO_ON_ERROR(bt_audio_le_prepare_adv_builder(cfg), fail, TAG, "Failed to prepare advertising data");
    ESP_GOTO_ON_ERROR(bt_audio_le_load_user_case(cfg), fail, TAG, "Failed to load LE user case");

#if CONFIG_BT_CSIP_SET_MEMBER
    if (cfg->csip_set_member.coordinate_set_size > 1) {
        uint8_t csis_rsi[ESP_BLE_AUDIO_CSIP_RSI_SIZE] = {0};

        s_le->start_info.csis_insts[0].svc_inst = NULL;
        s_le->start_info.csis_insts[0].included_by_cas = true;
        ESP_GOTO_ON_ERROR(bt_audio_le_csip_set_member_init(&cfg->csip_set_member,
                                                           &s_le->start_info.csis_insts[0].svc_inst,
                                                           csis_rsi,
                                                           s_le->start_info.csis_insts[0].included_by_cas,
                                                           s_le->adv_builder),
                          fail, TAG, "Failed to init CSIP set member");
        s_le->inited_csip_set_member = true;
    }
#endif  /* CONFIG_BT_CSIP_SET_MEMBER */

    esp_ble_audio_start_info_t *start_info = s_le->inited_csip_set_member ? &s_le->start_info : NULL;
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
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
    if (s_le->csip_connect_task) {
        TaskHandle_t task = s_le->csip_connect_task;
        s_le->csip_connect_abort = true;
        if (task != xTaskGetCurrentTaskHandle()) {
            xTaskNotifyGive(task);
            while (s_le->csip_connect_task) {
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        } else {
            s_le->csip_connect_task = NULL;
        }
    }
    if (s_le->scan_running) {
        bt_audio_le_stop_scan_ex(false);
    }
    bt_audio_host_connect_cancel(s_le->connect_target);
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
    if (s_le->scan_timer) {
        esp_timer_stop(s_le->scan_timer);
        esp_timer_delete(s_le->scan_timer);
        s_le->scan_timer = NULL;
    }
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
    if (s_le->csip_search_timer) {
        esp_timer_stop(s_le->csip_search_timer);
        esp_timer_delete(s_le->csip_search_timer);
        s_le->csip_search_timer = NULL;
    }
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
    bt_audio_le_stop_periodic_adv();
    bt_audio_le_stop_ext_adv();

#if CONFIG_BT_VCP_VOL_REND
    if (s_le->inited_vcp_rend) {
        bt_audio_le_vcp_rend_deinit();
    }
#endif  /* CONFIG_BT_VCP_VOL_REND */
#if CONFIG_BT_VCP_VOL_CTLR && CONFIG_BT_CAP_COMMANDER
    if (s_le->inited_vcp_ctlr) {
        bt_audio_le_vcp_ctlr_deinit();
    }
#endif  /* CONFIG_BT_VCP_VOL_CTLR && CONFIG_BT_CAP_COMMANDER */

#if CONFIG_BT_BAP_UNICAST_SERVER
    if (s_le->inited_unicast_server) {
        bt_audio_le_unicast_server_deinit();
    }
#endif  /* CONFIG_BT_BAP_UNICAST_SERVER */
#if CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR
    if (s_le->inited_unicast_client) {
        bt_audio_le_unicast_client_deinit();
    }
#endif  /* CONFIG_BT_BAP_UNICAST_CLIENT && CONFIG_BT_CAP_INITIATOR */
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
    if (s_le->inited_csip_set_member) {
        bt_audio_le_csip_set_member_deinit();
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
