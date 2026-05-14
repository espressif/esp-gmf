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

/**
 * @brief  Runtime context for the BAP scan delegator.
 */
typedef struct {
    const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state;                                       /*!< Active BASS receive state */
    uint8_t                                              broadcast_code[ESP_BLE_ISO_BROADCAST_CODE_SIZE];  /*!< BIG code */
    bool                                                 has_broadcast_code;                               /*!< Broadcast code is valid */
    bt_audio_le_scan_delegator_scan_cb_t                 start_scan;                                       /*!< Callback to start scanning */
    bt_audio_le_scan_delegator_stop_scan_cb_t            stop_scan;                                        /*!< Callback to stop scanning */
} bt_audio_le_scan_delegator_ctx_t;

static const char *TAG = "BT_AUD_LE_SDE";
static bt_audio_le_scan_delegator_ctx_t *s_sde;

static void bt_audio_le_scan_delegator_recv_state_updated(struct bt_conn *conn,
                                                          const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state)
{
    (void)conn;
    if (s_sde) {
        s_sde->recv_state = recv_state;
    }
}

static int bt_audio_le_scan_delegator_pa_sync_req(struct bt_conn *conn,
                                                  const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
                                                  bool past_avail,
                                                  uint16_t pa_interval)
{
    (void)conn;
    (void)past_avail;
    (void)pa_interval;
    if (!s_sde || !recv_state) {
        ESP_LOGW(TAG, "PA sync request failed: scan delegator or receive state is NULL");
        return -EINVAL;
    }
    s_sde->recv_state = recv_state;

    uint32_t sync_bits = (recv_state->num_subgroups && recv_state->subgroups) ? recv_state->subgroups[0].bis_sync : ESP_BLE_AUDIO_BAP_BIS_SYNC_NO_PREF;
    ESP_LOGI(TAG, "Broadcast assistant requested PA sync, broadcast_id 0x%06lx, bis 0x%08lx",
             recv_state->broadcast_id, sync_bits);
    esp_err_t ret = bt_audio_le_broadcast_sink_accept_scan_delegator_req(
        recv_state,
        s_sde->has_broadcast_code ? s_sde->broadcast_code : NULL,
        sync_bits);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PA sync request failed: broadcast sink rejected request");
        return -EINVAL;
    }
    if (!s_sde->start_scan) {
        ESP_LOGE(TAG, "PA sync request failed: start scan callback is NULL");
        return -ENOTSUP;
    }

    ret = s_sde->start_scan(0);
    if (ret == ESP_ERR_INVALID_STATE && s_sde->stop_scan) {
        (void)s_sde->stop_scan();
        ret = s_sde->start_scan(0);
    }
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start scan for assistant request: %s", esp_err_to_name(ret));
        return -EIO;
    }

    esp_ble_audio_bap_scan_delegator_set_pa_state(recv_state->src_id, ESP_BLE_AUDIO_BAP_PA_STATE_INFO_REQ);
    return 0;
}

static int bt_audio_le_scan_delegator_pa_sync_term_req(struct bt_conn *conn,
                                                       const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state)
{
    (void)conn;
    (void)recv_state;
    bt_audio_le_broadcast_sink_pa_sync_terminate();
    return 0;
}

static void bt_audio_le_scan_delegator_broadcast_code(struct bt_conn *conn,
                                                      const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
                                                      const uint8_t broadcast_code[ESP_BLE_ISO_BROADCAST_CODE_SIZE])
{
    (void)conn;
    if (!s_sde) {
        return;
    }
    s_sde->recv_state = recv_state;
    memcpy(s_sde->broadcast_code, broadcast_code, sizeof(s_sde->broadcast_code));
    s_sde->has_broadcast_code = true;
    bt_audio_le_broadcast_sink_set_broadcast_code(recv_state, s_sde->broadcast_code);
}

static int bt_audio_le_scan_delegator_bis_sync_req(struct bt_conn *conn,
                                                   const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
                                                   const uint32_t bis_sync_req[CONFIG_BT_BAP_BASS_MAX_SUBGROUPS])
{
    (void)conn;
    if (!s_sde || !recv_state) {
        ESP_LOGW(TAG, "BIS sync request failed: scan delegator or receive state is NULL");
        return -EINVAL;
    }
    s_sde->recv_state = recv_state;
    if (bis_sync_req[0] == 0) {
        bt_audio_le_broadcast_sink_pa_sync_terminate();
    } else {
        bt_audio_le_broadcast_sink_set_bis_sync_req(recv_state, bis_sync_req[0]);
    }
    return 0;
}

static void bt_audio_le_scan_delegator_scanning_state(struct bt_conn *conn, bool is_scanning)
{
    (void)conn;
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

esp_err_t bt_audio_le_scan_delegator_init(bt_audio_le_adv_builder_t adv_builder,
                                          bt_audio_le_scan_delegator_scan_cb_t start_scan,
                                          bt_audio_le_scan_delegator_stop_scan_cb_t stop_scan)
{
    ESP_RETURN_ON_FALSE(!s_sde, ESP_ERR_INVALID_STATE, TAG, "Scan delegator already initialized");

    s_sde = heap_caps_calloc_prefer(1, sizeof(*s_sde), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(s_sde, ESP_ERR_NO_MEM, TAG, "No memory for scan delegator");
    s_sde->start_scan = start_scan;
    s_sde->stop_scan = stop_scan;

    if (adv_builder) {
        bt_audio_le_adv_builder_add_service_uuid16(adv_builder, ESP_BLE_AUDIO_UUID_BASS_VAL);
    }
    esp_err_t ret = esp_ble_audio_bap_scan_delegator_register(&s_scan_delegator_cbs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init scan delegator failed: register callback error %s", esp_err_to_name(ret));
    }
    return ret;
}

void bt_audio_le_scan_delegator_deinit(void)
{
    if (!s_sde) {
        return;
    }
    esp_ble_audio_bap_scan_delegator_unregister();
    heap_caps_free(s_sde);
    s_sde = NULL;
}
