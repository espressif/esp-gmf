/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_ble_audio_tbs_api.h"
#include "host/conn_internal.h"
#include "bt_audio_ops.h"
#include "bt_audio_evt_dispatcher.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_event.h"
#include "esp_bt_audio_tel.h"

#include "bt_audio_le_ccp.h"

#define BT_AUDIO_LE_CCP_CONN_HANDLE_NONE    UINT16_MAX
#define BT_AUDIO_LE_CCP_DEFAULT_TBS_INST    ESP_BLE_AUDIO_TBS_GTBS_INDEX
#define BT_AUDIO_LE_CCP_URI_SCHEME_MAX_LEN  16

/**
 * @brief  Runtime context for the Call Control Profile client.
 */
typedef struct {
    uint16_t                    conn_handle;     /*!< Discovered TBS connection handle */
    uint8_t                     tbs_count;       /*!< Number of discovered TBS instances */
    uint8_t                     ccid;            /*!< TBS content control ID */
    bool                        gtbs_found;      /*!< True when GTBS was discovered */
    char                        uri_scheme[BT_AUDIO_LE_CCP_URI_SCHEME_MAX_LEN];  /*!< First supported URI scheme */
    uint8_t                     tracked_idx[ESP_BT_AUDIO_CALL_MAX_NUM];          /*!< Calls seen in last notification */
    uint8_t                     tracked_count;   /*!< Number of entries in tracked_idx */
    bt_audio_le_ccp_ready_cb_t  ready_cb;        /*!< Optional discovery-ready callback */
    void                       *ready_user_ctx;  /*!< User context passed to ready_cb */
} bt_audio_le_ccp_ctx_t;

static const char *TAG = "BT_AUD_LE_CCP";
static bt_audio_le_ccp_ctx_t *s_ccp;

static esp_bt_audio_call_state_t bt_audio_le_ccp_tbs_to_call_state(uint8_t tbs_state)
{
    static const esp_bt_audio_call_state_t map[] = {
        ESP_BT_AUDIO_CALL_STATE_INCOMING,                   /* BT_TBS_CALL_STATE_INCOMING (0) */
        ESP_BT_AUDIO_CALL_STATE_DIALING,                    /* BT_TBS_CALL_STATE_DIALING (1) */
        ESP_BT_AUDIO_CALL_STATE_ALERTING,                   /* BT_TBS_CALL_STATE_ALERTING (2) */
        ESP_BT_AUDIO_CALL_STATE_ACTIVE,                     /* BT_TBS_CALL_STATE_ACTIVE (3) */
        ESP_BT_AUDIO_CALL_STATE_LOCALLY_HELD,               /* BT_TBS_CALL_STATE_LOCALLY_HELD (4) */
        ESP_BT_AUDIO_CALL_STATE_REMOTELY_HELD,              /* BT_TBS_CALL_STATE_REMOTELY_HELD (5) */
        ESP_BT_AUDIO_CALL_STATE_LOCALLY_AND_REMOTELY_HELD,  /* BT_TBS_CALL_STATE_LOCALLY_AND_REMOTELY_HELD (6) */
    };
    if (tbs_state < sizeof(map) / sizeof(map[0])) {
        return map[tbs_state];
    }
    return ESP_BT_AUDIO_CALL_STATE_INACTIVE;
}

static uint8_t bt_audio_le_ccp_resolve_idx(uint8_t idx)
{
    if (idx != 0 || !s_ccp || s_ccp->tracked_count == 0) {
        return idx;
    }
    return s_ccp->tracked_idx[0];
}

static void bt_audio_le_ccp_forget_call(uint8_t call_idx)
{
    if (!s_ccp) {
        return;
    }
    for (uint8_t i = 0; i < s_ccp->tracked_count; i++) {
        if (s_ccp->tracked_idx[i] == call_idx) {
            s_ccp->tracked_idx[i] = s_ccp->tracked_idx[--s_ccp->tracked_count];
            return;
        }
    }
}

static void bt_audio_le_ccp_dispatch_inactive(uint8_t call_idx, const char *reason)
{
    bt_audio_le_ccp_forget_call(call_idx);

    esp_bt_audio_event_call_state_t ev = {0};
    ev.tech = ESP_BT_AUDIO_TECH_LE;
    ev.idx = call_idx;
    ev.state = ESP_BT_AUDIO_CALL_STATE_INACTIVE;
    ESP_LOGI(TAG, "Call %u ended%s%s", call_idx, reason ? ": " : "", reason ? reason : "");
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CALL_STATE_CHG, &ev);
}

static esp_err_t bt_audio_le_ccp_answer_call(uint8_t idx)
{
    ESP_RETURN_ON_FALSE(s_ccp && s_ccp->conn_handle != BT_AUDIO_LE_CCP_CONN_HANDLE_NONE,
                        ESP_ERR_INVALID_STATE, TAG, "CCP not discovered");
    uint8_t tbs_idx = bt_audio_le_ccp_resolve_idx(idx);
    ESP_RETURN_ON_FALSE(tbs_idx != 0, ESP_ERR_INVALID_STATE, TAG, "No active call to answer");
    esp_err_t ret = esp_ble_audio_tbs_client_accept_call(s_ccp->conn_handle, BT_AUDIO_LE_CCP_DEFAULT_TBS_INST, tbs_idx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Answer call failed: tbs_idx %u, err %s", tbs_idx, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bt_audio_le_ccp_reject_call(uint8_t idx)
{
    ESP_RETURN_ON_FALSE(s_ccp && s_ccp->conn_handle != BT_AUDIO_LE_CCP_CONN_HANDLE_NONE,
                        ESP_ERR_INVALID_STATE, TAG, "CCP not discovered");
    uint8_t tbs_idx = bt_audio_le_ccp_resolve_idx(idx);
    ESP_RETURN_ON_FALSE(tbs_idx != 0, ESP_ERR_INVALID_STATE, TAG, "No active call to terminate");
    esp_err_t ret = esp_ble_audio_tbs_client_terminate_call(s_ccp->conn_handle,
                                                            BT_AUDIO_LE_CCP_DEFAULT_TBS_INST, tbs_idx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reject call failed: tbs_idx %u, err %s", tbs_idx, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bt_audio_le_ccp_dial(const char *number)
{
    ESP_RETURN_ON_FALSE(s_ccp && s_ccp->conn_handle != BT_AUDIO_LE_CCP_CONN_HANDLE_NONE,
                        ESP_ERR_INVALID_STATE, TAG, "CCP not discovered");
    ESP_RETURN_ON_FALSE(number && number[0], ESP_ERR_INVALID_ARG, TAG, "Dial number is empty");

    char uri_buf[CONFIG_BT_TBS_MAX_URI_LENGTH + 1];
    const char *uri = number;
    if (strchr(number, ':') == NULL) {
        const char *scheme = s_ccp->uri_scheme[0] ? s_ccp->uri_scheme : "tel";
        int len = snprintf(uri_buf, sizeof(uri_buf), "%s:%s", scheme, number);
        if (len <= 0 || len >= (int)sizeof(uri_buf)) {
            ESP_LOGE(TAG, "Dial failed: URI too long (%d chars, max %d)", len, CONFIG_BT_TBS_MAX_URI_LENGTH);
            return ESP_ERR_INVALID_ARG;
        }
        uri = uri_buf;
    }

    esp_err_t ret = esp_ble_audio_tbs_client_originate_call(s_ccp->conn_handle,
                                                            BT_AUDIO_LE_CCP_DEFAULT_TBS_INST,
                                                            uri);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Dial failed: originate call error %s", esp_err_to_name(ret));
    }
    return ret;
}

static inline void bt_audio_le_ccp_install_call_ops(void)
{
    esp_bt_audio_call_ops_t call_ops = {
        .answer_call = bt_audio_le_ccp_answer_call,
        .reject_call = bt_audio_le_ccp_reject_call,
        .dial = bt_audio_le_ccp_dial,
    };
    bt_audio_ops_set_call(&call_ops);
}

static inline void bt_audio_le_ccp_read_uri_list(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_tbs_client_read_uri_list(conn_handle,
                                                           BT_AUDIO_LE_CCP_DEFAULT_TBS_INST);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read URI list: %s", esp_err_to_name(ret));
    }
}

static void bt_audio_le_ccp_discover_cb(struct bt_conn *conn, int err, uint8_t tbs_count, bool gtbs_found)
{
    if (!s_ccp || !conn) {
        return;
    }

    ESP_LOGI(TAG, "TBS discovery complete, err %d, conn_handle %u, tbs_count %u, gtbs %u",
             err, conn->handle, tbs_count, gtbs_found);
    if (err == 0) {
        s_ccp->conn_handle = conn->handle;
        s_ccp->tbs_count = tbs_count;
        s_ccp->gtbs_found = gtbs_found;
        bt_audio_le_ccp_install_call_ops();
        if (gtbs_found) {
            esp_err_t ret = esp_ble_audio_tbs_client_read_ccid(conn->handle,
                                                               BT_AUDIO_LE_CCP_DEFAULT_TBS_INST);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to read TBS CCID: %s", esp_err_to_name(ret));
                bt_audio_le_ccp_read_uri_list(conn->handle);
            }
        }
    }
}

static void bt_audio_le_ccp_ccid_cb(struct bt_conn *conn, int err, uint8_t inst_index, uint32_t value)
{
    if (!s_ccp || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Read TBS CCID failed, err %d", err);
        bt_audio_le_ccp_read_uri_list(conn->handle);
        return;
    }
    if (inst_index != BT_AUDIO_LE_CCP_DEFAULT_TBS_INST) {
        ESP_LOGW(TAG, "Unexpected TBS instance index %u", inst_index);
        return;
    }
    s_ccp->ccid = (uint8_t)value;
    ESP_LOGI(TAG, "TBS content control ID %u", s_ccp->ccid);
    bt_audio_le_ccp_read_uri_list(conn->handle);
}

static void bt_audio_le_ccp_uri_list_cb(struct bt_conn *conn, int err, uint8_t inst_index, const char *value)
{
    if (!s_ccp || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Read URI schemes string failed, err %d", err);
        return;
    }
    if (inst_index != BT_AUDIO_LE_CCP_DEFAULT_TBS_INST) {
        ESP_LOGW(TAG, "Unexpected TBS instance index %u", inst_index);
        return;
    }
    if (value && value[0] && !s_ccp->uri_scheme[0]) {
        size_t len = 0;
        while (value[len] && value[len] != ',' && len < BT_AUDIO_LE_CCP_URI_SCHEME_MAX_LEN - 1) {
            len++;
        }
        memcpy(s_ccp->uri_scheme, value, len);
        s_ccp->uri_scheme[len] = '\0';
        ESP_LOGI(TAG, "TBS URI scheme: %s (from \"%s\")", s_ccp->uri_scheme, value);
    }
    if (s_ccp->ready_cb) {
        s_ccp->ready_cb(conn->handle, s_ccp->ready_user_ctx);
    }
    esp_err_t sync_ret = esp_ble_audio_tbs_client_read_call_state(conn->handle,
                                                                   BT_AUDIO_LE_CCP_DEFAULT_TBS_INST);
    if (sync_ret != ESP_OK) {
        ESP_LOGW(TAG, "Initial call state read failed: %s", esp_err_to_name(sync_ret));
    }
}

static void bt_audio_le_ccp_originate_cb(struct bt_conn *conn, int err, uint8_t inst_index, uint8_t call_index)
{
    (void)conn;
    if (err) {
        ESP_LOGE(TAG, "Originate call failed, err %d, inst %u", err, inst_index);
        return;
    }
    ESP_LOGD(TAG, "Call originated, inst %u, call_idx %u", inst_index, call_index);
    if (s_ccp && call_index != 0 && inst_index == BT_AUDIO_LE_CCP_DEFAULT_TBS_INST) {
        bool already_tracked = false;
        for (uint8_t i = 0; i < s_ccp->tracked_count; i++) {
            if (s_ccp->tracked_idx[i] == call_index) {
                already_tracked = true;
                break;
            }
        }
        if (!already_tracked && s_ccp->tracked_count < ESP_BT_AUDIO_CALL_MAX_NUM) {
            s_ccp->tracked_idx[s_ccp->tracked_count++] = call_index;
            ESP_LOGD(TAG, "Early-tracked call_idx %u", call_index);
        }
    }
}

static void bt_audio_le_ccp_terminate_cb(struct bt_conn *conn, int err, uint8_t inst_index, uint8_t call_index)
{
    (void)conn;
    ESP_LOGD(TAG, "Terminate CP: err %d, inst %u, call_idx %u", err, inst_index, call_index);
    if (err || inst_index != BT_AUDIO_LE_CCP_DEFAULT_TBS_INST || !s_ccp) {
        return;
    }
    bt_audio_le_ccp_dispatch_inactive(call_index, "local terminate complete");
}

static void bt_audio_le_ccp_cp_cb(struct bt_conn *conn, int err, uint8_t inst_index, uint8_t call_index)
{
    (void)conn;
    ESP_LOGD(TAG, "TBS control point complete, err %d, inst %u, call %u", err, inst_index, call_index);
}

static bool bt_audio_le_ccp_call_info_contains(uint8_t call_idx, uint8_t call_count,
                                               const esp_ble_audio_tbs_client_call_t *calls)
{
    for (uint8_t i = 0; i < call_count; i++) {
        if (calls[i].call_info.index == call_idx) {
            return true;
        }
    }
    return false;
}

static bool bt_audio_le_ccp_call_state_contains(uint8_t call_idx, uint8_t call_count,
                                                const esp_ble_audio_tbs_client_call_state_t *call_states)
{
    for (uint8_t i = 0; i < call_count; i++) {
        if (call_states[i].index == call_idx) {
            return true;
        }
    }
    return false;
}

static void bt_audio_le_ccp_current_calls_cb(struct bt_conn *conn, int err, uint8_t inst_index,
                                             uint8_t call_count,
                                             const esp_ble_audio_tbs_client_call_t *calls)
{
    if (!s_ccp || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Current calls read failed, err %d", err);
        return;
    }
    if (inst_index != BT_AUDIO_LE_CCP_DEFAULT_TBS_INST) {
        return;
    }
    ESP_LOGD(TAG, "Current calls: count %u (tracked %u)", call_count, s_ccp->tracked_count);
    for (uint8_t i = 0; i < s_ccp->tracked_count;) {
        uint8_t tracked_idx = s_ccp->tracked_idx[i];
        if (!bt_audio_le_ccp_call_info_contains(tracked_idx, call_count, calls)) {
            bt_audio_le_ccp_dispatch_inactive(tracked_idx, "removed from current calls");
            continue;
        }
        i++;
    }

    s_ccp->tracked_count = 0;
    for (uint8_t i = 0; i < call_count; i++) {
        const esp_ble_audio_tbs_client_call_t *call = &calls[i];
        if (s_ccp->tracked_count < ESP_BT_AUDIO_CALL_MAX_NUM) {
            s_ccp->tracked_idx[s_ccp->tracked_count++] = call->call_info.index;
        }
        esp_bt_audio_event_call_state_t ev = {0};
        ev.tech = ESP_BT_AUDIO_TECH_LE;
        ev.idx = call->call_info.index;
        ev.dir = (call->call_info.flags & BT_TBS_CALL_FLAG_OUTGOING) ?
                 ESP_BT_AUDIO_CALL_DIR_OUTGOING : ESP_BT_AUDIO_CALL_DIR_INCOMING;
        ev.state = bt_audio_le_ccp_tbs_to_call_state(call->call_info.state);
        if (call->remote_uri) {
            strncpy(ev.uri, call->remote_uri, sizeof(ev.uri) - 1);
        }
        ESP_LOGD(TAG, "  call %u: dir=%u state=%u uri=%s", ev.idx, ev.dir, ev.state, ev.uri);
        bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CALL_STATE_CHG, &ev);
    }
}

static void bt_audio_le_ccp_read_call_states_cb(struct bt_conn *conn, int err, uint8_t inst_index,
                                                uint8_t call_count,
                                                const esp_ble_audio_tbs_client_call_state_t *call_states)
{
    if (!s_ccp || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "TBS call states notification error %d, inst %u", err, inst_index);
        return;
    }
    if (inst_index != BT_AUDIO_LE_CCP_DEFAULT_TBS_INST) {
        return;
    }
    ESP_LOGD(TAG, "TBS call states: inst %u, count %u", inst_index, call_count);

    for (uint8_t i = 0; i < s_ccp->tracked_count;) {
        uint8_t tracked_idx = s_ccp->tracked_idx[i];
        if (!bt_audio_le_ccp_call_state_contains(tracked_idx, call_count, call_states)) {
            bt_audio_le_ccp_dispatch_inactive(tracked_idx, "removed from call states");
            continue;
        }
        i++;
    }

    s_ccp->tracked_count = 0;
    for (uint8_t i = 0; i < call_count && i < ESP_BT_AUDIO_CALL_MAX_NUM; i++) {
        s_ccp->tracked_idx[i] = call_states[i].index;
        s_ccp->tracked_count++;
    }

    for (uint8_t i = 0; i < call_count; i++) {
        esp_bt_audio_event_call_state_t ev = {0};
        ev.tech = ESP_BT_AUDIO_TECH_LE;
        ev.idx = call_states[i].index;
        ev.dir = (call_states[i].flags & BT_TBS_CALL_FLAG_OUTGOING) ?
                 ESP_BT_AUDIO_CALL_DIR_OUTGOING : ESP_BT_AUDIO_CALL_DIR_INCOMING;
        ev.state = bt_audio_le_ccp_tbs_to_call_state(call_states[i].state);
        bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CALL_STATE_CHG, &ev);
    }

    if (call_count > 0) {
        esp_err_t ret = esp_ble_audio_tbs_client_read_current_calls(s_ccp->conn_handle,
                                                                     BT_AUDIO_LE_CCP_DEFAULT_TBS_INST);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read current calls: %s", esp_err_to_name(ret));
        }
    }
}

static void bt_audio_le_ccp_termination_reason_cb(struct bt_conn *conn, int err, uint8_t inst_index,
                                                  uint8_t call_index, uint8_t reason)
{
    (void)conn;
    if (!s_ccp || err || inst_index != BT_AUDIO_LE_CCP_DEFAULT_TBS_INST) {
        return;
    }
    uint8_t resolved_idx = bt_audio_le_ccp_resolve_idx(call_index);
    ESP_LOGI(TAG, "TBS termination reason: call %u, reason %u", resolved_idx, reason);
    bt_audio_le_ccp_dispatch_inactive(resolved_idx, "termination reason notified");
}

esp_err_t bt_audio_le_ccp_init(bt_audio_le_ccp_ready_cb_t ready_cb, void *user_ctx)
{
    static esp_ble_audio_tbs_client_cb_t ccp_cbs;

    ESP_RETURN_ON_FALSE(!s_ccp, ESP_ERR_INVALID_STATE, TAG, "CCP already initialized");

    s_ccp = heap_caps_calloc_prefer(1, sizeof(*s_ccp), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(s_ccp, ESP_ERR_NO_MEM, TAG, "No memory for CCP");
    s_ccp->conn_handle = BT_AUDIO_LE_CCP_CONN_HANDLE_NONE;
    s_ccp->ready_cb = ready_cb;
    s_ccp->ready_user_ctx = user_ctx;

    ccp_cbs = (esp_ble_audio_tbs_client_cb_t){
        .discover = bt_audio_le_ccp_discover_cb,
        .ccid = bt_audio_le_ccp_ccid_cb,
        .uri_list = bt_audio_le_ccp_uri_list_cb,
        .originate_call = bt_audio_le_ccp_originate_cb,
        .terminate_call = bt_audio_le_ccp_terminate_cb,
        .accept_call = bt_audio_le_ccp_cp_cb,
        .call_state = bt_audio_le_ccp_read_call_states_cb,
        .current_calls = bt_audio_le_ccp_current_calls_cb,
        .termination_reason = bt_audio_le_ccp_termination_reason_cb,
    };

    esp_err_t ret = esp_ble_audio_tbs_client_register_cb(&ccp_cbs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init CCP failed: register callbacks error %s", esp_err_to_name(ret));
        heap_caps_free(s_ccp);
        s_ccp = NULL;
    }
    return ret;
}

void bt_audio_le_ccp_deinit(void)
{
    if (!s_ccp) {
        return;
    }
    bt_audio_ops_set_call(NULL);
    heap_caps_free(s_ccp);
    s_ccp = NULL;
}

esp_err_t bt_audio_le_ccp_discover(uint16_t conn_handle)
{
    ESP_RETURN_ON_FALSE(s_ccp, ESP_ERR_INVALID_STATE, TAG, "CCP not initialized");
    esp_err_t ret = esp_ble_audio_tbs_client_discover(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Discover CCP failed: conn_handle %u, err %s", conn_handle, esp_err_to_name(ret));
    }
    return ret;
}
