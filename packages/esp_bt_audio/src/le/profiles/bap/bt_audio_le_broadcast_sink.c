/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <strings.h>

#include "host/ble_gap.h"
#include "host/ble_hs.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_iso_common_api.h"

#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_le_broadcast_sink.h"
#include "bt_audio_le_stream.h"

#define BT_AUDIO_LE_PA_SYNC_HANDLE_NONE          UINT16_MAX
#define BT_AUDIO_LE_INVALID_BROADCAST_ID         0xFFFFFFFFU
#define BT_AUDIO_LE_PA_SYNC_SUPERVISION_TIMEOUT  1000U

typedef enum {
    BT_AUDIO_LE_BSNK_PA_SYNC_IDLE,
    BT_AUDIO_LE_BSNK_PA_SYNCING,
    BT_AUDIO_LE_BSNK_PA_SYNCED,
} bt_audio_le_broadcast_sink_pa_state_t;

typedef enum {
    BT_AUDIO_LE_BSNK_BIG_SYNC_IDLE,
    BT_AUDIO_LE_BSNK_BIG_SYNCING,
} bt_audio_le_broadcast_sink_big_state_t;

/**
 * @brief  Runtime context for the broadcast sink profile.
 */
typedef struct {
    uint32_t                                             location;                                               /*!< Sink audio location bitmask */
    uint32_t                                             target_broadcast_id;                                    /*!< Broadcast ID filter */
    uint32_t                                             requested_bis_sync;                                     /*!< Requested BIS sync bitfield */
    uint32_t                                             bis_index_bitfield;                                     /*!< BIS indexes parsed from BASE */
    uint8_t                                              target_name[32];                                        /*!< Broadcast name filter */
    uint8_t                                              broadcast_code[ESP_BLE_ISO_BROADCAST_CODE_SIZE];        /*!< BIG code */
    bool                                                 has_broadcast_code;                                     /*!< Broadcast code is valid */
    uint16_t                                             pa_sync_handle;                                         /*!< Periodic advertising sync handle */
    bool                                                 pa_syncing;                                             /*!< Periodic advertising sync is pending */
    bool                                                 base_received;                                          /*!< BASE has been received */
    bool                                                 biginfo_received;                                       /*!< BIGInfo has been received */
    bool                                                 biginfo_encrypted;                                      /*!< BIGInfo indicates encryption */
    bt_audio_le_broadcast_sink_pa_state_t                pa_state;                                               /*!< Periodic advertising sync state */
    bt_audio_le_broadcast_sink_big_state_t               big_state;                                              /*!< BIG sync state */
    esp_ble_audio_bap_broadcast_sink_t                  *sink;                                                   /*!< BLE Audio broadcast sink instance */
    const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state;                                             /*!< Active BASS receive state */
    uint8_t                                              active_stream_count;                                    /*!< Number of active streams */
    bt_audio_le_stream_t                                *streams[CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT];      /*!< Streams */
    esp_ble_audio_bap_stream_t                          *bap_streams[CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT];  /*!< BAP streams */
    bt_audio_le_gap_event_cb_t                           gap_cb;                                                 /*!< GAP callback for PA sync operations */
} bt_audio_le_broadcast_sink_ctx_t;

static const char *TAG = "BT_AUD_LE_BSNK";
static bt_audio_le_broadcast_sink_ctx_t *s_bsnk;

static inline bool bt_audio_le_broadcast_sink_addr_matches_recv_state(const esp_bt_audio_event_device_discovered_t *device)
{
    if (!s_bsnk || !s_bsnk->recv_state || !device) {
        return false;
    }

    if (s_bsnk->recv_state->addr.type != device->disc_data.le.addr_type) {
        return false;
    }
    return memcmp(s_bsnk->recv_state->addr.a.val, device->addr, sizeof(device->addr)) == 0;
}

static inline void bt_audio_le_broadcast_sink_set_scan_delegator_pa_state(esp_ble_audio_bap_pa_state_t pa_state)
{
    if (s_bsnk && s_bsnk->recv_state) {
        int err = esp_ble_audio_bap_scan_delegator_set_pa_state(s_bsnk->recv_state->src_id, pa_state);
        if (err) {
            ESP_LOGW(TAG, "Failed to set scan delegator PA state %d: %d", pa_state, err);
        }
    }
}

static inline void bt_audio_le_broadcast_sink_reset(void)
{
    if (!s_bsnk) {
        return;
    }
    if (s_bsnk->sink) {
        int err = esp_ble_audio_bap_broadcast_sink_delete(s_bsnk->sink);
        if (err) {
            ESP_LOGW(TAG, "Failed to delete broadcast sink: %d", err);
        }
        s_bsnk->sink = NULL;
    }
    s_bsnk->pa_sync_handle = BT_AUDIO_LE_PA_SYNC_HANDLE_NONE;
    s_bsnk->pa_syncing = false;
    s_bsnk->base_received = false;
    s_bsnk->biginfo_received = false;
    s_bsnk->biginfo_encrypted = false;
    s_bsnk->bis_index_bitfield = 0;
    s_bsnk->pa_state = BT_AUDIO_LE_BSNK_PA_SYNC_IDLE;
    s_bsnk->big_state = BT_AUDIO_LE_BSNK_BIG_SYNC_IDLE;
    s_bsnk->active_stream_count = 0;
    s_bsnk->target_broadcast_id = BT_AUDIO_LE_INVALID_BROADCAST_ID;
    s_bsnk->recv_state = NULL;
}

static inline uint8_t bt_audio_le_broadcast_sink_get_stream_count(uint32_t sync_bitfield)
{
    uint8_t count = __builtin_popcount((unsigned int)sync_bitfield);

    if (count == 0 || count > CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT) {
        count = CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT;
    }
    return count;
}

static inline void bt_audio_le_broadcast_sink_dispatch_streams(esp_bt_audio_stream_state_t state)
{
    uint8_t count = s_bsnk ? s_bsnk->active_stream_count : 0;

    if (count == 0) {
        return;
    }
    if (count > CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT) {
        count = CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT;
    }
    for (uint8_t i = 0; i < count; i++) {
        if (state == ESP_BT_AUDIO_STREAM_STATE_ALLOCATED) {
            bt_audio_le_stream_dispatch_allocated(s_bsnk->streams[i]);
        } else {
            bt_audio_le_stream_dispatch_state(s_bsnk->streams[i], state);
        }
    }
}

static esp_err_t bt_audio_le_broadcast_sink_try_big_sync(void)
{
    uint32_t sync_bitfield;
    const uint8_t *code = NULL;

    if (!s_bsnk || !s_bsnk->sink || !s_bsnk->base_received || !s_bsnk->biginfo_received) {
        ESP_LOGD(TAG, "BIG sync not ready: sink or BASE/BIGInfo state incomplete");
        return ESP_ERR_INVALID_STATE;
    }
    if (s_bsnk->pa_state != BT_AUDIO_LE_BSNK_PA_SYNCED) {
        ESP_LOGD(TAG, "BIG sync not ready: PA state %d", (int)s_bsnk->pa_state);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_bsnk->big_state == BT_AUDIO_LE_BSNK_BIG_SYNCING) {
        return ESP_OK;
    }

    sync_bitfield = s_bsnk->requested_bis_sync;
    if (sync_bitfield == ESP_BLE_AUDIO_BAP_BIS_SYNC_NO_PREF || sync_bitfield == 0) {
        sync_bitfield = s_bsnk->bis_index_bitfield;
    } else {
        sync_bitfield &= s_bsnk->bis_index_bitfield;
    }
    if (!sync_bitfield) {
        ESP_LOGW(TAG, "No matching BIS indexes");
        return ESP_ERR_NOT_FOUND;
    }
    if (s_bsnk->biginfo_encrypted) {
        if (!s_bsnk->has_broadcast_code) {
            return ESP_ERR_INVALID_STATE;
        }
        code = s_bsnk->broadcast_code;
    }

    esp_err_t err = esp_ble_audio_bap_broadcast_sink_sync(s_bsnk->sink, sync_bitfield, s_bsnk->bap_streams, code);
    if (err) {
        ESP_LOGW(TAG, "BIG sync failed: %s", esp_err_to_name(err));
        return err;
    }

    s_bsnk->requested_bis_sync = sync_bitfield;
    s_bsnk->active_stream_count = bt_audio_le_broadcast_sink_get_stream_count(sync_bitfield);
    s_bsnk->big_state = BT_AUDIO_LE_BSNK_BIG_SYNCING;
    bt_audio_le_broadcast_sink_dispatch_streams(ESP_BT_AUDIO_STREAM_STATE_ALLOCATED);
    ESP_LOGI(TAG, "BIG sync requested, bis 0x%08lx", sync_bitfield);
    return ESP_OK;
}

static esp_err_t bt_audio_le_broadcast_sink_finish_pa_sync(uint16_t sync_handle)
{
    ESP_RETURN_ON_FALSE(s_bsnk, ESP_ERR_INVALID_STATE, TAG, "Broadcast sink not initialized");
    ESP_RETURN_ON_FALSE(sync_handle != BT_AUDIO_LE_PA_SYNC_HANDLE_NONE,
                        ESP_ERR_INVALID_ARG, TAG, "Invalid PA sync handle");
    ESP_RETURN_ON_FALSE(s_bsnk->target_broadcast_id != BT_AUDIO_LE_INVALID_BROADCAST_ID,
                        ESP_ERR_INVALID_STATE, TAG, "Broadcast ID is not configured");

    s_bsnk->pa_syncing = false;
    s_bsnk->pa_sync_handle = sync_handle;
    s_bsnk->pa_state = BT_AUDIO_LE_BSNK_PA_SYNCED;

    if (!s_bsnk->sink) {
        esp_err_t err = esp_ble_audio_bap_broadcast_sink_create(sync_handle,
                                                                s_bsnk->target_broadcast_id,
                                                                &s_bsnk->sink);
        ESP_RETURN_ON_ERROR(err, TAG, "Failed to create broadcast sink");
    }

    bt_audio_le_broadcast_sink_set_scan_delegator_pa_state(ESP_BLE_AUDIO_BAP_PA_STATE_SYNCED);
    bt_audio_le_broadcast_sink_try_big_sync();
    return ESP_OK;
}

static esp_err_t bt_audio_le_broadcast_sink_create_pa_sync(const esp_bt_audio_event_device_discovered_t *device)
{
    struct ble_gap_periodic_sync_params params = {0};
    ble_addr_t sync_addr = {0};
    int err;

    ESP_RETURN_ON_FALSE(s_bsnk && device, ESP_ERR_INVALID_ARG, TAG, "Invalid PA sync args");
    if (s_bsnk->pa_syncing || s_bsnk->pa_sync_handle != BT_AUDIO_LE_PA_SYNC_HANDLE_NONE) {
        ESP_LOGD(TAG, "Periodic sync already active or pending");
        return ESP_OK;
    }

    sync_addr.type = device->disc_data.le.addr_type;
    memcpy(sync_addr.val, device->addr, sizeof(sync_addr.val));
    params.skip = 0;
    params.sync_timeout = BT_AUDIO_LE_PA_SYNC_SUPERVISION_TIMEOUT;
    err = ble_gap_periodic_adv_sync_create(&sync_addr, device->disc_data.le.sid, &params, s_bsnk->gap_cb, NULL);
    ESP_RETURN_ON_FALSE(err == 0, ESP_FAIL, TAG, "Failed to create PA sync: %d", err);
    s_bsnk->pa_syncing = true;
    s_bsnk->pa_state = BT_AUDIO_LE_BSNK_PA_SYNCING;
    if (s_bsnk->target_broadcast_id == BT_AUDIO_LE_INVALID_BROADCAST_ID) {
        s_bsnk->target_broadcast_id = device->disc_data.le.broadcast_id;
    }
    bt_audio_le_broadcast_sink_set_scan_delegator_pa_state(ESP_BLE_AUDIO_BAP_PA_STATE_INFO_REQ);
    return ESP_OK;
}

static void bt_audio_le_broadcast_sink_base_recv(esp_ble_audio_bap_broadcast_sink_t *sink,
                                                 const esp_ble_audio_bap_base_t *base,
                                                 size_t base_size)
{
    (void)sink;
    (void)base_size;
    uint32_t indexes = 0;
    uint32_t pres_delay = 0;

    if (!s_bsnk || s_bsnk->base_received) {
        return;
    }

    if (esp_ble_audio_bap_base_get_bis_indexes(base, &indexes) != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get BIS indexes from BASE");
        return;
    }
    if (esp_ble_audio_bap_base_get_pres_delay(base, &pres_delay) == ESP_OK) {
        ESP_LOGI(TAG, "Presentation delay: %" PRIu32 " us", pres_delay);
        for (uint8_t i = 0; i < CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT; i++) {
            s_bsnk->streams[i]->presentation_delay = pres_delay;
        }
    } else {
        ESP_LOGW(TAG, "Failed to get presentation delay from BASE");
    }
    s_bsnk->bis_index_bitfield = indexes;
    s_bsnk->base_received = true;
    bt_audio_le_broadcast_sink_try_big_sync();
}

static void bt_audio_le_broadcast_sink_syncable(esp_ble_audio_bap_broadcast_sink_t *sink,
                                                const esp_ble_iso_biginfo_t *biginfo)
{
    (void)sink;
    if (!s_bsnk || !biginfo) {
        return;
    }

    s_bsnk->biginfo_received = true;
    s_bsnk->biginfo_encrypted = biginfo->encryption;
    bt_audio_le_broadcast_sink_try_big_sync();
}

static esp_ble_audio_bap_broadcast_sink_cb_t s_broadcast_sink_cbs = {
    .base_recv = bt_audio_le_broadcast_sink_base_recv,
    .syncable  = bt_audio_le_broadcast_sink_syncable,
};

esp_err_t bt_audio_le_broadcast_sink_init(uint32_t location, bt_audio_le_gap_event_cb_t gap_cb)
{
    ESP_RETURN_ON_FALSE(!s_bsnk, ESP_ERR_INVALID_STATE, TAG, "Broadcast sink already initialized");

    s_bsnk = heap_caps_calloc_prefer(1, sizeof(*s_bsnk), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(s_bsnk, ESP_ERR_NO_MEM, TAG, "No memory for broadcast sink");

    s_bsnk->location = location;
    s_bsnk->requested_bis_sync = ESP_BLE_AUDIO_BAP_BIS_SYNC_NO_PREF;
    s_bsnk->target_broadcast_id = BT_AUDIO_LE_INVALID_BROADCAST_ID;
    s_bsnk->pa_sync_handle = BT_AUDIO_LE_PA_SYNC_HANDLE_NONE;
    s_bsnk->gap_cb = gap_cb;

    for (uint8_t i = 0; i < CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT; i++) {
        ESP_RETURN_ON_ERROR(bt_audio_le_stream_create(&s_bsnk->streams[i]), TAG,
                            "Failed to create broadcast sink stream");
        s_bsnk->streams[i]->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_LE_BROADCAST;
        s_bsnk->streams[i]->base.direction = ESP_BT_AUDIO_STREAM_DIR_SINK;
        s_bsnk->bap_streams[i] = &s_bsnk->streams[i]->bap_stream;
    }

    ESP_RETURN_ON_ERROR(esp_ble_audio_bap_broadcast_sink_register_cb(&s_broadcast_sink_cbs),
                        TAG, "Failed to register broadcast sink callbacks");
    return ESP_OK;
}

void bt_audio_le_broadcast_sink_deinit(void)
{
    if (!s_bsnk) {
        return;
    }
    bt_audio_le_broadcast_sink_pa_sync_terminate();
    for (uint8_t i = 0; i < CONFIG_BT_BAP_BROADCAST_SNK_STREAM_COUNT; i++) {
        bt_audio_le_stream_destroy(s_bsnk->streams[i]);
    }
    heap_caps_free(s_bsnk);
    s_bsnk = NULL;
}

esp_err_t bt_audio_le_broadcast_sink_sync(const uint8_t *broadcast_name, const uint8_t *broadcast_code,
                                          uint32_t bit_field, uint32_t timeout_ms)
{
    (void)timeout_ms;
    ESP_RETURN_ON_FALSE(s_bsnk, ESP_ERR_INVALID_STATE, TAG, "Broadcast sink not initialized");

    memset(s_bsnk->target_name, 0, sizeof(s_bsnk->target_name));
    memset(s_bsnk->broadcast_code, 0, sizeof(s_bsnk->broadcast_code));
    s_bsnk->has_broadcast_code = false;
    s_bsnk->recv_state = NULL;
    s_bsnk->target_broadcast_id = BT_AUDIO_LE_INVALID_BROADCAST_ID;
    if (broadcast_name) {
        size_t name_len = strlen((const char *)broadcast_name);
        size_t copy_len = MIN(name_len, sizeof(s_bsnk->target_name) - 1);
        memcpy(s_bsnk->target_name, broadcast_name, copy_len);
    }
    if (broadcast_code) {
        memcpy(s_bsnk->broadcast_code, broadcast_code, ESP_BLE_ISO_BROADCAST_CODE_SIZE);
        s_bsnk->has_broadcast_code = true;
    }
    s_bsnk->requested_bis_sync = bit_field ? bit_field : ESP_BLE_AUDIO_BAP_BIS_SYNC_NO_PREF;
    return ESP_OK;
}

esp_err_t bt_audio_le_broadcast_sink_pa_sync_terminate(void)
{
    if (!s_bsnk) {
        ESP_LOGW(TAG, "PA sync terminate failed: broadcast sink not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = ESP_OK;

    bt_audio_le_broadcast_sink_set_scan_delegator_pa_state(ESP_BLE_AUDIO_BAP_PA_STATE_NOT_SYNCED);
    if (s_bsnk->sink) {
        int err = esp_ble_audio_bap_broadcast_sink_stop(s_bsnk->sink);
        if (err) {
            ESP_LOGW(TAG, "Failed to stop broadcast sink: %d", err);
            ret = ESP_FAIL;
        }
    }
    bt_audio_le_broadcast_sink_dispatch_streams(ESP_BT_AUDIO_STREAM_STATE_RELEASED);

    if (s_bsnk->pa_sync_handle != BT_AUDIO_LE_PA_SYNC_HANDLE_NONE) {
        int err = ble_gap_periodic_adv_sync_terminate(s_bsnk->pa_sync_handle);
        if (err) {
            ESP_LOGW(TAG, "Failed to terminate PA sync: %d", err);
            ret = ESP_FAIL;
        }
    } else if (s_bsnk->pa_syncing) {
        int err = ble_gap_periodic_adv_sync_create_cancel();
        if (err) {
            ESP_LOGW(TAG, "Failed to cancel pending PA sync: %d", err);
            ret = ESP_FAIL;
        }
    }

    bt_audio_le_broadcast_sink_reset();
    return ret;
}

esp_err_t bt_audio_le_broadcast_sink_accept_scan_delegator_req(const esp_ble_audio_bap_scan_delegator_recv_state_t
                                                               *recv_state,
                                                               const uint8_t *broadcast_code, uint32_t bit_field)
{
    ESP_RETURN_ON_FALSE(s_bsnk && recv_state, ESP_ERR_INVALID_ARG, TAG, "Invalid scan delegator request");
    ESP_RETURN_ON_FALSE(s_bsnk->pa_state == BT_AUDIO_LE_BSNK_PA_SYNC_IDLE &&
                            s_bsnk->big_state == BT_AUDIO_LE_BSNK_BIG_SYNC_IDLE &&
                            !s_bsnk->sink,
                        ESP_ERR_INVALID_STATE, TAG, "Broadcast sink is busy");

    s_bsnk->recv_state = recv_state;
    s_bsnk->target_broadcast_id = recv_state->broadcast_id;
    s_bsnk->requested_bis_sync = bit_field ? bit_field : ESP_BLE_AUDIO_BAP_BIS_SYNC_NO_PREF;
    memset(s_bsnk->target_name, 0, sizeof(s_bsnk->target_name));
    memset(s_bsnk->broadcast_code, 0, sizeof(s_bsnk->broadcast_code));
    s_bsnk->has_broadcast_code = false;
    if (broadcast_code) {
        memcpy(s_bsnk->broadcast_code, broadcast_code, ESP_BLE_ISO_BROADCAST_CODE_SIZE);
        s_bsnk->has_broadcast_code = true;
    }
    return ESP_OK;
}

esp_err_t bt_audio_le_broadcast_sink_sync_with_past(
                                                    struct bt_conn *conn,
                                                    const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state)
{
    ESP_RETURN_ON_FALSE(s_bsnk, ESP_ERR_INVALID_STATE, TAG, "Broadcast sink not initialized");
    ESP_RETURN_ON_FALSE(s_bsnk->pa_state == BT_AUDIO_LE_BSNK_PA_SYNC_IDLE &&
                            s_bsnk->pa_sync_handle == BT_AUDIO_LE_PA_SYNC_HANDLE_NONE &&
                            !s_bsnk->pa_syncing,
                        ESP_ERR_INVALID_STATE, TAG, "PA sync is busy");

    s_bsnk->recv_state = recv_state;

    struct ble_gap_periodic_sync_params params = {0};
    params.skip = 0;
    params.sync_timeout = 1000;
    int err = ble_gap_periodic_adv_sync_receive(conn->handle, &params, s_bsnk->gap_cb, NULL);
    if (err) {
        ESP_LOGW(TAG, "Failed to enable PAST receive: %d", err);
        return ESP_FAIL;
    }
    bt_audio_le_broadcast_sink_set_scan_delegator_pa_state(ESP_BLE_AUDIO_BAP_PA_STATE_INFO_REQ);
    ESP_LOGI(TAG, "PAST receive enabled, conn_handle %u, timeout 0x%04x", conn->handle, params.sync_timeout);
    s_bsnk->pa_state = BT_AUDIO_LE_BSNK_PA_SYNCING;
    s_bsnk->target_broadcast_id = recv_state->broadcast_id;
    return ESP_OK;
}

esp_err_t bt_audio_le_broadcast_sink_sync_without_past(const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state)
{
    ESP_RETURN_ON_FALSE(s_bsnk && recv_state, ESP_ERR_INVALID_ARG, TAG, "Invalid receive state");
    s_bsnk->recv_state = recv_state;
    struct ble_gap_periodic_sync_params params = {0};
    ble_addr_t sync_addr = {0};
    int err;

    sync_addr.type = recv_state->addr.type;
    memcpy(sync_addr.val, recv_state->addr.a.val, sizeof(sync_addr.val));
    params.skip = 0;
    params.sync_timeout = 1000;

    err = ble_gap_periodic_adv_sync_create(&sync_addr, recv_state->adv_sid, &params,
                                           s_bsnk->gap_cb, NULL);
    if (err) {
        ESP_LOGW(TAG, "Failed to create PA sync without past: %d", err);
        return ESP_FAIL;
    }
    s_bsnk->pa_syncing = true;
    s_bsnk->pa_state = BT_AUDIO_LE_BSNK_PA_SYNCING;
    s_bsnk->target_broadcast_id = recv_state->broadcast_id;
    return ESP_OK;
}

esp_err_t bt_audio_le_broadcast_sink_set_broadcast_code(const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
                                                        const uint8_t broadcast_code[ESP_BLE_ISO_BROADCAST_CODE_SIZE])
{
    ESP_RETURN_ON_FALSE(s_bsnk && recv_state && broadcast_code, ESP_ERR_INVALID_ARG, TAG, "Invalid broadcast code");
    s_bsnk->recv_state = recv_state;
    memcpy(s_bsnk->broadcast_code, broadcast_code, ESP_BLE_ISO_BROADCAST_CODE_SIZE);
    s_bsnk->has_broadcast_code = true;
    (void)bt_audio_le_broadcast_sink_try_big_sync();
    return ESP_OK;
}

esp_err_t bt_audio_le_broadcast_sink_set_bis_sync_req(
                                                      const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
                                                      uint32_t bit_field)
{
    ESP_RETURN_ON_FALSE(s_bsnk && recv_state, ESP_ERR_INVALID_ARG, TAG, "Invalid BIS sync request");
    s_bsnk->recv_state = recv_state;
    s_bsnk->requested_bis_sync = bit_field;
    if (bit_field == 0) {
        return bt_audio_le_broadcast_sink_pa_sync_terminate();
    }
    bt_audio_le_broadcast_sink_try_big_sync();
    return ESP_OK;
}

esp_err_t bt_audio_le_broadcast_sink_set_recv_state(const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state)
{
    ESP_RETURN_ON_FALSE(s_bsnk && recv_state, ESP_ERR_INVALID_ARG, TAG, "Invalid receive state");
    s_bsnk->recv_state = recv_state;
    return ESP_OK;
}

void bt_audio_le_broadcast_sink_on_device(const esp_bt_audio_event_device_discovered_t *device)
{
    if (!s_bsnk || !device) {
        return;
    }
    if (bt_audio_le_broadcast_sink_addr_matches_recv_state(device)) {
        bt_audio_le_broadcast_sink_create_pa_sync(device);
        return;
    }
    if (s_bsnk->target_name[0] &&
        strcasecmp((const char *)s_bsnk->target_name, device->disc_data.le.broadcast_name) == 0) {
        bt_audio_le_broadcast_sink_create_pa_sync(device);
        return;
    }
    if (s_bsnk->target_broadcast_id != BT_AUDIO_LE_INVALID_BROADCAST_ID &&
        s_bsnk->target_broadcast_id == device->disc_data.le.broadcast_id) {
        bt_audio_le_broadcast_sink_create_pa_sync(device);
    }
}

void bt_audio_le_broadcast_sink_on_gap_event(esp_ble_audio_gap_app_event_t *event)
{
    if (!s_bsnk || !event) {
        return;
    }

    switch (event->type) {
        case ESP_BLE_AUDIO_GAP_EVENT_PA_SYNC:
            if (event->pa_sync.status == 0) {
                ESP_LOGI(TAG, "PA established, sync_handle %u", event->pa_sync.sync_handle);
                bt_audio_le_broadcast_sink_finish_pa_sync(event->pa_sync.sync_handle);
            } else {
                ESP_LOGE(TAG, "PA sync failed, status %d", event->pa_sync.status);
                bt_audio_le_broadcast_sink_set_scan_delegator_pa_state(ESP_BLE_AUDIO_BAP_PA_STATE_FAILED);
                bt_audio_le_broadcast_sink_reset();
            }
            break;
        case ESP_BLE_AUDIO_GAP_EVENT_PA_SYNC_PAST:
            if (event->pa_sync_past.status == 0) {
                ESP_LOGI(TAG, "PAST established, conn_handle %u, sync_handle %u",
                         event->pa_sync_past.conn_handle, event->pa_sync_past.sync_handle);
                bt_audio_le_broadcast_sink_finish_pa_sync(event->pa_sync_past.sync_handle);
            } else {
                ESP_LOGE(TAG, "PAST sync failed, status %d", event->pa_sync_past.status);
                bt_audio_le_broadcast_sink_set_scan_delegator_pa_state(ESP_BLE_AUDIO_BAP_PA_STATE_NO_PAST);
                bt_audio_le_broadcast_sink_reset();
            }
            break;
        case ESP_BLE_AUDIO_GAP_EVENT_PA_SYNC_LOST:
            if (s_bsnk->recv_state && s_bsnk->recv_state->pa_sync_state != ESP_BLE_AUDIO_BAP_PA_STATE_NOT_SYNCED) {
                int err = esp_ble_audio_bap_scan_delegator_set_pa_state(s_bsnk->recv_state->src_id,
                                                                        ESP_BLE_AUDIO_BAP_PA_STATE_NOT_SYNCED);
                if (err) {
                    ESP_LOGE(TAG, "Failed to set PA state to ESP_BLE_AUDIO_BAP_PA_STATE_NOT_SYNCED, err %d", err);
                }
            }
            if (s_bsnk->recv_state && s_bsnk->recv_state->src_id != 0) {
                int err = esp_ble_audio_bap_scan_delegator_rem_src(s_bsnk->recv_state->src_id);
                if (err) {
                    ESP_LOGE(TAG, "Failed to remove source from scan delegator, err %d", err);
                }
            }
            if (s_bsnk->sink) {
                esp_ble_audio_bap_broadcast_sink_stop(s_bsnk->sink);
            }
            bt_audio_le_broadcast_sink_dispatch_streams(ESP_BT_AUDIO_STREAM_STATE_RELEASED);
            if (event->pa_sync_lost.sync_handle == s_bsnk->pa_sync_handle) {
                esp_bt_audio_event_t evt = ESP_BT_AUDIO_EVENT_PA_SYNC_LOST;
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, evt, NULL);
            }
            bt_audio_le_broadcast_sink_reset();
            break;
        case ESP_BLE_ISO_GAP_EVENT_BIGINFO_RECV:
            if (event->biginfo_recv.sync_handle == s_bsnk->pa_sync_handle) {
                s_bsnk->biginfo_received = true;
                s_bsnk->biginfo_encrypted = event->biginfo_recv.encryption;
                bt_audio_le_broadcast_sink_try_big_sync();
            }
            break;
        default:
            break;
    }
}
