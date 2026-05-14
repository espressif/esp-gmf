/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_bt_audio_playback.h"
#include "esp_ble_audio_mcc_api.h"
#include "esp_ble_audio_mcs_defs.h"
#include "host/conn_internal.h"
#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_ops.h"

#include "bt_audio_le_mcc.h"

#define BT_AUDIO_LE_MCC_CONN_HANDLE_NONE  UINT16_MAX
#define BT_AUDIO_LE_MCC_DURATION_STR_LEN  16

/**
 * @brief  Runtime context for the Media Control Client.
 */
typedef struct {
    uint16_t  conn_handle;                                     /*!< Discovered MCS connection handle */
    uint32_t  opcodes;                                         /*!< Supported MCS opcodes bitmask */
    uint32_t  notify_mask;                                     /*!< Playback notification subscription mask */
    uint8_t   content_control_id;                              /*!< MCS content control ID */
    char      duration_str[BT_AUDIO_LE_MCC_DURATION_STR_LEN];  /*!< Cached duration string for metadata */
} bt_audio_le_mcc_ctx_t;

static const char *TAG = "BT_AUD_LE_MCC";
static bt_audio_le_mcc_ctx_t *s_mcc;

static inline uint32_t bt_audio_le_mcc_media_state_to_playback_status(uint8_t state)
{
    switch (state) {
        case ESP_BLE_AUDIO_MCS_MEDIA_STATE_INACTIVE:
            return ESP_BT_AUDIO_PLAYBACK_STATUS_STOPPED;
        case ESP_BLE_AUDIO_MCS_MEDIA_STATE_PLAYING:
            return ESP_BT_AUDIO_PLAYBACK_STATUS_PLAYING;
        case ESP_BLE_AUDIO_MCS_MEDIA_STATE_PAUSED:
            return ESP_BT_AUDIO_PLAYBACK_STATUS_PAUSED;
        case ESP_BLE_AUDIO_MCS_MEDIA_STATE_SEEKING:
            return ESP_BT_AUDIO_PLAYBACK_STATUS_FWD_SEEK;
        default:
            ESP_LOGW(TAG, "Unknown MCC media state %u", state);
            return ESP_BT_AUDIO_PLAYBACK_STATUS_ERROR;
    }
}

static inline esp_err_t bt_audio_le_mcc_merge_read_result(esp_err_t current, esp_err_t next, bool *requested)
{
    if (next == ESP_OK) {
        *requested = true;
        return ESP_OK;
    }
    return current == ESP_OK ? next : current;
}

static inline void bt_audio_le_mcc_dispatch_metadata(uint32_t type, uint8_t *value, uint32_t length)
{
    esp_bt_audio_event_playback_metadata_t event = {
        .type = type,
        .length = length,
        .value = value,
    };
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_PLAYBACK_METADATA, &event);
}

static inline void bt_audio_le_mcc_dispatch_playback_status(uint32_t event_mask, uint32_t value)
{
    esp_bt_audio_event_playback_st_t event = {
        .event = event_mask,
    };

    if (event_mask == ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE) {
        event.evt_param.play_status = value;
    } else if (event_mask == ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED) {
        event.evt_param.position = value;
    }
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_PLAYBACK_STATUS_CHG, &event);
}

static inline esp_err_t bt_audio_le_mcc_send_cmd(uint8_t opcode)
{
    ESP_RETURN_ON_FALSE(s_mcc && s_mcc->conn_handle != BT_AUDIO_LE_MCC_CONN_HANDLE_NONE,
                        ESP_ERR_INVALID_STATE, TAG, "MCC not discovered");

    esp_ble_audio_mpl_cmd_t cmd = {
        .opcode = opcode,
        .use_param = false,
    };
    esp_err_t ret = esp_ble_audio_mcc_send_cmd(s_mcc->conn_handle, &cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Send MCC command failed: opcode %u, err %s", opcode, esp_err_to_name(ret));
    }
    return ret;
}

static inline void bt_audio_le_mcc_read_content_control_id(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_mcc_read_content_control_id(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "MCC content control ID not readable, err %d", ret);
    }
}

static inline void bt_audio_le_mcc_read_opcodes_supported(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_mcc_read_opcodes_supported(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MCC opcodes, err %d", ret);
    }
}

static inline void bt_audio_le_mcc_read_media_state(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_mcc_read_media_state(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "MCC media state not readable, err %d", ret);
    }
}

static inline void bt_audio_le_mcc_read_playing_orders_supported(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_mcc_read_playing_orders_supported(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "MCC playing orders supported not readable, err %d", ret);
    }
}

static inline void bt_audio_le_mcc_read_playing_order(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_mcc_read_playing_order(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "MCC playing order not readable, err %d", ret);
    }
}

static inline void bt_audio_le_mcc_read_seeking_speed(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_mcc_read_seeking_speed(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "MCC seeking speed not readable, err %d", ret);
    }
}

static inline void bt_audio_le_mcc_read_playback_speed(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_mcc_read_playback_speed(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "MCC playback speed not readable, err %d", ret);
    }
}

static inline void bt_audio_le_mcc_read_track_position(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_mcc_read_track_position(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "MCC track position not readable, err %d", ret);
    }
}

static inline void bt_audio_le_mcc_read_track_duration(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_mcc_read_track_duration(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "MCC track duration not readable, err %d", ret);
    }
}

static inline void bt_audio_le_mcc_read_track_title(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_mcc_read_track_title(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "MCC track title not readable, err %d", ret);
    }
}

static inline void bt_audio_le_mcc_read_player_name(uint16_t conn_handle)
{
    esp_err_t ret = esp_ble_audio_mcc_read_player_name(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "MCC player name not readable, err %d", ret);
    }
}

static esp_err_t bt_audio_le_mcc_play(void)
{
    return bt_audio_le_mcc_send_cmd(ESP_BLE_AUDIO_MCS_OPC_PLAY);
}

static esp_err_t bt_audio_le_mcc_pause(void)
{
    return bt_audio_le_mcc_send_cmd(ESP_BLE_AUDIO_MCS_OPC_PAUSE);
}

static esp_err_t bt_audio_le_mcc_stop(void)
{
    return bt_audio_le_mcc_send_cmd(ESP_BLE_AUDIO_MCS_OPC_STOP);
}

static esp_err_t bt_audio_le_mcc_next(void)
{
    return bt_audio_le_mcc_send_cmd(ESP_BLE_AUDIO_MCS_OPC_NEXT_TRACK);
}

static esp_err_t bt_audio_le_mcc_prev(void)
{
    return bt_audio_le_mcc_send_cmd(ESP_BLE_AUDIO_MCS_OPC_PREV_TRACK);
}

static esp_err_t bt_audio_le_mcc_request_metadata(uint32_t mask)
{
    ESP_RETURN_ON_FALSE(s_mcc && s_mcc->conn_handle != BT_AUDIO_LE_MCC_CONN_HANDLE_NONE,
                        ESP_ERR_INVALID_STATE, TAG, "MCC not discovered");

    esp_err_t ret = ESP_OK;
    bool requested = false;

    if (mask & ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE) {
        ret = bt_audio_le_mcc_merge_read_result(ret,
                                                esp_ble_audio_mcc_read_track_title(s_mcc->conn_handle),
                                                &requested);
    }
    if (mask & ESP_BT_AUDIO_PLAYBACK_METADATA_PLAYING_TIME) {
        ret = bt_audio_le_mcc_merge_read_result(ret,
                                                esp_ble_audio_mcc_read_track_duration(s_mcc->conn_handle),
                                                &requested);
    }
#if CONFIG_BT_OTS
    if (mask & ESP_BT_AUDIO_PLAYBACK_METADATA_COVER_ART) {
        ret = bt_audio_le_mcc_merge_read_result(ret,
                                                esp_ble_audio_mcc_otc_read_icon_object(s_mcc->conn_handle),
                                                &requested);
    }
#endif  /* CONFIG_BT_OTS */

    uint32_t unsupported = mask & ~(ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE | ESP_BT_AUDIO_PLAYBACK_METADATA_PLAYING_TIME |
#if CONFIG_BT_OTS
                                    ESP_BT_AUDIO_PLAYBACK_METADATA_COVER_ART |
#endif  /* CONFIG_BT_OTS */
                                    0);
    if (unsupported) {
        ESP_LOGD(TAG, "MCC metadata mask 0x%08lx is not supported by current MCS mapping", unsupported);
    }

    if (requested) {
        return ESP_OK;
    }
    if (ret == ESP_OK) {
        ESP_LOGW(TAG, "Request MCC metadata: no supported metadata in mask 0x%08lx", mask);
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_LOGE(TAG, "Request MCC metadata failed: %s", esp_err_to_name(ret));
    return ret;
}

static esp_err_t bt_audio_le_mcc_reg_notifications(uint32_t mask)
{
    ESP_RETURN_ON_FALSE(s_mcc && s_mcc->conn_handle != BT_AUDIO_LE_MCC_CONN_HANDLE_NONE,
                        ESP_ERR_INVALID_STATE, TAG, "MCC not discovered");

    s_mcc->notify_mask = mask;

    if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE) {
        bt_audio_le_mcc_read_media_state(s_mcc->conn_handle);
    }
    if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED) {
        bt_audio_le_mcc_read_track_position(s_mcc->conn_handle);
    }
    return ESP_OK;
}

static void bt_audio_le_mcc_discover_mcs_cb(struct bt_conn *conn, int err)
{
    if (!s_mcc || !conn) {
        ESP_LOGE(TAG, "MCS discovery complete: conn is NULL");
        return;
    }

    ESP_LOGI(TAG, "MCS discovery complete, err %d, conn_handle %u", err, conn->handle);
    if (err == 0) {
        s_mcc->conn_handle = conn->handle;
        bt_audio_le_mcc_read_opcodes_supported(conn->handle);
    }
}

static void bt_audio_le_mcc_send_cmd_cb(struct bt_conn *conn, int err, const esp_ble_audio_mpl_cmd_t *cmd)
{
    (void)conn;
    ESP_LOGD(TAG, "MCC command complete, err %d, opcode %u", err, cmd ? cmd->opcode : 0);
}

static void bt_audio_le_mcc_cmd_ntf(struct bt_conn *conn, int err, const esp_ble_audio_mpl_cmd_ntf_t *ntf)
{
    (void)conn;
    ESP_LOGD(TAG, "MCC command notify, err %d, has_ntf %u", err, ntf ? 1 : 0);
    if (!s_mcc || err) {
        return;
    }
    if (s_mcc->notify_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE) {
        bt_audio_le_mcc_read_media_state(s_mcc->conn_handle);
    }
}

static void bt_audio_le_mcc_track_changed_ntf(struct bt_conn *conn, int err)
{
    (void)conn;
    if (!s_mcc || err) {
        ESP_LOGW(TAG, "MCC track changed notification failed, err %d", err);
        return;
    }
    if (s_mcc->notify_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE) {
        bt_audio_le_mcc_dispatch_playback_status(ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE, 0);
    }
}

static void bt_audio_le_mcc_read_media_state_cb(struct bt_conn *conn, int err, uint8_t state)
{
    if (!s_mcc || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC media state, err %d", err);
    } else {
        ESP_LOGD(TAG, "MCC media state %u", state);
        if (s_mcc->notify_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE) {
            bt_audio_le_mcc_dispatch_playback_status(ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE,
                                                     bt_audio_le_mcc_media_state_to_playback_status(state));
        }
    }
}

static void bt_audio_le_mcc_read_player_name_cb(struct bt_conn *conn, int err, const char *name)
{
    if (!s_mcc || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC player name, err %d", err);
    } else {
        ESP_LOGI(TAG, "MCC player name %s", name ? name : "");
    }
}

static void bt_audio_le_mcc_read_track_title_cb(struct bt_conn *conn, int err, const char *title)
{
    if (!s_mcc || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC track title, err %d", err);
    } else {
        ESP_LOGD(TAG, "MCC track title %s", title ? title : "");
        bt_audio_le_mcc_dispatch_metadata(ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE,
                                          (uint8_t *)title,
                                          title ? strlen(title) : 0);
    }
}

static void bt_audio_le_mcc_read_track_duration_cb(struct bt_conn *conn, int err, int32_t dur)
{
    if (!s_mcc || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC track duration, err %d", err);
    } else {
        ESP_LOGD(TAG, "MCC track duration %ld", dur);
        int len = snprintf(s_mcc->duration_str, sizeof(s_mcc->duration_str), "%ld", dur);
        if (len < 0) {
            ESP_LOGE(TAG, "Read MCC track duration failed: format error");
            return;
        }
        size_t value_len = (size_t)len;
        if (value_len >= sizeof(s_mcc->duration_str)) {
            value_len = sizeof(s_mcc->duration_str) - 1;
        }
        bt_audio_le_mcc_dispatch_metadata(ESP_BT_AUDIO_PLAYBACK_METADATA_PLAYING_TIME,
                                          (uint8_t *)s_mcc->duration_str,
                                          value_len);
    }
}

static void bt_audio_le_mcc_read_track_position_cb(struct bt_conn *conn, int err, int32_t pos)
{
    if (!s_mcc || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC track position, err %d", err);
    } else {
        ESP_LOGD(TAG, "MCC track position %ld", pos);
        if (s_mcc->notify_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED) {
            bt_audio_le_mcc_dispatch_playback_status(ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED, pos);
        }
    }
}

static void bt_audio_le_mcc_read_playback_speed_cb(struct bt_conn *conn, int err, int8_t speed)
{
    if (!s_mcc || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC playback speed, err %d", err);
    } else {
        ESP_LOGD(TAG, "MCC playback speed %d", speed);
    }
}

static void bt_audio_le_mcc_read_seeking_speed_cb(struct bt_conn *conn, int err, int8_t speed)
{
    if (!s_mcc || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC seeking speed, err %d", err);
    } else {
        ESP_LOGD(TAG, "MCC seeking speed %d", speed);
    }
}

static void bt_audio_le_mcc_read_playing_order_cb(struct bt_conn *conn, int err, uint8_t order)
{
    if (!s_mcc || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC playing order, err %d", err);
    } else {
        ESP_LOGD(TAG, "MCC playing order %u", order);
    }
}

static void bt_audio_le_mcc_read_playing_orders_supported_cb(struct bt_conn *conn, int err, uint16_t orders)
{
    if (!s_mcc || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC playing orders supported, err %d", err);
    } else {
        ESP_LOGD(TAG, "MCC playing orders supported 0x%04x", orders);
    }
}

static void bt_audio_le_mcc_opcodes_supported_cb(struct bt_conn *conn, int err, uint32_t opcodes)
{
    esp_bt_audio_playback_ops_t playback_ops = {0};

    if (!s_mcc || !conn) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC opcodes, err %d", err);
        return;
    }

    s_mcc->opcodes = opcodes;
    if (opcodes & ESP_BLE_AUDIO_MCS_OPC_SUP_PLAY) {
        playback_ops.play = bt_audio_le_mcc_play;
    }
    if (opcodes & ESP_BLE_AUDIO_MCS_OPC_SUP_PAUSE) {
        playback_ops.pause = bt_audio_le_mcc_pause;
    }
    if (opcodes & ESP_BLE_AUDIO_MCS_OPC_SUP_STOP) {
        playback_ops.stop = bt_audio_le_mcc_stop;
    }
    if (opcodes & ESP_BLE_AUDIO_MCS_OPC_SUP_NEXT_TRACK) {
        playback_ops.next = bt_audio_le_mcc_next;
    }
    if (opcodes & ESP_BLE_AUDIO_MCS_OPC_SUP_PREV_TRACK) {
        playback_ops.prev = bt_audio_le_mcc_prev;
    }
    playback_ops.request_metadata = bt_audio_le_mcc_request_metadata;
    playback_ops.reg_notifications = bt_audio_le_mcc_reg_notifications;

    ESP_LOGI(TAG, "MCC supported opcodes 0x%08lx", opcodes);
    bt_audio_ops_set_playback(&playback_ops);
}

#if CONFIG_BT_OTS
static void bt_audio_le_mcc_otc_icon_object(struct bt_conn *conn, int err, struct net_buf_simple *buf)
{
    (void)conn;
    if (!s_mcc || !buf) {
        return;
    }
    if (err && err != -EMSGSIZE) {
        ESP_LOGE(TAG, "Failed to read MCC icon object, err %d", err);
        return;
    }

    esp_bt_audio_playback_cover_art_t cover_art = {
        .format_fourcc = ESP_BT_AUDIO_FOURCC_PNG,
        .size = buf->len,
        .data = buf->data,
    };
    bt_audio_le_mcc_dispatch_metadata(ESP_BT_AUDIO_PLAYBACK_METADATA_COVER_ART,
                                      (uint8_t *)&cover_art,
                                      sizeof(cover_art));
}
#endif  /* CONFIG_BT_OTS */

static void bt_audio_le_mcc_content_control_id_cb(struct bt_conn *conn, int err, uint8_t ccid)
{
    (void)conn;
    if (!s_mcc) {
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC content control ID, err %d", err);
        return;
    }
    s_mcc->content_control_id = ccid;
    ESP_LOGI(TAG, "MCC content control ID %u", ccid);
}

esp_err_t bt_audio_le_mcc_init(void)
{
    static esp_ble_audio_mcc_cb_t mcc_cbs;

    ESP_RETURN_ON_FALSE(!s_mcc, ESP_ERR_INVALID_STATE, TAG, "MCC already initialized");

    s_mcc = heap_caps_calloc_prefer(1, sizeof(*s_mcc), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(s_mcc, ESP_ERR_NO_MEM, TAG, "No memory for MCC");
    s_mcc->conn_handle = BT_AUDIO_LE_MCC_CONN_HANDLE_NONE;

    memset(&mcc_cbs, 0, sizeof(mcc_cbs));
    mcc_cbs.discover_mcs = bt_audio_le_mcc_discover_mcs_cb;
    mcc_cbs.send_cmd = bt_audio_le_mcc_send_cmd_cb;
    mcc_cbs.cmd_ntf = bt_audio_le_mcc_cmd_ntf;
    mcc_cbs.track_changed_ntf = bt_audio_le_mcc_track_changed_ntf;
    mcc_cbs.read_player_name = bt_audio_le_mcc_read_player_name_cb;
    mcc_cbs.read_track_title = bt_audio_le_mcc_read_track_title_cb;
    mcc_cbs.read_track_duration = bt_audio_le_mcc_read_track_duration_cb;
    mcc_cbs.read_track_position = bt_audio_le_mcc_read_track_position_cb;
    mcc_cbs.read_playback_speed = bt_audio_le_mcc_read_playback_speed_cb;
    mcc_cbs.read_seeking_speed = bt_audio_le_mcc_read_seeking_speed_cb;
    mcc_cbs.read_playing_order = bt_audio_le_mcc_read_playing_order_cb;
    mcc_cbs.read_playing_orders_supported = bt_audio_le_mcc_read_playing_orders_supported_cb;
    mcc_cbs.read_media_state = bt_audio_le_mcc_read_media_state_cb;
    mcc_cbs.read_opcodes_supported = bt_audio_le_mcc_opcodes_supported_cb;
    mcc_cbs.read_content_control_id = bt_audio_le_mcc_content_control_id_cb;
#if CONFIG_BT_OTS
    mcc_cbs.otc_icon_object = bt_audio_le_mcc_otc_icon_object;
#endif  /* CONFIG_BT_OTS */

    esp_err_t ret = esp_ble_audio_mcc_init(&mcc_cbs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init MCC failed: %s", esp_err_to_name(ret));
        heap_caps_free(s_mcc);
        s_mcc = NULL;
    }
    return ret;
}

void bt_audio_le_mcc_deinit(void)
{
    if (!s_mcc) {
        return;
    }
    bt_audio_ops_set_playback(NULL);
    heap_caps_free(s_mcc);
    s_mcc = NULL;
}

esp_err_t bt_audio_le_mcc_discover(uint16_t conn_handle)
{
    ESP_RETURN_ON_FALSE(s_mcc, ESP_ERR_INVALID_STATE, TAG, "MCC not initialized");
    esp_err_t ret = esp_ble_audio_mcc_discover_mcs(conn_handle, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Discover MCC failed: conn_handle %u, err %s", conn_handle, esp_err_to_name(ret));
    }
    return ret;
}
