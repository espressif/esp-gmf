/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#if CONFIG_BT_ENABLED
#include "esp_bt.h"
#endif  /* CONFIG_BT_ENABLED */

#include "nvs_flash.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_host.h"
#include "esp_gmf_pool.h"
#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
#include "esp_ble_audio_defs.h"
#include "esp_ble_audio_tmap_api.h"
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */

#include "esp_codec_dev.h"
#include "esp_codec_dev_types.h"
#include "esp_bt_audio_media.h"
#include "esp_bt_audio_playback.h"
#include "esp_bt_audio_tel.h"
#include "esp_bt_audio_classic.h"
#include "esp_bt_audio_le.h"
#include "esp_bt_audio_pb.h"
#include "esp_bt_audio.h"

#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "dev_audio_codec.h"
#if CONFIG_EXAMPLE_BT_UI_ENABLE
#include "bt_ui.h"
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */

#include "cmd_reg.h"
#include "pool_reg.h"
#include "stream_proc.h"
#include "codec_defs.h"

#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
#define A2DP_SRC_SEND_TASK_CORE_ID     1
#define A2DP_SRC_SEND_TASK_PRIO        10
#define A2DP_SRC_SEND_TASK_STACK_SIZE  4096
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */

#define PHONEBOOK_ENTRY_LOG_BUF_SIZE  512
#define VOLUME_CTRL_QUEUE_SIZE        8
#define VOLUME_CTRL_TASK_STACK_SIZE   3072
#define VOLUME_CTRL_TASK_PRIO         5
#define BT_UI_DEVICE_NAME             "GMF_BT_UI"

static const char *TAG = "BT_AUD_EXAMPLE";
static const char *media_ctrl_cmd_str[] = {
    "UNKNOWN",
    "PLAY",
    "PAUSE",
    "STOP",
    "NEXT",
    "PREV",
};
static const char *playback_metadata_type_str[] = {
    "TITLE",
    "ARTIST",
    "ALBUM",
    "TRACK_NUM",
    "NUM_TRACKS",
    "GENRE",
    "PLAYING_TIME",
    "COVER_ART",
};

static const char *call_state_str[] = {
    "INACTIVE",
    "INCOMING",
    "DIALING",
    "ALERTING",
    "ACTIVE",
    "LOCALLY_HELD",
    "REMOTELY_HELD",
    "LOCALLY_AND_REMOTELY_HELD",
    "UNKNOWN",
};

static const char *tel_event_str[] = {
    "CALL_STATE",
    "BATTERY",
    "SIGNAL_STRENGTH",
    "ROAMING",
    "NETWORK",
    "OPERATOR",
    "UNKNOWN",
};

static esp_gmf_pool_handle_t pool = NULL;
static QueueHandle_t volume_ctrl_queue = NULL;

typedef enum {
    VOLUME_CTRL_CMD_ABSOLUTE,
    VOLUME_CTRL_CMD_RELATIVE,
} volume_ctrl_cmd_type_t;

typedef struct {
    volume_ctrl_cmd_type_t         type;
    uint8_t                        vol;
    bool                           mute;
    bool                           up_down;
    esp_bt_audio_stream_context_t  context;
} volume_ctrl_cmd_t;

#if CONFIG_EXAMPLE_BT_UI_ENABLE
static bt_ui_t *ui = NULL;
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */

static inline const char *media_ctrl_cmd_to_str(esp_bt_audio_media_ctrl_cmd_t cmd)
{
    return media_ctrl_cmd_str[cmd];
}

static inline const char *playback_metadata_type_to_str(uint32_t type)
{
    return playback_metadata_type_str[__builtin_ctz(type)];
}

static inline const char *call_state_to_str(esp_bt_audio_call_state_t state)
{
    unsigned i = (unsigned)state;
    return call_state_str[i < sizeof(call_state_str) / sizeof(call_state_str[0]) ? i : (sizeof(call_state_str) / sizeof(call_state_str[0]) - 1)];
}

static inline const char *tel_event_to_str(esp_bt_audio_tel_event_t type)
{
    unsigned i = (unsigned)type;
    return tel_event_str[i < sizeof(tel_event_str) / sizeof(tel_event_str[0]) ? i : (sizeof(tel_event_str) / sizeof(tel_event_str[0]) - 1)];
}

static bool volume_ctrl_post_cmd(const volume_ctrl_cmd_t *cmd)
{
    if (volume_ctrl_queue == NULL) {
        ESP_LOGE(TAG, "Volume control task is not initialized");
        return false;
    }
    if (xQueueSend(volume_ctrl_queue, cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Volume control command queue full, drop action %d", cmd->type);
        return false;
    }
    return true;
}

static esp_err_t volume_ctrl_get_codec(dev_audio_codec_handles_t **codec_handle)
{
    esp_err_t ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)codec_handle);
    if (ret != ESP_OK || *codec_handle == NULL || (*codec_handle)->codec_dev == NULL) {
        ESP_LOGE(TAG, "Failed to get audio DAC handle: %s", esp_err_to_name(ret));
        return ret == ESP_OK ? ESP_ERR_INVALID_STATE : ret;
    }
    return ESP_OK;
}

static void volume_ctrl_task(void *arg)
{
    (void)arg;
    volume_ctrl_cmd_t cmd = {0};

    while (true) {
        if (xQueueReceive(volume_ctrl_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        dev_audio_codec_handles_t *codec_handle = NULL;
        if (volume_ctrl_get_codec(&codec_handle) != ESP_OK) {
            continue;
        }

        switch (cmd.type) {
            case VOLUME_CTRL_CMD_ABSOLUTE:
                ESP_LOGI(TAG, "Set absolute volume: vol %d, mute %d, context %d", cmd.vol, cmd.mute, cmd.context);
                esp_codec_dev_set_out_vol(codec_handle->codec_dev, cmd.vol);
                break;
            case VOLUME_CTRL_CMD_RELATIVE: {
                int current_volume = 0;
                esp_codec_dev_get_out_vol(codec_handle->codec_dev, &current_volume);
                current_volume = cmd.up_down ?
                                 ((current_volume >= 90) ? 100 : current_volume + 10) :
                                 ((current_volume <= 10) ? 0 : current_volume - 10);
                esp_codec_dev_set_out_vol(codec_handle->codec_dev, current_volume);
                ESP_LOGI(TAG, "Set relative volume: up_down %d, context %d, volume %d",
                         cmd.up_down, cmd.context, current_volume);
                break;
            }
            default:
                ESP_LOGW(TAG, "Unknown volume control action %d", cmd.type);
                break;
        }
    }
}

static void setup_volume_ctrl_task(void)
{
    if (volume_ctrl_queue) {
        return;
    }

    volume_ctrl_queue = xQueueCreate(VOLUME_CTRL_QUEUE_SIZE, sizeof(volume_ctrl_cmd_t));
    if (volume_ctrl_queue == NULL) {
        ESP_LOGE(TAG, "Create volume control command queue failed");
        return;
    }

    BaseType_t ret = xTaskCreate(volume_ctrl_task, "volume_ctrl_task", VOLUME_CTRL_TASK_STACK_SIZE, NULL,
                                 VOLUME_CTRL_TASK_PRIO, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Create volume control task failed");
        vQueueDelete(volume_ctrl_queue);
        volume_ctrl_queue = NULL;
    }
}

#if CONFIG_EXAMPLE_BT_UI_ENABLE
/* --------------------------------------------------------------------- */
/*  BT Audio callbacks                                                     */
/* --------------------------------------------------------------------- */

static void on_dial_cb(const char *number, void *ctx)
{
    (void)ctx;
    if (number == NULL || number[0] == '\0') {
        return;
    }
    esp_err_t ret = esp_bt_audio_call_dial(number);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Dial failed: %s", esp_err_to_name(ret));
    }
}

static void on_end_call_cb(void *ctx)
{
    (void)ctx;
    esp_err_t ret = esp_bt_audio_call_reject(0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "End call failed: %s", esp_err_to_name(ret));
    }
}

static void on_answer_call_cb(void *ctx)
{
    (void)ctx;
    esp_err_t ret = esp_bt_audio_call_answer(0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Answer call failed: %s", esp_err_to_name(ret));
    }
}

static void on_play_pause_cb(bool want_play, void *ctx)
{
    (void)ctx;
    esp_err_t ret = want_play ? esp_bt_audio_playback_play() : esp_bt_audio_playback_pause();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback %s failed: %s", want_play ? "play" : "pause", esp_err_to_name(ret));
    }
}

static void on_prev_cb(void *ctx)
{
    (void)ctx;
    esp_err_t ret = esp_bt_audio_playback_prev();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback prev failed: %s", esp_err_to_name(ret));
    }
}

static void on_next_cb(void *ctx)
{
    (void)ctx;
    esp_err_t ret = esp_bt_audio_playback_next();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback next failed: %s", esp_err_to_name(ret));
    }
}
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */

#if CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_SIZE <= 1
static esp_err_t append_mac_suffix_to_device_name(char *device_name, size_t device_name_size)
{
    uint8_t mac[6] = {0};
    char default_name[ESP_BT_AUDIO_HOST_MAX_DEV_NAME_LEN] = {0};

    int written = snprintf(default_name, sizeof(default_name), "%s", device_name);
    if (written < 0 || (size_t)written >= sizeof(default_name)) {
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = esp_read_mac(mac, ESP_MAC_BT);
    if (ret != ESP_OK) {
        return ret;
    }

    written = snprintf(device_name, device_name_size, "%s_%02X%02X", default_name, mac[4], mac[5]);
    if (written < 0 || (size_t)written >= device_name_size) {
        return ESP_ERR_INVALID_SIZE;
    }
    return ESP_OK;
}
#endif  /* CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_SIZE <= 1 */

#if CONFIG_BT_CLASSIC_ENABLED && CONFIG_GMF_EXAMPLE_AUDIO_TECH_CLASSIC
static uint32_t get_classic_roles()
{
    uint32_t roles = 0;
#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
    roles |= ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC;
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
#ifdef CONFIG_GMF_EXAMPLE_A2DP_SINK
    roles |= ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SNK;
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SINK */
#ifdef CONFIG_GMF_EXAMPLE_HFP_HF
    roles |= ESP_BT_AUDIO_CLASSIC_ROLE_HFP_HF;
#endif  /* CONFIG_GMF_EXAMPLE_HFP_HF */
#ifdef CONFIG_GMF_EXAMPLE_HFP_AG
    roles |= ESP_BT_AUDIO_CLASSIC_ROLE_HFP_AG;
#endif  /* CONFIG_GMF_EXAMPLE_HFP_AG */
#ifdef CONFIG_GMF_EXAMPLE_AVRC_CT
    roles |= ESP_BT_AUDIO_CLASSIC_ROLE_AVRC_CT;
#endif  /* CONFIG_GMF_EXAMPLE_AVRC_CT */
#ifdef CONFIG_GMF_EXAMPLE_AVRC_TG
    roles |= ESP_BT_AUDIO_CLASSIC_ROLE_AVRC_TG;
#endif  /* CONFIG_GMF_EXAMPLE_AVRC_TG */
#ifdef CONFIG_GMF_EXAMPLE_PBAP_PCE
    roles |= ESP_BT_AUDIO_CLASSIC_ROLE_PBAP_PCE;
#endif  /* CONFIG_GMF_EXAMPLE_PBAP_PCE */
    return roles;
}
#endif  /* CONFIG_BT_CLASSIC_ENABLED && CONFIG_GMF_EXAMPLE_AUDIO_TECH_CLASSIC */

#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
#if CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_LEFT
#define LE_LOCATION  ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT
#elif CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_RIGHT
#define LE_LOCATION  ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT
#endif  /* CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_LEFT || CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_RIGHT */

#if CONFIG_GMF_EXAMPLE_LE_SOURCE_ENABLE
#define LE_SOURCE_COUNT         1
#define LE_SOURCE_ENABLED       1
#define LE_SOURCE_CONTEXT_MASK  ESP_BLE_AUDIO_CONTEXT_TYPE_ANY
#define LE_SOURCE_LOCATION      LE_LOCATION
#else
#define LE_SOURCE_COUNT         0
#define LE_SOURCE_ENABLED       0
#define LE_SOURCE_CONTEXT_MASK  0
#define LE_SOURCE_LOCATION      0
#endif  /* CONFIG_GMF_EXAMPLE_LE_SOURCE_ENABLE */

static uint32_t get_le_roles(void)
{
    uint32_t roles = 0;
#if CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_CT
    roles |= ESP_BLE_AUDIO_TMAP_ROLE_CT;
#endif  /* CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_CT */
#if CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_UMR
    roles |= ESP_BLE_AUDIO_TMAP_ROLE_UMR;
#endif  /* CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_UMR */
#if CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMR
    roles |= ESP_BLE_AUDIO_TMAP_ROLE_BMR;
#endif  /* CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMR */
    return roles;
}
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */

static void media_ctrl_cmd_proc(esp_bt_audio_media_ctrl_cmd_t cmd)
{
    ESP_LOGI(TAG, "Media control command: %s", media_ctrl_cmd_to_str(cmd));
    switch (cmd) {
        case ESP_BT_AUDIO_MEDIA_CTRL_CMD_PLAY:
            esp_bt_audio_media_start(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC, NULL);
            break;
        case ESP_BT_AUDIO_MEDIA_CTRL_CMD_PAUSE:
            esp_bt_audio_media_stop(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC);
            break;
        case ESP_BT_AUDIO_MEDIA_CTRL_CMD_STOP:
            esp_bt_audio_media_stop(ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC);
            break;
        case ESP_BT_AUDIO_MEDIA_CTRL_CMD_NEXT:
#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
            local2bt_play_next();
#else
            esp_bt_audio_playback_next();
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
            break;
        case ESP_BT_AUDIO_MEDIA_CTRL_CMD_PREV:
#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
            local2bt_play_prev();
#else
            esp_bt_audio_playback_prev();
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
            break;
        default:
            ESP_LOGW(TAG, "Media control command %d not supported", cmd);
            break;
    }
}

static void playback_status_chg_proc(esp_bt_audio_event_playback_st_t *event_data)
{
    switch (event_data->event) {
        case ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE:
            ESP_LOGI(TAG, "Playback status changed: %d", event_data->evt_param.play_status);
#if CONFIG_EXAMPLE_BT_UI_ENABLE
            bt_ui_update_playback_status(ui, event_data->evt_param.play_status);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
            break;
        case ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE:
            ESP_LOGI(TAG, "Track changed, requesting metadata");
            esp_bt_audio_playback_request_metadata(ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE |
                                                   ESP_BT_AUDIO_PLAYBACK_METADATA_ARTIST |
                                                   ESP_BT_AUDIO_PLAYBACK_METADATA_ALBUM |
                                                   ESP_BT_AUDIO_PLAYBACK_METADATA_GENRE |
                                                   ESP_BT_AUDIO_PLAYBACK_METADATA_COVER_ART);
            break;
        case ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED:
            ESP_LOGI(TAG, "Playback position changed: %d", event_data->evt_param.position);
            break;
        default:
            ESP_LOGW(TAG, "Playback event %02X", event_data->event);
            break;
    }
}

static void playback_metadata_proc(esp_bt_audio_event_playback_metadata_t *event_data)
{
#if CONFIG_EXAMPLE_BT_UI_ENABLE
    static char title[96];
    static char artist[96];
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
    if (event_data->type == ESP_BT_AUDIO_PLAYBACK_METADATA_COVER_ART) {
        esp_bt_audio_playback_cover_art_t *cover_art = (esp_bt_audio_playback_cover_art_t *)event_data->value;
        if (cover_art != NULL && cover_art->data != NULL && cover_art->size > 0) {
            ESP_LOGI(TAG, "Cover art: size %d, format 0x%04X", cover_art->size, cover_art->format_fourcc);
#if CONFIG_EXAMPLE_BT_UI_ENABLE
            bt_ui_post_cover(ui, cover_art->data, cover_art->size);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
        }
    } else {
        ESP_LOGI(TAG, "Metadata: %s:\t%s",
                 playback_metadata_type_to_str(event_data->type), event_data->length > 0 ? (const char *)event_data->value : "");
#if CONFIG_EXAMPLE_BT_UI_ENABLE
        if (event_data->value != NULL && event_data->length > 0) {
            char *target = NULL;
            size_t target_size = 0;
            if (event_data->type == ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE) {
                target = title;
                target_size = sizeof(title);
            } else if (event_data->type == ESP_BT_AUDIO_PLAYBACK_METADATA_ARTIST) {
                target = artist;
                target_size = sizeof(artist);
            }
            if (target != NULL) {
                size_t copy_len = event_data->length < target_size - 1 ? event_data->length : target_size - 1;
                memcpy(target, event_data->value, copy_len);
                target[copy_len] = '\0';
                bt_ui_update_track(ui, title[0] ? title : NULL, artist[0] ? artist : NULL);
            }
        }
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
    }
}

static void bt_audio_event_cb(esp_bt_audio_event_t event, void *event_data, void *user_data)
{
    switch (event) {
        case ESP_BT_AUDIO_EVENT_DISCOVERY_STATE_CHG: {
            esp_bt_audio_event_discovery_st_t *discovery_state = (esp_bt_audio_event_discovery_st_t *)event_data;
            ESP_LOGI(TAG, "Device Discovery State Changed:");
            ESP_LOGI(TAG, "  State: %s", discovery_state->discovering ? "Discovering" : "Not discovering");
            break;
        }
        case ESP_BT_AUDIO_EVENT_DEVICE_DISCOVERED: {
            esp_bt_audio_event_device_discovered_t *device_discovered = (esp_bt_audio_event_device_discovered_t *)event_data;
            ESP_LOGI(TAG, "Device discovered:");
            ESP_LOGI(TAG, "  Name: %s", device_discovered->name);
            ESP_LOGI(TAG, "  Address: %02x:%02x:%02x:%02x:%02x:%02x",
                     device_discovered->addr[0], device_discovered->addr[1], device_discovered->addr[2],
                     device_discovered->addr[3], device_discovered->addr[4], device_discovered->addr[5]);
            ESP_LOGI(TAG, "  RSSI: %d dBm", device_discovered->rssi);
            if (device_discovered->tech == ESP_BT_AUDIO_TECH_CLASSIC) {
                ESP_LOGI(TAG, "  CoD: 0x%06x", device_discovered->disc_data.classic.cod);
            }
            cli_bt_device_found(device_discovered->name, device_discovered->addr);
            break;
        }
        case ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG: {
            esp_bt_audio_event_connection_st_t *conn_st = (esp_bt_audio_event_connection_st_t *)event_data;
            ESP_LOGI(TAG, "Connection state changed: %s", conn_st->connected ? "Connected" : "Disconnected");
            ESP_LOGI(TAG, "Connected device address: %02x:%02x:%02x:%02x:%02x:%02x",
                     conn_st->addr[0], conn_st->addr[1], conn_st->addr[2],
                     conn_st->addr[3], conn_st->addr[4], conn_st->addr[5]);
            if (conn_st->connected) {
#if CONFIG_EXAMPLE_BT_UI_ENABLE
                bt_ui_set_connected(ui, true);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
#if CONFIG_BT_CLASSIC_ENABLED && (defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE) || defined(CONFIG_GMF_EXAMPLE_A2DP_SINK))
                esp_bt_audio_classic_set_scan_mode(false, false);
#endif  /* CONFIG_BT_CLASSIC_ENABLED && (defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE) || defined(CONFIG_GMF_EXAMPLE_A2DP_SINK)) */
#if CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_PBAP_PCE)
                esp_bt_audio_classic_connect(ESP_BT_AUDIO_CLASSIC_ROLE_PBAP_PCE, conn_st->addr);
#endif  /* CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_PBAP_PCE) */
            } else {
#if CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE)
                esp_bt_audio_classic_set_scan_mode(true, false);
#elif CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_A2DP_SINK)
                esp_bt_audio_classic_set_scan_mode(true, true);
#endif  /* CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE) */
#if CONFIG_EXAMPLE_BT_UI_ENABLE
                bt_ui_set_connected(ui, false);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
            }
            cli_bt_device_conn_st_chg(conn_st->addr, conn_st->connected);
            break;
        }
        case ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG: {
            esp_bt_audio_event_stream_st_t *stream_state = (esp_bt_audio_event_stream_st_t *)event_data;
            stream_proc_state_chg(stream_state->stream_handle, stream_state->state);
#if CONFIG_EXAMPLE_BT_UI_ENABLE
            bt_ui_update_stream_type(ui, stream_state->stream_handle, stream_state->state);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
            break;
        }
        case ESP_BT_AUDIO_EVENT_MEDIA_CTRL_CMD: {
            esp_bt_audio_event_media_ctrl_t *media_ctrl_cmd = (esp_bt_audio_event_media_ctrl_t *)event_data;
            media_ctrl_cmd_proc(media_ctrl_cmd->cmd);
            break;
        }
        case ESP_BT_AUDIO_EVENT_PLAYBACK_STATUS_CHG: {
            esp_bt_audio_event_playback_st_t *playback_status = (esp_bt_audio_event_playback_st_t *)event_data;
            playback_status_chg_proc(playback_status);
            break;
        }
        case ESP_BT_AUDIO_EVENT_PLAYBACK_METADATA: {
            esp_bt_audio_event_playback_metadata_t *playback_metadata = (esp_bt_audio_event_playback_metadata_t *)event_data;
            playback_metadata_proc(playback_metadata);
            break;
        }
        case ESP_BT_AUDIO_EVENT_VOL_ABSOLUTE: {
            esp_bt_audio_event_vol_absolute_t *vol_absolute = (esp_bt_audio_event_vol_absolute_t *)event_data;
            ESP_LOGI(TAG, "ESP_BT_AUDIO_EVENT_VOL_ABSOLUTE vol %d, mute %d, context %d",
                     vol_absolute->vol, vol_absolute->mute, vol_absolute->context);
            volume_ctrl_cmd_t cmd = {
                .type = VOLUME_CTRL_CMD_ABSOLUTE,
                .vol = vol_absolute->vol,
                .mute = vol_absolute->mute,
                .context = vol_absolute->context,
            };
            volume_ctrl_post_cmd(&cmd);
#if CONFIG_EXAMPLE_BT_UI_ENABLE
            bt_ui_update_volume(ui, vol_absolute->vol);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
            break;
        }
        case ESP_BT_AUDIO_EVENT_VOL_RELATIVE: {
            esp_bt_audio_event_vol_relative_t *vol_relative = (esp_bt_audio_event_vol_relative_t *)event_data;
            ESP_LOGI(TAG, "ESP_BT_AUDIO_EVENT_VOL_RELATIVE up_down %d, context %d", vol_relative->up_down, vol_relative->context);
            volume_ctrl_cmd_t cmd = {
                .type = VOLUME_CTRL_CMD_RELATIVE,
                .up_down = vol_relative->up_down,
                .context = vol_relative->context,
            };
            volume_ctrl_post_cmd(&cmd);
#if CONFIG_EXAMPLE_BT_UI_ENABLE
            bt_ui_update_volume(ui, bt_ui_get_volume(ui) + (vol_relative->up_down ? 10 : -10));
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
            break;
        }
        case ESP_BT_AUDIO_EVENT_TEL_STATUS_CHG: {
            esp_bt_audio_event_tel_status_chg_t *tel_status = (esp_bt_audio_event_tel_status_chg_t *)event_data;
            ESP_LOGI(TAG, "Telephony Status Changed:");
            ESP_LOGI(TAG, "  Type: %s", tel_event_to_str(tel_status->type));
            switch (tel_status->type) {
                case ESP_BT_AUDIO_TEL_STATUS_BATTERY:
                    ESP_LOGI(TAG, "  Battery level: %u%%", tel_status->data.battery.level);
                    break;
                case ESP_BT_AUDIO_TEL_STATUS_SIGNAL_STRENGTH:
                    ESP_LOGI(TAG, "  Signal strength: %d", tel_status->data.signal_strength.value);
                    break;
                case ESP_BT_AUDIO_TEL_STATUS_ROAMING:
                    ESP_LOGI(TAG, "  Roaming: %s", tel_status->data.roaming.active ? "active" : "inactive");
                    break;
                case ESP_BT_AUDIO_TEL_STATUS_NETWORK:
                    ESP_LOGI(TAG, "  Network: %s", tel_status->data.network.available ? "available" : "unavailable");
                    break;
                case ESP_BT_AUDIO_TEL_STATUS_OPERATOR:
                    ESP_LOGI(TAG, "  Operator: %s", tel_status->data.operator_name.name);
                    break;
                default:
                    break;
            }
            break;
        }
        case ESP_BT_AUDIO_EVENT_CALL_STATE_CHG: {
            esp_bt_audio_event_call_state_t *call_state = (esp_bt_audio_event_call_state_t *)event_data;
            ESP_LOGI(TAG, "Call State Changed: idx=%u dir=%s state=%s uri=%s",
                     call_state->idx,
                     call_state->dir == ESP_BT_AUDIO_CALL_DIR_INCOMING ? "INCOMING" : "OUTGOING",
                     call_state_to_str(call_state->state),
                     call_state->uri[0] ? call_state->uri : "(none)");
#if CONFIG_EXAMPLE_BT_UI_ENABLE
            const char *display_uri = call_state->uri[0] ? call_state->uri : NULL;
            if (display_uri) {
                const char *colon = strchr(display_uri, ':');
                if (colon && colon[1]) {
                    display_uri = colon + 1;
                }
            }
            bt_ui_update_call_state(ui, (int)call_state->state, display_uri);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
            break;
        }
        case ESP_BT_AUDIO_EVENT_PHONEBOOK_COUNT: {
            uint16_t count = *(uint16_t *)event_data;
            ESP_LOGI(TAG, "Phonebook count: %u", count);
            break;
        }
        case ESP_BT_AUDIO_EVENT_PHONEBOOK_ENTRY: {
            esp_bt_audio_pb_entry_t *entry = (esp_bt_audio_pb_entry_t *)event_data;
            char *buf = heap_caps_calloc_prefer(1, PHONEBOOK_ENTRY_LOG_BUF_SIZE, 2,
                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
            if (buf) {
                int n = snprintf(buf, PHONEBOOK_ENTRY_LOG_BUF_SIZE,
                                 "Phonebook entry: Fullname [%s], Last [%s], First [%s], Middle [%s]",
                                 entry->fullname ? entry->fullname : "",
                                 entry->name.last_name ? entry->name.last_name : "",
                                 entry->name.first_name ? entry->name.first_name : "",
                                 entry->name.middle_name ? entry->name.middle_name : "");
                for (size_t i = 0; i < entry->tel_count && n > 0 && (size_t)n < PHONEBOOK_ENTRY_LOG_BUF_SIZE; i++) {
                    if (entry->tel[i].number) {
                        n += snprintf(buf + n, PHONEBOOK_ENTRY_LOG_BUF_SIZE - (size_t)n, " | Tel [%s] (%s)",
                                      entry->tel[i].number, entry->tel[i].type ? entry->tel[i].type : "");
                    }
                }
                ESP_LOGI(TAG, "%s", buf);
                free(buf);
            }
            break;
        }
        case ESP_BT_AUDIO_EVENT_PHONEBOOK_HISTORY: {
            esp_bt_audio_pb_history_t *history = (esp_bt_audio_pb_history_t *)event_data;
            ESP_LOGI(TAG, "Phonebook history: Full name [%s], Property [%s], Tel [%s], Timestamp [%s]",
                     history->entry.fullname ? history->entry.fullname : "",
                     history->property ? history->property : "",
                     (history->entry.tel_count > 0 && history->entry.tel[0].number) ? history->entry.tel[0].number : "",
                     history->timestamp ? history->timestamp : "");
            break;
        }
        case ESP_BT_AUDIO_EVENT_BIG_SYNC_LOST: {
            ESP_LOGI(TAG, "BIG sync lost");
            break;
        }
        case ESP_BT_AUDIO_EVENT_PA_SYNC_LOST: {
            ESP_LOGI(TAG, "PA sync lost");
            break;
        }
        default:
            ESP_LOGI(TAG, "bt audio event %d", event);
            break;
    }
}

void app_main()
{
    /* Initialize NVS flash which is used by bluetooth */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize all devices with board manager */
    ESP_ERROR_CHECK(esp_board_manager_init());

    /* Initialize codec devices */
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = CODEC_DAC_SAMPLE_RATE,
        .bits_per_sample = CODEC_DAC_BITS_PER_SAMPLE,
        .channel = CODEC_DAC_CHANNELS,
    };
    dev_audio_codec_handles_t *codec_handle = NULL;
    ESP_ERROR_CHECK(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&codec_handle));
    ESP_ERROR_CHECK(esp_codec_dev_open(codec_handle->codec_dev, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(codec_handle->codec_dev, 50));
    setup_volume_ctrl_task();

    fs = (esp_codec_dev_sample_info_t) {
        .sample_rate = CODEC_ADC_SAMPLE_RATE,
        .bits_per_sample = CODEC_ADC_BITS_PER_SAMPLE,
        .channel = CODEC_ADC_CHANNELS,
    };
    ESP_ERROR_CHECK(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&codec_handle));
    ESP_ERROR_CHECK(esp_codec_dev_open(codec_handle->codec_dev, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(codec_handle->codec_dev, 40.0f));

    /* Initialize GMF pool */
    ESP_ERROR_CHECK(esp_gmf_pool_init(&pool));

    /* Register elements and IO types to GMF pool */
    ESP_ERROR_CHECK(pool_reg(pool));

    /* Setup pipelines for bluetooth audio */
    stream_proc_init(pool);

#if CONFIG_EXAMPLE_BT_UI_ENABLE
    bt_ui_config_t ui_cfg = {
        .dial_cb = on_dial_cb,
        .dial_ctx = NULL,
        .end_call_cb = on_end_call_cb,
        .end_call_ctx = NULL,
        .answer_call_cb = on_answer_call_cb,
        .answer_call_ctx = NULL,
        .play_pause_cb = on_play_pause_cb,
        .play_pause_ctx = NULL,
        .prev_cb = on_prev_cb,
        .next_cb = on_next_cb,
        .prev_next_ctx = NULL,
    };
    ESP_ERROR_CHECK(bt_ui_init());
    ui = bt_ui_create(BT_UI_DEVICE_NAME, &ui_cfg);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */

#if CONFIG_BT_ENABLED
#ifdef CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY
    uint32_t btmode = ESP_BT_MODE_CLASSIC_BT;
#elif CONFIG_BTDM_CTRL_MODE_BLE_ONLY
    uint32_t btmode = ESP_BT_MODE_BLE;
#elif CONFIG_BTDM_CTRL_MODE_BTDM
#if CONFIG_BT_CLASSIC_ENABLED && CONFIG_GMF_EXAMPLE_AUDIO_TECH_CLASSIC
    uint32_t btmode = ESP_BT_MODE_BTDM;
#else
    uint32_t btmode = ESP_BT_MODE_BLE;
#endif  /* CONFIG_BT_CLASSIC_ENABLED && CONFIG_GMF_EXAMPLE_AUDIO_TECH_CLASSIC */
#else   /* CONFIG_BTDM_CTRL_MODE_BTDM */
    uint32_t btmode = ESP_BT_MODE_BLE;
#endif  /* CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(btmode));
#endif  /* CONFIG_BT_ENABLED */

    /* Initialize Bluetooth module */
    void *host_config = NULL;
#if CONFIG_BT_NIMBLE_ENABLED
    esp_bt_audio_host_nimble_cfg_t host_cfg = ESP_BT_AUDIO_HOST_NIMBLE_CFG_DEFAULT();
#if CONFIG_EXAMPLE_BT_UI_ENABLE
    snprintf(host_cfg.dev_name, sizeof(host_cfg.dev_name), "%s", BT_UI_DEVICE_NAME);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
#if CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_SIZE <= 1
    ESP_ERROR_CHECK(append_mac_suffix_to_device_name(host_cfg.dev_name, sizeof(host_cfg.dev_name)));
#endif  /* CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_SIZE <= 1 */
    host_config = &host_cfg;
#elif CONFIG_BT_BLUEDROID_ENABLED
    esp_bt_audio_host_bluedroid_cfg_t host_cfg = ESP_BT_AUDIO_HOST_BLUEDROID_CFG_DEFAULT();
#if CONFIG_EXAMPLE_BT_UI_ENABLE
    snprintf(host_cfg.dev_name, sizeof(host_cfg.dev_name), "%s", BT_UI_DEVICE_NAME);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
    ESP_ERROR_CHECK(append_mac_suffix_to_device_name(host_cfg.dev_name, sizeof(host_cfg.dev_name)));
    host_config = &host_cfg;
#endif  /* CONFIG_BT_NIMBLE_ENABLED */

    esp_bt_audio_config_t bt_config = {
        .host_config = host_config,
        .event_cb = bt_audio_event_cb,
        .event_user_ctx = NULL,
#if CONFIG_BT_CLASSIC_ENABLED && CONFIG_GMF_EXAMPLE_AUDIO_TECH_CLASSIC
        .classic.roles = get_classic_roles(),
#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
        .classic.a2dp_src_send_task_core_id = A2DP_SRC_SEND_TASK_CORE_ID,
        .classic.a2dp_src_send_task_prio = A2DP_SRC_SEND_TASK_PRIO,
        .classic.a2dp_src_send_task_stack_size = A2DP_SRC_SEND_TASK_STACK_SIZE,
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
#endif  /* CONFIG_BT_CLASSIC_ENABLED && CONFIG_GMF_EXAMPLE_AUDIO_TECH_CLASSIC */
#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
        .le.user_case = ESP_BT_AUDIO_LE_USER_CASE_TMAP,
        .le.roles = get_le_roles(),
        .le.snk_cnt = 1,
        .le.src_cnt = LE_SOURCE_COUNT,
        .le.pacs = {
            .sink_enabled = 1,
            .sink_context_mask = ESP_BLE_AUDIO_CONTEXT_TYPE_ANY,
            .sink_locations = LE_LOCATION,
            .source_enabled = LE_SOURCE_ENABLED,
            .source_context_mask = LE_SOURCE_CONTEXT_MASK,
            .source_locations = LE_SOURCE_LOCATION,
        },
        .le.vcp = {
            .volume = 50,
            .mute = 0,
            .step = 10,
        },
        .le.csip = {
            .coordinate_set_size = CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_SIZE,
            .rank = CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_RANK,
            .sirk = {184, 3, 170, 198, 175, 187, 101, 162, 90, 65, 241, 83, 5, 105, 143, 132},
        },
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */
    };
#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
    ESP_LOGI(TAG, "LE Audio configuration:");
    ESP_LOGI(TAG, "  User case: %s", CONFIG_GMF_EXAMPLE_LE_USER_CASE_TMAP ? "TMAP" : "UNKNOWN");
    ESP_LOGI(TAG, "  Roles: %s", CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_CT ? "CT" : "UNKNOWN");
    ESP_LOGI(TAG, "  Roles: %s", CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_UMR ? "UMR" : "UNKNOWN");
    ESP_LOGI(TAG, "  Roles: %s", CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMR ? "BMR" : "UNKNOWN");
    ESP_LOGI(TAG, "  Coordinate set size: %d", CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_SIZE);
    ESP_LOGI(TAG, "  Coordinate set rank: %d", CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_RANK);
    ESP_LOGI(TAG, "  PACS sink locations: %s", LE_LOCATION == ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT ? "Front left" : "Front right");
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */
    ESP_ERROR_CHECK(esp_bt_audio_init(&bt_config));
#if CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE)
    ESP_ERROR_CHECK(esp_bt_audio_classic_set_scan_mode(true, false));
#elif CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_A2DP_SINK)
    ESP_ERROR_CHECK(esp_bt_audio_classic_set_scan_mode(true, true));
#endif  /* CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE) */
    ESP_ERROR_CHECK(esp_bt_audio_playback_reg_notifications(ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_END |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_START |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_NOW_PLAYING_CHANGE |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_AVAILABLE_PLAYERS_CHANGE |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_ADDRESSED_PLAYER_CHANGE));
    /* Initialize console for user interaction */
    cli_init();
}
