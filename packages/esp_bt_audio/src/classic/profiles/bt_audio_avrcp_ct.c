/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <sys/queue.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#ifdef CONFIG_BT_AVRCP_ENABLED
#include "esp_avrc_api.h"
#endif  /* CONFIG_BT_AVRCP_ENABLED */

#include "esp_bt_audio_playback.h"
#include "esp_bt_audio_stream.h"
#include "bt_audio_avrcp.h"
#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_ops.h"

#define AVRCP_CT_KEY_DEFAULT_HOLD_MS  200

typedef struct avrcp_ct_key_evt {
    uint8_t                         key_state;
    uint8_t                         key_code;
    esp_timer_handle_t              timer;
    STAILQ_ENTRY(avrcp_ct_key_evt)  next;
} avrcp_ct_key_evt_t;

STAILQ_HEAD(avrcp_ct_key_evt_q, avrcp_ct_key_evt);

typedef struct {
    bool                        is_connected;
    esp_avrc_rn_evt_cap_mask_t  remote_capabilities;
    uint32_t                    rn_mask;
    uint8_t                     tl;
    struct avrcp_ct_key_evt_q   key_evt_q;
} avrcp_ct_ctx_t;

static const char *TAG = "BT_AUD_AVRC_CT";
static avrcp_ct_ctx_t *avrcp_ct = NULL;

static uint8_t convt_mask_to_avrcp_md_attr_mask(uint32_t mask)
{
    uint8_t attr_mask = 0;

    if (mask & ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE) {
        attr_mask |= ESP_AVRC_MD_ATTR_TITLE;
    }
    if (mask & ESP_BT_AUDIO_PLAYBACK_METADATA_ARTIST) {
        attr_mask |= ESP_AVRC_MD_ATTR_ARTIST;
    }
    if (mask & ESP_BT_AUDIO_PLAYBACK_METADATA_ALBUM) {
        attr_mask |= ESP_AVRC_MD_ATTR_ALBUM;
    }
    if (mask & ESP_BT_AUDIO_PLAYBACK_METADATA_TRACK_NUM) {
        attr_mask |= ESP_AVRC_MD_ATTR_TRACK_NUM;
    }
    if (mask & ESP_BT_AUDIO_PLAYBACK_METADATA_NUM_TRACKS) {
        attr_mask |= ESP_AVRC_MD_ATTR_NUM_TRACKS;
    }
    if (mask & ESP_BT_AUDIO_PLAYBACK_METADATA_GENRE) {
        attr_mask |= ESP_AVRC_MD_ATTR_GENRE;
    }
    if (mask & ESP_BT_AUDIO_PLAYBACK_METADATA_PLAYING_TIME) {
        attr_mask |= ESP_AVRC_MD_ATTR_PLAYING_TIME;
    }

    return attr_mask;
}

static uint32_t convt_avrcp_md_attr_to_mask(uint8_t attr_id)
{
    switch (attr_id) {
        case ESP_AVRC_MD_ATTR_TITLE:
            return ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE;
        case ESP_AVRC_MD_ATTR_ARTIST:
            return ESP_BT_AUDIO_PLAYBACK_METADATA_ARTIST;
        case ESP_AVRC_MD_ATTR_ALBUM:
            return ESP_BT_AUDIO_PLAYBACK_METADATA_ALBUM;
        case ESP_AVRC_MD_ATTR_TRACK_NUM:
            return ESP_BT_AUDIO_PLAYBACK_METADATA_TRACK_NUM;
        case ESP_AVRC_MD_ATTR_NUM_TRACKS:
            return ESP_BT_AUDIO_PLAYBACK_METADATA_NUM_TRACKS;
        case ESP_AVRC_MD_ATTR_GENRE:
            return ESP_BT_AUDIO_PLAYBACK_METADATA_GENRE;
        case ESP_AVRC_MD_ATTR_PLAYING_TIME:
            return ESP_BT_AUDIO_PLAYBACK_METADATA_PLAYING_TIME;
        default:
            ESP_LOGW(TAG, "CT metadata rsp evt: unsupported attr_id %u", attr_id);
            return 0;
    }
}

static inline uint8_t vol_from_avrc(uint8_t avrcp_vol)
{
    return (uint8_t)((avrcp_vol * 100) / 127);
}

static inline uint8_t vol_to_avrc(uint8_t app_vol)
{
    return (uint8_t)((app_vol * 127) / 100);
}

static uint8_t avrcp_ct_tl_acquire()
{
    if (avrcp_ct != NULL) {
        return (avrcp_ct->tl++) & 0x0F;
    } else {
        ESP_LOGW(TAG, "CT not initialized");
        return 0;
    }
}

static void avrcp_ct_key_timer_cb(void *arg)
{
    avrcp_ct_key_evt_t *evt = (avrcp_ct_key_evt_t *)arg;
    if (evt->key_state == ESP_AVRC_PT_CMD_STATE_PRESSED) {
        esp_avrc_ct_send_passthrough_cmd(avrcp_ct_tl_acquire(), evt->key_code, ESP_AVRC_PT_CMD_STATE_RELEASED);
    }
    esp_timer_delete(evt->timer);
    STAILQ_REMOVE(&avrcp_ct->key_evt_q, evt, avrcp_ct_key_evt, next);
    free(evt);
    evt = NULL;
}

static esp_err_t avrcp_ct_key_press(uint32_t key_code, uint32_t last_ms)
{
    if (!avrcp_ct || !avrcp_ct->is_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    avrcp_ct_key_evt_t *evt = heap_caps_calloc_prefer(1, sizeof(avrcp_ct_key_evt_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (!evt) {
        ESP_LOGW(TAG, "Failed to malloc space for avrcp_ct_key_evt_t");
        return ESP_ERR_NO_MEM;
    }
    evt->key_code = key_code;
    evt->key_state = ESP_AVRC_PT_CMD_STATE_PRESSED;
    esp_timer_create_args_t timer_args = {
        .callback = avrcp_ct_key_timer_cb,
        .arg = evt,
        .name = "avrcp_ct_key_timer"};
    esp_timer_create(&timer_args, &evt->timer);
    if (!evt->timer) {
        ESP_LOGW(TAG, "Failed to create timer");
        free(evt);
        return ESP_ERR_NO_MEM;
    }
    STAILQ_INSERT_TAIL(&avrcp_ct->key_evt_q, evt, next);
    esp_avrc_ct_send_passthrough_cmd(avrcp_ct_tl_acquire(), evt->key_code, ESP_AVRC_PT_CMD_STATE_PRESSED);
    esp_timer_start_once(evt->timer, last_ms * 1000);
    return ESP_OK;
}

static esp_err_t avrcp_ct_play()
{
    ESP_LOGI(TAG, "CT: Play command");
    if (!avrcp_ct || !avrcp_ct->is_connected) {
        ESP_LOGW(TAG, "CT not connected");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = avrcp_ct_key_press(ESP_AVRC_PT_CMD_PLAY, AVRCP_CT_KEY_DEFAULT_HOLD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send play command");
    }
    return ret;
}

static esp_err_t avrcp_ct_pause()
{
    ESP_LOGI(TAG, "CT: Pause command");
    if (!avrcp_ct || !avrcp_ct->is_connected) {
        ESP_LOGW(TAG, "CT not connected");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = avrcp_ct_key_press(ESP_AVRC_PT_CMD_PAUSE, AVRCP_CT_KEY_DEFAULT_HOLD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send pause command");
    }
    return ret;
}

static esp_err_t avrcp_ct_stop()
{
    ESP_LOGI(TAG, "CT: Stop command");
    if (!avrcp_ct || !avrcp_ct->is_connected) {
        ESP_LOGW(TAG, "CT not connected");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = avrcp_ct_key_press(ESP_AVRC_PT_CMD_STOP, AVRCP_CT_KEY_DEFAULT_HOLD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send stop command");
    }
    return ret;
}

static esp_err_t avrcp_ct_next()
{
    ESP_LOGI(TAG, "CT: Next command");
    if (!avrcp_ct || !avrcp_ct->is_connected) {
        ESP_LOGW(TAG, "CT not connected");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = avrcp_ct_key_press(ESP_AVRC_PT_CMD_FORWARD, AVRCP_CT_KEY_DEFAULT_HOLD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send forward command");
    }
    return ret;
}

static esp_err_t avrcp_ct_prev()
{
    ESP_LOGI(TAG, "CT: Previous command");
    if (!avrcp_ct || !avrcp_ct->is_connected) {
        ESP_LOGW(TAG, "CT not connected");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = avrcp_ct_key_press(ESP_AVRC_PT_CMD_BACKWARD, AVRCP_CT_KEY_DEFAULT_HOLD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send backward command");
    }
    return ret;
}

static esp_err_t avrcp_ct_request_metadata(uint32_t mask)
{
    ESP_LOGI(TAG, "CT: Request metadata");
    if (!avrcp_ct || !avrcp_ct->is_connected) {
        ESP_LOGW(TAG, "CT not connected");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t attr_mask = convt_mask_to_avrcp_md_attr_mask(mask);
    return esp_avrc_ct_send_metadata_cmd(avrcp_ct_tl_acquire(), attr_mask);
}

static esp_err_t avrcp_ct_do_register_rn_event(esp_avrc_rn_evt_cap_mask_t *remote_capabilities, uint32_t mask)
{
    if (remote_capabilities && remote_capabilities->bits) {
        if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE) {
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, remote_capabilities, ESP_AVRC_RN_PLAY_STATUS_CHANGE)) {
                ESP_LOGD(TAG, "CT: register PLAY_STATUS_CHANGE notification");
                esp_avrc_ct_send_register_notification_cmd(avrcp_ct_tl_acquire(), ESP_AVRC_RN_PLAY_STATUS_CHANGE, 0);
            }
        }
        if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE) {
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, remote_capabilities, ESP_AVRC_RN_TRACK_CHANGE)) {
                ESP_LOGD(TAG, "CT: register TRACK_CHANGE notification");
                esp_avrc_ct_send_register_notification_cmd(avrcp_ct_tl_acquire(), ESP_AVRC_RN_TRACK_CHANGE, 0);
            }
        }
        if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_END) {
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, remote_capabilities, ESP_AVRC_RN_TRACK_REACHED_END)) {
                ESP_LOGD(TAG, "CT: register TRACK_REACHED_END notification");
                esp_avrc_ct_send_register_notification_cmd(avrcp_ct_tl_acquire(), ESP_AVRC_RN_TRACK_REACHED_END, 0);
            }
        }
        if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_START) {
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, remote_capabilities, ESP_AVRC_RN_TRACK_REACHED_START)) {
                ESP_LOGD(TAG, "CT: register TRACK_REACHED_START notification");
                esp_avrc_ct_send_register_notification_cmd(avrcp_ct_tl_acquire(), ESP_AVRC_RN_TRACK_REACHED_START, 0);
            }
        }
        if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED) {
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, remote_capabilities, ESP_AVRC_RN_PLAY_POS_CHANGED)) {
                ESP_LOGD(TAG, "CT: register PLAY_POS_CHANGED notification");
                esp_avrc_ct_send_register_notification_cmd(avrcp_ct_tl_acquire(), ESP_AVRC_RN_PLAY_POS_CHANGED, 0);
            }
        }
        if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_NOW_PLAYING_CHANGE) {
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, remote_capabilities, ESP_AVRC_RN_NOW_PLAYING_CHANGE)) {
                ESP_LOGD(TAG, "CT: register NOW_PLAYING_CHANGE notification");
                esp_avrc_ct_send_register_notification_cmd(avrcp_ct_tl_acquire(), ESP_AVRC_RN_NOW_PLAYING_CHANGE, 0);
            }
        }
        if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_AVAILABLE_PLAYERS_CHANGE) {
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, remote_capabilities, ESP_AVRC_RN_AVAILABLE_PLAYERS_CHANGE)) {
                ESP_LOGD(TAG, "CT: register AVAILABLE_PLAYERS_CHANGE notification");
                esp_avrc_ct_send_register_notification_cmd(avrcp_ct_tl_acquire(), ESP_AVRC_RN_AVAILABLE_PLAYERS_CHANGE, 0);
            }
        }
        if (mask & ESP_BT_AUDIO_PLAYBACK_EVENT_ADDRESSED_PLAYER_CHANGE) {
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, remote_capabilities, ESP_AVRC_RN_ADDRESSED_PLAYER_CHANGE)) {
                ESP_LOGD(TAG, "CT: register ADDRESSED_PLAYER_CHANGE notification");
                esp_avrc_ct_send_register_notification_cmd(avrcp_ct_tl_acquire(), ESP_AVRC_RN_ADDRESSED_PLAYER_CHANGE, 0);
            }
        }
    }
    return ESP_OK;
}

static esp_err_t avrcp_ct_register_notifications(uint32_t mask)
{
    ESP_LOGI(TAG, "CT: Register notifications mask 0x%x", mask);
    if (!avrcp_ct) {
        ESP_LOGW(TAG, "CT not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    avrcp_ct->rn_mask = mask;
    return avrcp_ct_do_register_rn_event(&avrcp_ct->remote_capabilities, avrcp_ct->rn_mask);
}

static esp_err_t avrcp_ct_set_absolute_volume(uint32_t vol)
{
    ESP_LOGI(TAG, "CT: Set volume to %d (app range 0-100)", (int)vol);
    if (!avrcp_ct || !avrcp_ct->is_connected) {
        ESP_LOGW(TAG, "CT not connected");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t avrcp_vol = vol_to_avrc(vol);
    ESP_LOGI(TAG, "CT: Converted volume %d -> %d (AVRCP range 0-127)", (int)vol, avrcp_vol);
    return esp_avrc_ct_send_set_absolute_volume_cmd(avrcp_ct_tl_acquire(), avrcp_vol);
}

static esp_err_t avrcp_ct_set_relative_volume(bool up_down)
{
    ESP_LOGI(TAG, "CT: Set relative volume to %s", up_down ? "up" : "down");
    if (!avrcp_ct || !avrcp_ct->is_connected) {
        ESP_LOGW(TAG, "CT not connected");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = avrcp_ct_key_press(up_down ? ESP_AVRC_PT_CMD_VOL_UP : ESP_AVRC_PT_CMD_VOL_DOWN, AVRCP_CT_KEY_DEFAULT_HOLD_MS);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to send relative volume command");
    }
    return ret;
}

static void avrcp_ct_handle_rn_event(esp_avrc_rn_event_ids_t event, esp_avrc_rn_param_t *param)
{
    switch (event) {
        case ESP_AVRC_RN_VOLUME_CHANGE: {
            ESP_LOGD(TAG, "CT volume change notify evt: volume %d", param->volume);
            esp_bt_audio_event_vol_absolute_t event_data = {0};
            event_data.context = ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA;
            event_data.vol = vol_from_avrc(param->volume);
            event_data.mute = false;
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_VOL_ABSOLUTE, &event_data);
            esp_avrc_ct_send_register_notification_cmd(avrcp_ct_tl_acquire(), ESP_AVRC_RN_VOLUME_CHANGE, 0);
            break;
        }
        case ESP_AVRC_RN_PLAY_STATUS_CHANGE: {
            ESP_LOGD(TAG, "CT play status change notify evt: play_status %d", param->playback);
            esp_bt_audio_event_playback_st_t event_data = {0};
            event_data.event = ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE;
            event_data.evt_param.play_status = (uint32_t)param->playback;
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_PLAYBACK_STATUS_CHG, &event_data);
            if (avrcp_ct && (avrcp_ct->rn_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE)) {
                avrcp_ct_do_register_rn_event(&avrcp_ct->remote_capabilities, ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE);
            }
            break;
        }
        case ESP_AVRC_RN_TRACK_CHANGE: {
            ESP_LOGD(TAG, "CT track change notify evt: track");
            esp_bt_audio_event_playback_st_t event_data = {0};
            event_data.event = ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE;
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_PLAYBACK_STATUS_CHG, &event_data);
            if (avrcp_ct && (avrcp_ct->rn_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE)) {
                avrcp_ct_do_register_rn_event(&avrcp_ct->remote_capabilities, ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE);
            }
            break;
        }
        case ESP_AVRC_RN_TRACK_REACHED_END: {
            ESP_LOGD(TAG, "CT track reached end notify evt: track");
            esp_bt_audio_event_playback_st_t event_data = {0};
            event_data.event = ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_END;
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_PLAYBACK_STATUS_CHG, &event_data);
            if (avrcp_ct && (avrcp_ct->rn_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_END)) {
                avrcp_ct_do_register_rn_event(&avrcp_ct->remote_capabilities, ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_END);
            }
            break;
        }
        case ESP_AVRC_RN_TRACK_REACHED_START: {
            ESP_LOGD(TAG, "CT track reached start notify evt: track");
            esp_bt_audio_event_playback_st_t event_data = {0};
            event_data.event = ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_START;
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_PLAYBACK_STATUS_CHG, &event_data);
            if (avrcp_ct && (avrcp_ct->rn_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_START)) {
                avrcp_ct_do_register_rn_event(&avrcp_ct->remote_capabilities, ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_START);
            }
            break;
        }
        case ESP_AVRC_RN_PLAY_POS_CHANGED: {
            ESP_LOGD(TAG, "CT play pos changed notify evt: play_pos");
            esp_bt_audio_event_playback_st_t event_data = {0};
            event_data.event = ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED;
            event_data.evt_param.position = param->play_pos;
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_PLAYBACK_STATUS_CHG, &event_data);
            if (avrcp_ct && (avrcp_ct->rn_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED)) {
                avrcp_ct_do_register_rn_event(&avrcp_ct->remote_capabilities, ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED);
            }
            break;
        }
        case ESP_AVRC_RN_NOW_PLAYING_CHANGE: {
            ESP_LOGD(TAG, "CT now playing change notify evt: now_playing");
            esp_bt_audio_event_playback_st_t event_data = {0};
            event_data.event = ESP_BT_AUDIO_PLAYBACK_EVENT_NOW_PLAYING_CHANGE;
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_PLAYBACK_STATUS_CHG, &event_data);
            if (avrcp_ct && (avrcp_ct->rn_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_NOW_PLAYING_CHANGE)) {
                avrcp_ct_do_register_rn_event(&avrcp_ct->remote_capabilities, ESP_BT_AUDIO_PLAYBACK_EVENT_NOW_PLAYING_CHANGE);
            }
            break;
        }
        case ESP_AVRC_RN_AVAILABLE_PLAYERS_CHANGE: {
            ESP_LOGD(TAG, "CT available players change notify evt: available_players");
            esp_bt_audio_event_playback_st_t event_data = {0};
            event_data.event = ESP_BT_AUDIO_PLAYBACK_EVENT_AVAILABLE_PLAYERS_CHANGE;
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_PLAYBACK_STATUS_CHG, &event_data);
            if (avrcp_ct && (avrcp_ct->rn_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_AVAILABLE_PLAYERS_CHANGE)) {
                avrcp_ct_do_register_rn_event(&avrcp_ct->remote_capabilities, ESP_BT_AUDIO_PLAYBACK_EVENT_AVAILABLE_PLAYERS_CHANGE);
            }
            break;
        }
        case ESP_AVRC_RN_ADDRESSED_PLAYER_CHANGE: {
            ESP_LOGD(TAG, "CT addressed player change notify evt: addressed_player");
            esp_bt_audio_event_playback_st_t event_data = {0};
            event_data.event = ESP_BT_AUDIO_PLAYBACK_EVENT_ADDRESSED_PLAYER_CHANGE;
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_PLAYBACK_STATUS_CHG, &event_data);
            if (avrcp_ct && (avrcp_ct->rn_mask & ESP_BT_AUDIO_PLAYBACK_EVENT_ADDRESSED_PLAYER_CHANGE)) {
                avrcp_ct_do_register_rn_event(&avrcp_ct->remote_capabilities, ESP_BT_AUDIO_PLAYBACK_EVENT_ADDRESSED_PLAYER_CHANGE);
            }
            break;
        }
        default: {
            ESP_LOGI(TAG, "CT: Unhandled remote notification event: %d", event);
            break;
        }
    }
}

static void bt_app_rc_ct_cb(esp_avrc_ct_cb_event_t event, esp_avrc_ct_cb_param_t *param)
{
    ESP_LOGD(TAG, "%s event: %d", __func__, event);
    esp_avrc_ct_cb_param_t *rc = (esp_avrc_ct_cb_param_t *)(param);

    switch (event) {
        case ESP_AVRC_CT_CONNECTION_STATE_EVT: {
            ESP_LOGI(TAG, "CT conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                     rc->conn_stat.connected, rc->conn_stat.remote_bda[0], rc->conn_stat.remote_bda[1], rc->conn_stat.remote_bda[2], rc->conn_stat.remote_bda[3], rc->conn_stat.remote_bda[4], rc->conn_stat.remote_bda[5]);

            if (avrcp_ct) {
                avrcp_ct->is_connected = rc->conn_stat.connected;
                esp_bt_audio_vol_ops_t vol_ops = {0};
                bt_audio_ops_get_vol(&vol_ops);
                esp_bt_audio_playback_ops_t playback_ops = {0};
                bt_audio_ops_get_playback(&playback_ops);
                if (rc->conn_stat.connected) {
                    esp_avrc_ct_send_get_rn_capabilities_cmd(avrcp_ct_tl_acquire());
                    playback_ops.play = avrcp_ct_play;
                    playback_ops.pause = avrcp_ct_pause;
                    playback_ops.stop = avrcp_ct_stop;
                    playback_ops.next = avrcp_ct_next;
                    playback_ops.prev = avrcp_ct_prev;
                    playback_ops.request_metadata = avrcp_ct_request_metadata;

                    vol_ops.set_absolute = avrcp_ct_set_absolute_volume;
                    vol_ops.set_relative = avrcp_ct_set_relative_volume;
                } else {
                    playback_ops.play = NULL;
                    playback_ops.pause = NULL;
                    playback_ops.stop = NULL;
                    playback_ops.next = NULL;
                    playback_ops.prev = NULL;
                    playback_ops.request_metadata = NULL;

                    vol_ops.set_absolute = NULL;
                    vol_ops.set_relative = NULL;
                }
                bt_audio_ops_set_vol(&vol_ops);
                bt_audio_ops_set_playback(&playback_ops);
            }
            break;
        }
        case ESP_AVRC_CT_PASSTHROUGH_RSP_EVT: {
            ESP_LOGI(TAG, "CT passthrough rsp evt: key_code 0x%x, key_state %d", rc->psth_rsp.key_code, rc->psth_rsp.key_state);
            break;
        }
        case ESP_AVRC_CT_METADATA_RSP_EVT: {
            ESP_LOGD(TAG, "CT metadata rsp evt: metadata: %d, length %d, text %s",
                     rc->meta_rsp.attr_id,
                     rc->meta_rsp.attr_length,
                     rc->meta_rsp.attr_length > 0 && rc->meta_rsp.attr_text ? (const char *)rc->meta_rsp.attr_text : "");
            esp_bt_audio_event_playback_metadata_t event_data = {0};
            event_data.type = convt_avrcp_md_attr_to_mask(rc->meta_rsp.attr_id);
            event_data.length = rc->meta_rsp.attr_length;
            event_data.value = rc->meta_rsp.attr_text;
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_PLAYBACK_METADATA, &event_data);
            break;
        }
        case ESP_AVRC_CT_PLAY_STATUS_RSP_EVT: {
            ESP_LOGI(TAG, "CT play status rsp evt: play_status %d", rc->play_status_rsp.play_status);
            break;
        }
        case ESP_AVRC_CT_CHANGE_NOTIFY_EVT: {
            ESP_LOGI(TAG, "CT change notify evt: change_notify %d", rc->change_ntf.event_id);
            avrcp_ct_handle_rn_event(rc->change_ntf.event_id, &rc->change_ntf.event_parameter);
            break;
        }
        case ESP_AVRC_CT_REMOTE_FEATURES_EVT: {
            ESP_LOGI(TAG, "CT remote features evt: remote_features %d", rc->rmt_feats.feat_mask);
            break;
        }
        case ESP_AVRC_CT_GET_RN_CAPABILITIES_RSP_EVT: {
            ESP_LOGI(TAG, "CT get rn capabilities rsp evt: count %d, bitmask 0x%x", rc->get_rn_caps_rsp.cap_count, rc->get_rn_caps_rsp.evt_set.bits);
            memcpy(&avrcp_ct->remote_capabilities, &rc->get_rn_caps_rsp.evt_set, sizeof(esp_avrc_rn_evt_cap_mask_t));
            if (esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_TEST, &rc->get_rn_caps_rsp.evt_set, ESP_AVRC_RN_VOLUME_CHANGE)) {
                ESP_LOGI(TAG, "CT: register volume change notification");
                esp_avrc_ct_send_register_notification_cmd(avrcp_ct_tl_acquire(), ESP_AVRC_RN_VOLUME_CHANGE, 0);
            }
            avrcp_ct_do_register_rn_event(&avrcp_ct->remote_capabilities, avrcp_ct->rn_mask);
            break;
        }
        case ESP_AVRC_CT_SET_ABSOLUTE_VOLUME_RSP_EVT: {
            ESP_LOGI(TAG, "CT set absolute volume rsp evt: volume %d", rc->set_volume_rsp.volume);
            break;
        }
        case ESP_AVRC_CT_COVER_ART_STATE_EVT: {
            ESP_LOGI(TAG, "CT cover art state evt: cover_art_state %d", rc->cover_art_state.state);
            break;
        }
        case ESP_AVRC_CT_COVER_ART_DATA_EVT: {
            ESP_LOGI(TAG, "CT cover art data evt: cover_art_data %s", rc->cover_art_data.p_data);
            break;
        }
        case ESP_AVRC_CT_PROF_STATE_EVT: {
            ESP_LOGI(TAG, "CT prof state evt: prof_state %d", rc->avrc_ct_init_stat.state);
            break;
        }
        default: {
            ESP_LOGI(TAG, "CT unhandled event: %d", event);
            break;
        }
    }
}

esp_err_t bt_audio_avrcp_ct_init()
{
    if (avrcp_ct) {
        return ESP_ERR_INVALID_STATE;
    }
    avrcp_ct = heap_caps_calloc_prefer(1, sizeof(avrcp_ct_ctx_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(avrcp_ct, ESP_ERR_NO_MEM, TAG, "Failed to malloc space for avrcp_ct_ctx");
    STAILQ_INIT(&avrcp_ct->key_evt_q);
    ESP_RETURN_ON_ERROR(esp_avrc_ct_init(), TAG, "esp_avrc_ct_init failed");
    ESP_RETURN_ON_ERROR(esp_avrc_ct_register_callback(bt_app_rc_ct_cb), TAG, "esp_avrc_ct_register_callback failed");
    esp_bt_audio_playback_ops_t playback_ops = {0};
    bt_audio_ops_get_playback(&playback_ops);
    playback_ops.reg_notifications = avrcp_ct_register_notifications;
    bt_audio_ops_set_playback(&playback_ops);
    ESP_LOGI(TAG, "CT init success");

    return ESP_OK;
}

esp_err_t bt_audio_avrcp_ct_deinit()
{
    if (!avrcp_ct) {
        return ESP_ERR_INVALID_STATE;
    }
    avrcp_ct_key_evt_t *evt;
    while ((evt = STAILQ_FIRST(&avrcp_ct->key_evt_q)) != NULL) {
        if (evt->timer) {
            esp_timer_stop(evt->timer);
            esp_timer_delete(evt->timer);
        }
        STAILQ_REMOVE_HEAD(&avrcp_ct->key_evt_q, next);
        free(evt);
        evt = NULL;
    }
    free(avrcp_ct);
    avrcp_ct = NULL;
    esp_avrc_ct_deinit();
    ESP_LOGI(TAG, "CT deinit success");
    return ESP_OK;
}
