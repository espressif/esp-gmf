/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_iso_common_api.h"

#include "bt_audio_le_broadcast_sink.h"
#include "bt_audio_le_scan_delegator.h"

static bool s_initialized = false;
static const char *TAG = "BT_AUD_LE_SDE";

static void bt_audio_le_scan_delegator_recv_state_updated(struct bt_conn *conn,
                                                          const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state)
{
    if (s_initialized && recv_state->pa_sync_state == ESP_BLE_AUDIO_BAP_PA_STATE_SYNCED) {
        bt_audio_le_broadcast_sink_set_recv_state(recv_state);
    }
}

static int bt_audio_le_scan_delegator_pa_sync_req(struct bt_conn *conn,
                                                  const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
                                                  bool past_avail,
                                                  uint16_t pa_interval)
{
    if (!s_initialized || !recv_state || !conn) {
        ESP_LOGW(TAG, "PA sync request failed: scan delegator or receive state is NULL");
        return -EINVAL;
    }
    esp_err_t ret = ESP_OK;

    if (past_avail) {
        ret = bt_audio_le_broadcast_sink_sync_with_past(conn, recv_state);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to receive PAST, falling back to scan: %s", esp_err_to_name(ret));
            return -EIO;
        }
    } else {
        ret = bt_audio_le_broadcast_sink_sync_without_past(recv_state);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to receive PA sync without PAST, falling back to scan: %s", esp_err_to_name(ret));
            return -EIO;
        }
    }
    return 0;
}

static int bt_audio_le_scan_delegator_pa_sync_term_req(struct bt_conn *conn,
                                                       const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state)
{
    ESP_LOGI(TAG, "Broadcast assistant requested PA sync termination");
    bt_audio_le_broadcast_sink_pa_sync_terminate();
    return 0;
}

static void bt_audio_le_scan_delegator_broadcast_code(struct bt_conn *conn,
                                                      const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
                                                      const uint8_t broadcast_code[ESP_BLE_ISO_BROADCAST_CODE_SIZE])
{
    ESP_LOGI(TAG, "Broadcast assistant requested broadcast code, broadcast_id 0x%06lx", recv_state->broadcast_id);
    bt_audio_le_broadcast_sink_set_broadcast_code(recv_state, broadcast_code);
}

static int bt_audio_le_scan_delegator_bis_sync_req(struct bt_conn *conn,
                                                   const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
                                                   const uint32_t bis_sync_req[CONFIG_BT_BAP_BASS_MAX_SUBGROUPS])
{
    if (!s_initialized || !recv_state) {
        ESP_LOGW(TAG, "BIS sync request failed: scan delegator or receive state is NULL");
        return -EINVAL;
    }
    ESP_LOGI(TAG, "Broadcast assistant requested BIS sync, broadcast_id 0x%06lx, bis 0x%08lx",
             recv_state->broadcast_id, bis_sync_req[0]);
    bt_audio_le_broadcast_sink_set_bis_sync_req(recv_state, bis_sync_req[0]);
    return 0;
}

static void bt_audio_le_scan_delegator_scanning_state(struct bt_conn *conn, bool is_scanning)
{
    ESP_LOGI(TAG, "Broadcast assistant scanning %s", is_scanning ? "started" : "stopped");
}

static esp_ble_audio_bap_scan_delegator_cb_t s_scan_delegator_cbs = {
    .recv_state_updated = bt_audio_le_scan_delegator_recv_state_updated,
    .pa_sync_req        = bt_audio_le_scan_delegator_pa_sync_req,
    .pa_sync_term_req   = bt_audio_le_scan_delegator_pa_sync_term_req,
    .broadcast_code     = bt_audio_le_scan_delegator_broadcast_code,
    .bis_sync_req       = bt_audio_le_scan_delegator_bis_sync_req,
    .scanning_state     = bt_audio_le_scan_delegator_scanning_state,
};

esp_err_t bt_audio_le_scan_delegator_init(bt_audio_le_adv_builder_t adv_builder)
{
    ESP_RETURN_ON_FALSE(!s_initialized, ESP_ERR_INVALID_STATE, TAG, "Scan delegator already initialized");
    if (adv_builder) {
        bt_audio_le_adv_builder_add_service_uuid16(adv_builder, ESP_BLE_AUDIO_UUID_BASS_VAL);
    }
    ESP_RETURN_ON_ERROR(esp_ble_audio_bap_scan_delegator_register(&s_scan_delegator_cbs), TAG,
                        "Failed to register scan delegator callback");
    s_initialized = true;
    return ESP_OK;
}

void bt_audio_le_scan_delegator_deinit(void)
{
    if (!s_initialized) {
        return;
    }
    esp_ble_audio_bap_scan_delegator_unregister();
    s_initialized = false;
}
