/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_ble_audio_tbs_api.h"
#include "host/conn_internal.h"
#include "bt_audio_ops.h"

#include "bt_audio_le_ccp.h"

#define BT_AUDIO_LE_CCP_CONN_HANDLE_NONE  UINT16_MAX
#define BT_AUDIO_LE_CCP_DEFAULT_TBS_INST  ESP_BLE_AUDIO_TBS_GTBS_INDEX

/**
 * @brief  Runtime context for the Call Control Profile client.
 */
typedef struct {
    uint16_t                    conn_handle;     /*!< Discovered TBS connection handle */
    uint8_t                     tbs_count;       /*!< Number of discovered TBS instances */
    uint8_t                     ccid;            /*!< TBS content control ID */
    bool                        gtbs_found;      /*!< True when GTBS was discovered */
    bt_audio_le_ccp_ready_cb_t  ready_cb;        /*!< Optional discovery-ready callback */
    void                       *ready_user_ctx;  /*!< User context passed to ready_cb */
} bt_audio_le_ccp_ctx_t;

static const char *TAG = "BT_AUD_LE_CCP";
static bt_audio_le_ccp_ctx_t *s_ccp;

static esp_err_t bt_audio_le_ccp_answer_call(uint8_t idx)
{
    ESP_RETURN_ON_FALSE(s_ccp && s_ccp->conn_handle != BT_AUDIO_LE_CCP_CONN_HANDLE_NONE,
                        ESP_ERR_INVALID_STATE, TAG, "CCP not discovered");
    esp_err_t ret = esp_ble_audio_tbs_client_accept_call(s_ccp->conn_handle, BT_AUDIO_LE_CCP_DEFAULT_TBS_INST, idx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Answer call failed: idx %u, err %s", idx, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bt_audio_le_ccp_reject_call(uint8_t idx)
{
    ESP_RETURN_ON_FALSE(s_ccp && s_ccp->conn_handle != BT_AUDIO_LE_CCP_CONN_HANDLE_NONE,
                        ESP_ERR_INVALID_STATE, TAG, "CCP not discovered");
    esp_err_t ret = esp_ble_audio_tbs_client_terminate_call(s_ccp->conn_handle, BT_AUDIO_LE_CCP_DEFAULT_TBS_INST, idx);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Reject call failed: idx %u, err %s", idx, esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bt_audio_le_ccp_dial(const char *number)
{
    ESP_RETURN_ON_FALSE(s_ccp && s_ccp->conn_handle != BT_AUDIO_LE_CCP_CONN_HANDLE_NONE,
                        ESP_ERR_INVALID_STATE, TAG, "CCP not discovered");
    ESP_RETURN_ON_FALSE(number && number[0], ESP_ERR_INVALID_ARG, TAG, "Dial number is empty");
    esp_err_t ret = esp_ble_audio_tbs_client_originate_call(s_ccp->conn_handle,
                                                            BT_AUDIO_LE_CCP_DEFAULT_TBS_INST,
                                                            number);
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
    (void)value;
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
    if (s_ccp->ready_cb) {
        s_ccp->ready_cb(conn->handle, s_ccp->ready_user_ctx);
    }
}

static void bt_audio_le_ccp_cp_cb(struct bt_conn *conn, int err, uint8_t inst_index, uint8_t call_index)
{
    (void)conn;
    ESP_LOGD(TAG, "TBS control point complete, err %d, inst %u, call %u", err, inst_index, call_index);
}

static void bt_audio_le_ccp_read_call_states_cb(struct bt_conn *conn, int err, uint8_t inst_index,
                                                uint8_t call_count,
                                                const esp_ble_audio_tbs_client_call_state_t *call_states)
{
    (void)call_states;
    (void)conn;
    ESP_LOGD(TAG, "TBS call states, err %d, inst %u, count %u", err, inst_index, call_count);
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

    ccp_cbs = (esp_ble_audio_tbs_client_cb_t) {
        .discover = bt_audio_le_ccp_discover_cb,
        .ccid = bt_audio_le_ccp_ccid_cb,
        .uri_list = bt_audio_le_ccp_uri_list_cb,
        .originate_call = bt_audio_le_ccp_cp_cb,
        .terminate_call = bt_audio_le_ccp_cp_cb,
        .accept_call = bt_audio_le_ccp_cp_cb,
        .call_state = bt_audio_le_ccp_read_call_states_cb,
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
