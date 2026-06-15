/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_check.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_playback.h"
#include "esp_ble_audio_mcc_api.h"
#include "esp_ble_audio_mcs_defs.h"
#include "host/conn_internal.h"
#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_ops.h"

#include "bt_audio_le_mcc.h"

#define BT_AUDIO_LE_MCC_CONN_HANDLE_NONE             UINT16_MAX
#define BT_AUDIO_LE_MCC_DURATION_STR_LEN             16
#define BT_AUDIO_LE_MCC_OP_TIMEOUT_MS                3000
#define BT_AUDIO_LE_MCC_OP_TIMEOUT_US                (BT_AUDIO_LE_MCC_OP_TIMEOUT_MS * 1000)
#if CONFIG_BT_MCC_OTS
#define BT_AUDIO_LE_MCC_METADATA_SUPPORTED_MASK      \
    (ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE | \
     ESP_BT_AUDIO_PLAYBACK_METADATA_PLAYING_TIME | \
     ESP_BT_AUDIO_PLAYBACK_METADATA_COVER_ART)
#else
#define BT_AUDIO_LE_MCC_METADATA_SUPPORTED_MASK      \
    (ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE | \
     ESP_BT_AUDIO_PLAYBACK_METADATA_PLAYING_TIME)
#endif  /* CONFIG_BT_MCC_OTS */

typedef enum {
    BT_AUDIO_LE_MCC_OP_CMD,
    BT_AUDIO_LE_MCC_OP_READ_CONTENT_CONTROL_ID,
    BT_AUDIO_LE_MCC_OP_READ_OPCODES_SUPPORTED,
    BT_AUDIO_LE_MCC_OP_READ_MEDIA_STATE,
    BT_AUDIO_LE_MCC_OP_READ_PLAYING_ORDERS_SUPPORTED,
    BT_AUDIO_LE_MCC_OP_READ_PLAYING_ORDER,
    BT_AUDIO_LE_MCC_OP_READ_SEEKING_SPEED,
    BT_AUDIO_LE_MCC_OP_READ_PLAYBACK_SPEED,
    BT_AUDIO_LE_MCC_OP_READ_TRACK_POSITION,
    BT_AUDIO_LE_MCC_OP_READ_TRACK_DURATION,
    BT_AUDIO_LE_MCC_OP_READ_TRACK_TITLE,
    BT_AUDIO_LE_MCC_OP_READ_PLAYER_NAME,
#if CONFIG_BT_MCC_OTS
    BT_AUDIO_LE_MCC_OP_READ_CURRENT_TRACK_OBJECT_ID,
    BT_AUDIO_LE_MCC_OP_READ_CURRENT_TRACK_OBJECT,
#endif  /* CONFIG_BT_MCC_OTS */
} bt_audio_le_mcc_op_type_t;

typedef struct bt_audio_le_mcc_op_node {
    bt_audio_le_mcc_op_type_t        type;
    uint16_t                         conn_handle;
    uint8_t                          opcode;
    struct bt_audio_le_mcc_op_node  *next;
} bt_audio_le_mcc_op_node_t;

/**
 * @brief  Runtime context for the Media Control Client.
 */
typedef struct {
    uint16_t                    conn_handle;            /*!< Discovered MCS connection handle */
    uint32_t                    opcodes;                /*!< Supported MCS opcodes bitmask */
    uint32_t                    notify_mask;            /*!< Playback notification subscription mask */
    uint8_t                     content_control_id;     /*!< MCS content control ID */
    bool                        op_busy;                /*!< A MCC operation is waiting for completion */
    bt_audio_le_mcc_op_type_t   active_op_type;         /*!< Current MCC operation type */
    uint16_t                    active_op_conn_handle;  /*!< Current MCC operation connection handle */
    uint8_t                     active_op_opcode;       /*!< Current MCC command opcode */
    int64_t                     active_op_started_us;   /*!< Active MCC operation start time */
    bt_audio_le_mcc_op_node_t  *op_head;                /*!< Pending MCC operation list head */
    bt_audio_le_mcc_op_node_t  *op_tail;                /*!< Pending MCC operation list tail */
    SemaphoreHandle_t           op_lock;                /*!< Protects the pending MCC operation list */
    esp_timer_handle_t          op_timer;               /*!< Watchdog timer for active MCC operation */
    char                        duration_str[BT_AUDIO_LE_MCC_DURATION_STR_LEN];  /*!< Cached duration string */
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

#if CONFIG_BT_MCC_OTS
static bool bt_audio_le_mcc_detect_image_format(const uint8_t *data, size_t len, uint32_t *format_fourcc)
{
    if (data == NULL || format_fourcc == NULL) {
        return false;
    }
    if (len >= 8 &&
        data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G' &&
        data[4] == 0x0d && data[5] == 0x0a && data[6] == 0x1a && data[7] == 0x0a) {
        *format_fourcc = ESP_BT_AUDIO_FOURCC_PNG;
        return true;
    }
    if (len >= 3 && data[0] == 0xff && data[1] == 0xd8 && data[2] == 0xff) {
        *format_fourcc = ESP_BT_AUDIO_FOURCC_JPEG;
        return true;
    }
    if (len >= 6 &&
        data[0] == 'G' && data[1] == 'I' && data[2] == 'F' &&
        data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a') {
        *format_fourcc = ESP_BT_AUDIO_FOURCC_GIF;
        return true;
    }
    if (len >= 12 &&
        data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
        *format_fourcc = ESP_BT_AUDIO_FOURCC_WEBP;
        return true;
    }
    if (len >= 2 && data[0] == 'B' && data[1] == 'M') {
        *format_fourcc = ESP_BT_AUDIO_FOURCC_BMP;
        return true;
    }
    return false;
}
#endif  /* CONFIG_BT_MCC_OTS */

static esp_err_t bt_audio_le_mcc_execute_op(const bt_audio_le_mcc_op_node_t *op)
{
    esp_err_t ret = ESP_FAIL;

    switch (op->type) {
        case BT_AUDIO_LE_MCC_OP_CMD: {
            esp_ble_audio_mpl_cmd_t cmd = {
                .opcode = op->opcode,
                .use_param = false,
            };
            ret = esp_ble_audio_mcc_send_cmd(op->conn_handle, &cmd);
            break;
        }
        case BT_AUDIO_LE_MCC_OP_READ_CONTENT_CONTROL_ID:
            ret = esp_ble_audio_mcc_read_content_control_id(op->conn_handle);
            break;
        case BT_AUDIO_LE_MCC_OP_READ_OPCODES_SUPPORTED:
            ret = esp_ble_audio_mcc_read_opcodes_supported(op->conn_handle);
            break;
        case BT_AUDIO_LE_MCC_OP_READ_MEDIA_STATE:
            ret = esp_ble_audio_mcc_read_media_state(op->conn_handle);
            break;
        case BT_AUDIO_LE_MCC_OP_READ_PLAYING_ORDERS_SUPPORTED:
            ret = esp_ble_audio_mcc_read_playing_orders_supported(op->conn_handle);
            break;
        case BT_AUDIO_LE_MCC_OP_READ_PLAYING_ORDER:
            ret = esp_ble_audio_mcc_read_playing_order(op->conn_handle);
            break;
        case BT_AUDIO_LE_MCC_OP_READ_SEEKING_SPEED:
            ret = esp_ble_audio_mcc_read_seeking_speed(op->conn_handle);
            break;
        case BT_AUDIO_LE_MCC_OP_READ_PLAYBACK_SPEED:
            ret = esp_ble_audio_mcc_read_playback_speed(op->conn_handle);
            break;
        case BT_AUDIO_LE_MCC_OP_READ_TRACK_POSITION:
            ret = esp_ble_audio_mcc_read_track_position(op->conn_handle);
            break;
        case BT_AUDIO_LE_MCC_OP_READ_TRACK_DURATION:
            ret = esp_ble_audio_mcc_read_track_duration(op->conn_handle);
            break;
        case BT_AUDIO_LE_MCC_OP_READ_TRACK_TITLE:
            ret = esp_ble_audio_mcc_read_track_title(op->conn_handle);
            break;
        case BT_AUDIO_LE_MCC_OP_READ_PLAYER_NAME:
            ret = esp_ble_audio_mcc_read_player_name(op->conn_handle);
            break;
#if CONFIG_BT_MCC_OTS
        case BT_AUDIO_LE_MCC_OP_READ_CURRENT_TRACK_OBJECT_ID:
            ret = esp_ble_audio_mcc_read_current_track_obj_id(op->conn_handle);
            break;
        case BT_AUDIO_LE_MCC_OP_READ_CURRENT_TRACK_OBJECT:
            ret = esp_ble_audio_mcc_otc_read_current_track_object(op->conn_handle);
            break;
#endif  /* CONFIG_BT_MCC_OTS */
        default:
            ESP_LOGE(TAG, "Unknown MCC operation type %d", op->type);
            return ESP_ERR_INVALID_ARG;
    }

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MCC operation failed: type %d, opcode %u, err %s",
                 op->type, op->opcode, esp_err_to_name(ret));
    }
    return ret;
}

static void bt_audio_le_mcc_clear_pending_ops(void)
{
    if (!s_mcc) {
        return;
    }

    bt_audio_le_mcc_op_node_t *node = s_mcc->op_head;
    s_mcc->op_head = NULL;
    s_mcc->op_tail = NULL;
    while (node) {
        bt_audio_le_mcc_op_node_t *next = node->next;
        heap_caps_free(node);
        node = next;
    }
}

static void bt_audio_le_mcc_release_context(bool stop_timer)
{
    if (!s_mcc) {
        return;
    }

    if (s_mcc->op_timer) {
        if (stop_timer) {
            (void)esp_timer_stop(s_mcc->op_timer);
        }
        esp_timer_delete(s_mcc->op_timer);
    }
    if (s_mcc->op_lock) {
        vSemaphoreDelete(s_mcc->op_lock);
    }
    heap_caps_free(s_mcc);
    s_mcc = NULL;
}

static void bt_audio_le_mcc_insert_pending_op(bt_audio_le_mcc_op_node_t *node)
{
    node->next = NULL;

    if (s_mcc->op_tail) {
        s_mcc->op_tail->next = node;
    } else {
        s_mcc->op_head = node;
    }
    s_mcc->op_tail = node;
}

static inline void bt_audio_le_mcc_reset_active_op(void)
{
    s_mcc->op_busy = false;
    s_mcc->active_op_type = 0;
    s_mcc->active_op_conn_handle = BT_AUDIO_LE_MCC_CONN_HANDLE_NONE;
    s_mcc->active_op_opcode = 0;
    s_mcc->active_op_started_us = 0;
}

static void bt_audio_le_mcc_try_execute_next(void);

static inline void bt_audio_le_mcc_start_op_timer(bt_audio_le_mcc_op_type_t type, uint16_t conn_handle, uint8_t opcode)
{
    if (!s_mcc || !s_mcc->op_lock || !s_mcc->op_timer) {
        return;
    }

    if (xSemaphoreTake(s_mcc->op_lock, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Take MCC operation lock failed");
        return;
    }
    if (!s_mcc->op_busy ||
        s_mcc->active_op_type != type ||
        s_mcc->active_op_conn_handle != conn_handle ||
        s_mcc->active_op_opcode != opcode) {
        xSemaphoreGive(s_mcc->op_lock);
        return;
    }
    s_mcc->active_op_started_us = esp_timer_get_time();
    (void)esp_timer_stop(s_mcc->op_timer);
    esp_err_t ret = esp_timer_start_once(s_mcc->op_timer, BT_AUDIO_LE_MCC_OP_TIMEOUT_US);
    xSemaphoreGive(s_mcc->op_lock);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Start MCC operation timer failed: %s", esp_err_to_name(ret));
    }
}

static inline void bt_audio_le_mcc_stop_op_timer(void)
{
    if (s_mcc && s_mcc->op_timer) {
        (void)esp_timer_stop(s_mcc->op_timer);
    }
}

static void bt_audio_le_mcc_try_execute_next(void)
{
    while (true) {
        if (!s_mcc || !s_mcc->op_lock) {
            return;
        }

        bt_audio_le_mcc_op_node_t *node = NULL;

        if (xSemaphoreTake(s_mcc->op_lock, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Take MCC operation lock failed");
            return;
        }

        if (s_mcc->op_busy || s_mcc->op_head == NULL) {
            xSemaphoreGive(s_mcc->op_lock);
            return;
        }

        node = s_mcc->op_head;
        s_mcc->op_head = node->next;
        if (s_mcc->op_head == NULL) {
            s_mcc->op_tail = NULL;
        }
        s_mcc->op_busy = true;
        s_mcc->active_op_type = node->type;
        s_mcc->active_op_conn_handle = node->conn_handle;
        s_mcc->active_op_opcode = node->opcode;
        xSemaphoreGive(s_mcc->op_lock);

        bt_audio_le_mcc_op_type_t type = node->type;
        uint16_t conn_handle = node->conn_handle;
        uint8_t opcode = node->opcode;
        esp_err_t ret = bt_audio_le_mcc_execute_op(node);
        heap_caps_free(node);
        if (ret == ESP_OK) {
            bt_audio_le_mcc_start_op_timer(type, conn_handle, opcode);
            return;
        }

        if (xSemaphoreTake(s_mcc->op_lock, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Take MCC operation lock failed");
            return;
        }
        bt_audio_le_mcc_reset_active_op();
        xSemaphoreGive(s_mcc->op_lock);
    }
}

static void bt_audio_le_mcc_complete_op(bt_audio_le_mcc_op_type_t type)
{
    if (!s_mcc || !s_mcc->op_lock) {
        return;
    }

    if (xSemaphoreTake(s_mcc->op_lock, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Take MCC operation lock failed");
        return;
    }
    if (!s_mcc->op_busy || s_mcc->active_op_type != type) {
        xSemaphoreGive(s_mcc->op_lock);
        return;
    }
    bt_audio_le_mcc_reset_active_op();
    xSemaphoreGive(s_mcc->op_lock);

    bt_audio_le_mcc_stop_op_timer();
    bt_audio_le_mcc_try_execute_next();
}

static void bt_audio_le_mcc_op_timer_cb(void *arg)
{
    (void)arg;

    if (!s_mcc || !s_mcc->op_lock) {
        return;
    }

    if (xSemaphoreTake(s_mcc->op_lock, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Take MCC operation lock failed");
        return;
    }

    if (s_mcc->op_busy &&
        s_mcc->active_op_started_us > 0 &&
        esp_timer_get_time() - s_mcc->active_op_started_us >= BT_AUDIO_LE_MCC_OP_TIMEOUT_US) {
        ESP_LOGW(TAG, "MCC operation timeout: type %d, conn_handle %u, opcode %u",
                 s_mcc->active_op_type, s_mcc->active_op_conn_handle, s_mcc->active_op_opcode);
        bt_audio_le_mcc_reset_active_op();
    }
    xSemaphoreGive(s_mcc->op_lock);

    bt_audio_le_mcc_try_execute_next();
}

static esp_err_t bt_audio_le_mcc_queue_op(bt_audio_le_mcc_op_type_t type, uint16_t conn_handle, uint8_t opcode)
{
    ESP_RETURN_ON_FALSE(s_mcc && conn_handle != BT_AUDIO_LE_MCC_CONN_HANDLE_NONE,
                        ESP_ERR_INVALID_STATE, TAG, "MCC not discovered");
    ESP_RETURN_ON_FALSE(s_mcc->op_lock, ESP_ERR_INVALID_STATE, TAG, "MCC operation lock not initialized");

    bt_audio_le_mcc_op_node_t *node = heap_caps_calloc_prefer(1, sizeof(*node), 2,
                                                              MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                                              MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(node, ESP_ERR_NO_MEM, TAG, "No memory for pending MCC operation");
    node->type = type;
    node->conn_handle = conn_handle;
    node->opcode = opcode;

    if (xSemaphoreTake(s_mcc->op_lock, portMAX_DELAY) != pdTRUE) {
        heap_caps_free(node);
        ESP_LOGE(TAG, "Take MCC operation lock failed");
        return ESP_FAIL;
    }

    bt_audio_le_mcc_insert_pending_op(node);
    xSemaphoreGive(s_mcc->op_lock);

    bt_audio_le_mcc_try_execute_next();
    return ESP_OK;
}

static inline void bt_audio_le_mcc_queue_read_op(bt_audio_le_mcc_op_type_t type, uint16_t conn_handle)
{
    (void)bt_audio_le_mcc_queue_op(type, conn_handle, 0);
}

static esp_err_t bt_audio_le_mcc_send_cmd(uint8_t opcode)
{
    ESP_RETURN_ON_FALSE(s_mcc, ESP_ERR_INVALID_STATE, TAG, "MCC not initialized");
    return bt_audio_le_mcc_queue_op(BT_AUDIO_LE_MCC_OP_CMD, s_mcc->conn_handle, opcode);
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

static inline esp_err_t bt_audio_le_mcc_request_metadata_read(esp_err_t ret, uint32_t mask,
                                                              uint32_t metadata, bt_audio_le_mcc_op_type_t type,
                                                              bool *requested)
{
    if (!(mask & metadata)) {
        return ret;
    }
    return bt_audio_le_mcc_merge_read_result(ret,
                                             bt_audio_le_mcc_queue_op(type, s_mcc->conn_handle, 0),
                                             requested);
}

static esp_err_t bt_audio_le_mcc_request_metadata(uint32_t mask)
{
    ESP_RETURN_ON_FALSE(s_mcc && s_mcc->conn_handle != BT_AUDIO_LE_MCC_CONN_HANDLE_NONE,
                        ESP_ERR_INVALID_STATE, TAG, "MCC not discovered");

    esp_err_t ret = ESP_OK;
    bool requested = false;

    ret = bt_audio_le_mcc_request_metadata_read(ret, mask, ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE,
                                                BT_AUDIO_LE_MCC_OP_READ_TRACK_TITLE, &requested);
    ret = bt_audio_le_mcc_request_metadata_read(ret, mask, ESP_BT_AUDIO_PLAYBACK_METADATA_PLAYING_TIME,
                                                BT_AUDIO_LE_MCC_OP_READ_TRACK_DURATION, &requested);
#if CONFIG_BT_MCC_OTS
    ret = bt_audio_le_mcc_request_metadata_read(ret, mask, ESP_BT_AUDIO_PLAYBACK_METADATA_COVER_ART,
                                                BT_AUDIO_LE_MCC_OP_READ_CURRENT_TRACK_OBJECT_ID, &requested);
#endif  /* CONFIG_BT_MCC_OTS */

    uint32_t unsupported = mask & ~BT_AUDIO_LE_MCC_METADATA_SUPPORTED_MASK;
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
    ESP_RETURN_ON_FALSE(s_mcc, ESP_ERR_INVALID_STATE, TAG, "MCC not initialized");

    s_mcc->notify_mask = mask;

    if (s_mcc->conn_handle == BT_AUDIO_LE_MCC_CONN_HANDLE_NONE) {
        return ESP_OK;
    }

    if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE) {
        bt_audio_le_mcc_queue_read_op(BT_AUDIO_LE_MCC_OP_READ_MEDIA_STATE, s_mcc->conn_handle);
    }
    if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED) {
        bt_audio_le_mcc_queue_read_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_POSITION, s_mcc->conn_handle);
    }
    if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE) {
        bt_audio_le_mcc_queue_read_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_TITLE, s_mcc->conn_handle);
        bt_audio_le_mcc_queue_read_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_DURATION, s_mcc->conn_handle);
    }
    return ESP_OK;
}

static void bt_audio_le_mcc_discover_mcs_cb(struct bt_conn *conn, int err)
{
    if (!s_mcc || !conn) {
        ESP_LOGE(TAG, "MCS discovery complete: conn is NULL");
        return;
    }

    if (err == 0) {
        s_mcc->conn_handle = conn->handle;
        bt_audio_le_mcc_queue_read_op(BT_AUDIO_LE_MCC_OP_READ_OPCODES_SUPPORTED, conn->handle);
        bt_audio_le_mcc_queue_read_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_TITLE, conn->handle);
        bt_audio_le_mcc_queue_read_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_DURATION, conn->handle);
    }
}

static void bt_audio_le_mcc_send_cmd_cb(struct bt_conn *conn, int err, const esp_ble_audio_mpl_cmd_t *cmd)
{
    (void)conn;
    if (err) {
        ESP_LOGW(TAG, "MCC command complete: err %d, opcode %u", err, cmd ? cmd->opcode : 0);
    }
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_CMD);
}

static void bt_audio_le_mcc_cmd_ntf(struct bt_conn *conn, int err, const esp_ble_audio_mpl_cmd_ntf_t *ntf)
{
    (void)conn;
    (void)ntf;
    if (err) {
        ESP_LOGW(TAG, "MCC command notify: err %d", err);
        return;
    }
    if (!s_mcc) {
        return;
    }
    if (s_mcc->notify_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE) {
        bt_audio_le_mcc_queue_read_op(BT_AUDIO_LE_MCC_OP_READ_MEDIA_STATE, s_mcc->conn_handle);
    }
}

static void bt_audio_le_mcc_track_changed_ntf(struct bt_conn *conn, int err)
{
    (void)conn;
    if (err) {
        ESP_LOGW(TAG, "MCC track changed notification failed, err %d", err);
        return;
    }
    if (!s_mcc) {
        return;
    }
    if (s_mcc->notify_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE) {
        bt_audio_le_mcc_dispatch_playback_status(ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE, 0);
    }
    bt_audio_le_mcc_queue_read_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_TITLE, s_mcc->conn_handle);
    bt_audio_le_mcc_queue_read_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_DURATION, s_mcc->conn_handle);
}

static void bt_audio_le_mcc_read_media_state_cb(struct bt_conn *conn, int err, uint8_t state)
{
    if (!s_mcc || !conn) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_MEDIA_STATE);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC media state, err %d", err);
    } else {
        if (s_mcc->notify_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE) {
            bt_audio_le_mcc_dispatch_playback_status(ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE,
                                                     bt_audio_le_mcc_media_state_to_playback_status(state));
        }
    }
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_MEDIA_STATE);
}

static void bt_audio_le_mcc_read_player_name_cb(struct bt_conn *conn, int err, const char *name)
{
    (void)name;

    if (!s_mcc || !conn) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_PLAYER_NAME);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC player name, err %d", err);
    }
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_PLAYER_NAME);
}

static void bt_audio_le_mcc_read_track_title_cb(struct bt_conn *conn, int err, const char *title)
{
    if (!s_mcc || !conn) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_TITLE);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC track title, err %d", err);
    } else {
        bt_audio_le_mcc_dispatch_metadata(ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE,
                                          (uint8_t *)title,
                                          title ? strlen(title) : 0);
    }
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_TITLE);
}

static void bt_audio_le_mcc_read_track_duration_cb(struct bt_conn *conn, int err, int32_t dur)
{
    if (!s_mcc || !conn) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_DURATION);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC track duration, err %d", err);
    } else {
        int len = snprintf(s_mcc->duration_str, sizeof(s_mcc->duration_str), "%ld", (long)dur);
        if (len < 0) {
            ESP_LOGE(TAG, "Read MCC track duration failed: format error");
            bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_DURATION);
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
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_DURATION);
}

static void bt_audio_le_mcc_read_track_position_cb(struct bt_conn *conn, int err, int32_t pos)
{
    if (!s_mcc || !conn) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_POSITION);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC track position, err %d", err);
    } else {
        if (s_mcc->notify_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED) {
            bt_audio_le_mcc_dispatch_playback_status(ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED, pos);
        }
    }
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_TRACK_POSITION);
}

static void bt_audio_le_mcc_read_playback_speed_cb(struct bt_conn *conn, int err, int8_t speed)
{
    if (!s_mcc || !conn) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_PLAYBACK_SPEED);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC playback speed, err %d", err);
    }
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_PLAYBACK_SPEED);
}

static void bt_audio_le_mcc_read_seeking_speed_cb(struct bt_conn *conn, int err, int8_t speed)
{
    if (!s_mcc || !conn) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_SEEKING_SPEED);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC seeking speed, err %d", err);
    }
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_SEEKING_SPEED);
}

static void bt_audio_le_mcc_read_playing_order_cb(struct bt_conn *conn, int err, uint8_t order)
{
    if (!s_mcc || !conn) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_PLAYING_ORDER);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC playing order, err %d", err);
    }
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_PLAYING_ORDER);
}

static void bt_audio_le_mcc_read_playing_orders_supported_cb(struct bt_conn *conn, int err, uint16_t orders)
{
    if (!s_mcc || !conn) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_PLAYING_ORDERS_SUPPORTED);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC playing orders supported, err %d", err);
    }
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_PLAYING_ORDERS_SUPPORTED);
}

static void bt_audio_le_mcc_opcodes_supported_cb(struct bt_conn *conn, int err, uint32_t opcodes)
{
    esp_bt_audio_playback_ops_t playback_ops = {0};

    if (!s_mcc || !conn) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_OPCODES_SUPPORTED);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC opcodes, err %d", err);
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_OPCODES_SUPPORTED);
        return;
    }

    bt_audio_ops_get_playback(&playback_ops);

    s_mcc->opcodes = opcodes;
    playback_ops.play = (opcodes & ESP_BLE_AUDIO_MCS_OPC_SUP_PLAY) ? bt_audio_le_mcc_play : NULL;
    playback_ops.pause = (opcodes & ESP_BLE_AUDIO_MCS_OPC_SUP_PAUSE) ? bt_audio_le_mcc_pause : NULL;
    playback_ops.stop = (opcodes & ESP_BLE_AUDIO_MCS_OPC_SUP_STOP) ? bt_audio_le_mcc_stop : NULL;
    playback_ops.next = (opcodes & ESP_BLE_AUDIO_MCS_OPC_SUP_NEXT_TRACK) ? bt_audio_le_mcc_next : NULL;
    playback_ops.prev = (opcodes & ESP_BLE_AUDIO_MCS_OPC_SUP_PREV_TRACK) ? bt_audio_le_mcc_prev : NULL;
    playback_ops.request_metadata = bt_audio_le_mcc_request_metadata;
    playback_ops.reg_notifications = bt_audio_le_mcc_reg_notifications;

    bt_audio_ops_set_playback(&playback_ops);

    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_OPCODES_SUPPORTED);
}

#if CONFIG_BT_MCC_OTS
static void bt_audio_le_mcc_dispatch_cover_art_object(const char *name, bt_audio_le_mcc_op_type_t type,
                                                      int err, struct net_buf_simple *buf)
{
    if (!s_mcc || !buf) {
        bt_audio_le_mcc_complete_op(type);
        return;
    }
    if (err) {
        if (err == -EMSGSIZE) {
            ESP_LOGW(TAG, "MCC %s object was truncated; skip cover art dispatch", name);
        } else {
            ESP_LOGE(TAG, "Failed to read MCC %s object, err %d", name, err);
        }
        bt_audio_le_mcc_complete_op(type);
        return;
    }

    uint32_t format_fourcc = 0;
    if (!bt_audio_le_mcc_detect_image_format(buf->data, buf->len, &format_fourcc)) {
        ESP_LOGW(TAG, "MCC %s object is not a supported image, size %u", name, buf->len);
        bt_audio_le_mcc_complete_op(type);
        return;
    }

    esp_bt_audio_playback_cover_art_t cover_art = {
        .format_fourcc = format_fourcc,
        .size = buf->len,
        .data = buf->data,
    };
    bt_audio_le_mcc_dispatch_metadata(ESP_BT_AUDIO_PLAYBACK_METADATA_COVER_ART,
                                      (uint8_t *)&cover_art,
                                      sizeof(cover_art));
    bt_audio_le_mcc_complete_op(type);
}

static void bt_audio_le_mcc_read_current_track_obj_id_cb(struct bt_conn *conn, int err, uint64_t id)
{
    if (!s_mcc || !conn) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_CURRENT_TRACK_OBJECT_ID);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC current track object ID, err %d", err);
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_CURRENT_TRACK_OBJECT_ID);
        return;
    }

    ESP_LOGI(TAG, "MCC current track object ID: 0x%012llx", (unsigned long long)id);
    if (!ESP_BLE_AUDIO_MCS_VALID_OBJ_ID(id)) {
        ESP_LOGW(TAG, "MCC current track object ID is invalid, skip cover art");
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_CURRENT_TRACK_OBJECT_ID);
        return;
    }
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_CURRENT_TRACK_OBJECT_ID);
    bt_audio_le_mcc_queue_read_op(BT_AUDIO_LE_MCC_OP_READ_CURRENT_TRACK_OBJECT, conn->handle);
}

static void bt_audio_le_mcc_otc_current_track_object(struct bt_conn *conn, int err, struct net_buf_simple *buf)
{
    (void)conn;
    bt_audio_le_mcc_dispatch_cover_art_object("current track", BT_AUDIO_LE_MCC_OP_READ_CURRENT_TRACK_OBJECT,
                                              err, buf);
}
#endif  /* CONFIG_BT_MCC_OTS */

static void bt_audio_le_mcc_content_control_id_cb(struct bt_conn *conn, int err, uint8_t ccid)
{
    if (!s_mcc) {
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_CONTENT_CONTROL_ID);
        return;
    }
    if (err) {
        ESP_LOGE(TAG, "Failed to read MCC content control ID, err %d", err);
        bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_CONTENT_CONTROL_ID);
        return;
    }
    s_mcc->content_control_id = ccid;
    bt_audio_le_mcc_complete_op(BT_AUDIO_LE_MCC_OP_READ_CONTENT_CONTROL_ID);
}

esp_err_t bt_audio_le_mcc_init(void)
{
    static esp_ble_audio_mcc_cb_t mcc_cbs;

    ESP_RETURN_ON_FALSE(!s_mcc, ESP_ERR_INVALID_STATE, TAG, "MCC already initialized");

    s_mcc = heap_caps_calloc_prefer(1, sizeof(*s_mcc), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(s_mcc, ESP_ERR_NO_MEM, TAG, "No memory for MCC");
    s_mcc->conn_handle = BT_AUDIO_LE_MCC_CONN_HANDLE_NONE;
    s_mcc->op_lock = xSemaphoreCreateMutex();
    if (!s_mcc->op_lock) {
        ESP_LOGE(TAG, "Create MCC operation lock failed");
        bt_audio_le_mcc_release_context(false);
        return ESP_ERR_NO_MEM;
    }

    esp_timer_create_args_t timer_args = {
        .callback = bt_audio_le_mcc_op_timer_cb,
        .name = "mcc_op_timer",
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_mcc->op_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Create MCC operation timer failed: %s", esp_err_to_name(ret));
        bt_audio_le_mcc_release_context(false);
        return ret;
    }

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
#if CONFIG_BT_MCC_OTS
    mcc_cbs.read_current_track_obj_id = bt_audio_le_mcc_read_current_track_obj_id_cb;
    mcc_cbs.otc_current_track_object = bt_audio_le_mcc_otc_current_track_object;
#endif  /* CONFIG_BT_MCC_OTS */

    ret = esp_ble_audio_mcc_init(&mcc_cbs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init MCC failed: %s", esp_err_to_name(ret));
        bt_audio_le_mcc_release_context(false);
        return ret;
    }

    esp_bt_audio_playback_ops_t playback_ops = {0};
    playback_ops.reg_notifications = bt_audio_le_mcc_reg_notifications;
    bt_audio_ops_set_playback(&playback_ops);
    return ESP_OK;
}

void bt_audio_le_mcc_deinit(void)
{
    if (!s_mcc) {
        return;
    }
    bt_audio_ops_set_playback(NULL);
    if (s_mcc->op_lock && xSemaphoreTake(s_mcc->op_lock, portMAX_DELAY) == pdTRUE) {
        bt_audio_le_mcc_clear_pending_ops();
        s_mcc->op_busy = false;
        xSemaphoreGive(s_mcc->op_lock);
    }
    bt_audio_le_mcc_release_context(true);
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
