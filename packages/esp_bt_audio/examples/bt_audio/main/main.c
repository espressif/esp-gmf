/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_bt_audio_stream.h"
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
#include "esp_board_device.h"
#include "esp_board_manager_includes.h"

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
#define APP_CTRL_QUEUE_SIZE           12
#define APP_CTRL_TASK_STACK_SIZE      3072
#define APP_CTRL_TASK_PRIO            5
#define APP_CTRL_TASK_CORE_ID         0

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
    "BATTERY",
    "SIGNAL_STRENGTH",
    "ROAMING",
    "NETWORK",
    "OPERATOR",
    "UNKNOWN",
};

#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
static const char *le_audio_locations_to_str(uint32_t locations)
{
    if ((locations & (ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT | ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT)) ==
        (ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT | ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT)) {
        return "Front left/right";
    }
    if (locations & ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT) {
        return "Front left";
    }
    if (locations & ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT) {
        return "Front right";
    }
    return "None";
}
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */

static esp_gmf_pool_handle_t pool = NULL;
static QueueHandle_t app_ctrl_queue = NULL;
static bool phone_connected = false;
static esp_bt_audio_tech_t active_connected_tech = ESP_BT_AUDIO_TECH_CLASSIC;

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

typedef enum {
    APP_CTRL_MSG_VOLUME,
    APP_CTRL_MSG_CONNECTABLE_RESTORE,
    APP_CTRL_MSG_CONNECTABLE_DISABLE_PEER,
} app_ctrl_msg_id_t;

typedef union {
    volume_ctrl_cmd_t    volume;
    esp_bt_audio_tech_t  tech;
} app_ctrl_msg_data_t;

typedef struct {
    app_ctrl_msg_id_t    msg_id;
    app_ctrl_msg_data_t  msg_data;
} app_ctrl_msg_t;

#if CONFIG_EXAMPLE_BT_UI_ENABLE
static bt_ui_t *ui = NULL;
static char s_playback_title[96];
static char s_playback_artist[96];
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

static bool app_ctrl_send_cmd(app_ctrl_msg_id_t msg_id, const app_ctrl_msg_data_t *msg_data)
{
    if (app_ctrl_queue == NULL) {
        ESP_LOGE(TAG, "App control task is not initialized");
        return false;
    }

    app_ctrl_msg_t msg = {
        .msg_id = msg_id,
    };
    if (msg_data) {
        msg.msg_data = *msg_data;
    }

    if (xQueueSend(app_ctrl_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "App control queue full, drop msg %d", msg_id);
        return false;
    }
    return true;
}

static bool volume_ctrl_post_cmd(const volume_ctrl_cmd_t *cmd)
{
    app_ctrl_msg_data_t msg_data = {
        .volume = *cmd,
    };
    return app_ctrl_send_cmd(APP_CTRL_MSG_VOLUME, &msg_data);
}

static void bt_audio_restore_connectable(void)
{
#if CONFIG_BT_CLASSIC_ENABLED && (defined(CONFIG_GMF_EXAMPLE_HFP_AG) || defined(CONFIG_GMF_EXAMPLE_A2DP_SINK) || defined(CONFIG_GMF_EXAMPLE_HFP_HF))
    {
        esp_err_t ret = esp_bt_audio_classic_set_scan_mode(true, true);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Restore Classic scan mode failed: %s", esp_err_to_name(ret));
        }
    }
#elif CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE)
    {
        esp_err_t ret = esp_bt_audio_classic_set_scan_mode(true, false);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Restore Classic scan mode failed: %s", esp_err_to_name(ret));
        }
    }
#endif  /* CONFIG_BT_CLASSIC_ENABLED && (defined(CONFIG_GMF_EXAMPLE_HFP_AG) || defined(CONFIG_GMF_EXAMPLE_A2DP_SINK) || defined(CONFIG_GMF_EXAMPLE_HFP_HF)) */

#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
    {
        esp_err_t ret = esp_bt_audio_le_set_advertising(true);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Restore LE advertising failed: %s", esp_err_to_name(ret));
        }
    }
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */
}

static void bt_audio_disable_peer_connectable(esp_bt_audio_tech_t tech)
{
    if (tech == ESP_BT_AUDIO_TECH_CLASSIC) {
#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
        esp_err_t ret = esp_bt_audio_le_set_advertising(false);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Disable LE advertising failed: %s", esp_err_to_name(ret));
        }
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */
    } else if (tech == ESP_BT_AUDIO_TECH_LE) {
#if CONFIG_BT_CLASSIC_ENABLED && (defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE) || defined(CONFIG_GMF_EXAMPLE_A2DP_SINK) || defined(CONFIG_GMF_EXAMPLE_HFP_AG) || defined(CONFIG_GMF_EXAMPLE_HFP_HF))
        esp_err_t ret = esp_bt_audio_classic_set_scan_mode(false, false);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Disable Classic scan mode failed: %s", esp_err_to_name(ret));
        }
#endif  /* CONFIG_BT_CLASSIC_ENABLED && (defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE) || defined(CONFIG_GMF_EXAMPLE_A2DP_SINK) || defined(CONFIG_GMF_EXAMPLE_HFP_AG) || defined(CONFIG_GMF_EXAMPLE_HFP_HF)) */
    }
}

static esp_err_t volume_ctrl_get_codec(dev_audio_codec_handles_t **codec_handle);

static void app_ctrl_handle_volume(const volume_ctrl_cmd_t *cmd)
{
    dev_audio_codec_handles_t *codec_handle = NULL;
    if (volume_ctrl_get_codec(&codec_handle) != ESP_OK) {
        return;
    }

    switch (cmd->type) {
        case VOLUME_CTRL_CMD_ABSOLUTE:
            ESP_LOGI(TAG, "Set absolute volume: vol %d, mute %d, context %d", cmd->vol, cmd->mute, cmd->context);
            esp_codec_dev_set_out_vol(codec_handle->codec_dev, cmd->mute ? 0 : cmd->vol);
            break;
        case VOLUME_CTRL_CMD_RELATIVE: {
            int current_volume = 0;
            esp_codec_dev_get_out_vol(codec_handle->codec_dev, &current_volume);
            current_volume = cmd->up_down ?
                             ((current_volume >= 90) ? 100 : current_volume + 10) :
                             ((current_volume <= 10) ? 0 : current_volume - 10);
            esp_codec_dev_set_out_vol(codec_handle->codec_dev, current_volume);
            ESP_LOGI(TAG, "Set relative volume: up_down %d, context %d, volume %d",
                     cmd->up_down, cmd->context, current_volume);
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown volume control action %d", cmd->type);
            break;
    }
}

static void app_ctrl_task(void *arg)
{
    app_ctrl_msg_t msg = {0};

    while (true) {
        if (xQueueReceive(app_ctrl_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        switch (msg.msg_id) {
            case APP_CTRL_MSG_VOLUME:
                app_ctrl_handle_volume(&msg.msg_data.volume);
                break;
            case APP_CTRL_MSG_CONNECTABLE_RESTORE:
                bt_audio_restore_connectable();
                break;
            case APP_CTRL_MSG_CONNECTABLE_DISABLE_PEER:
                bt_audio_disable_peer_connectable(msg.msg_data.tech);
                break;
            default:
                ESP_LOGW(TAG, "Unknown app control msg %d", msg.msg_id);
                break;
        }
    }
}

static void setup_app_ctrl_task(void)
{
    if (app_ctrl_queue) {
        return;
    }

    app_ctrl_queue = xQueueCreate(APP_CTRL_QUEUE_SIZE, sizeof(app_ctrl_msg_t));
    if (app_ctrl_queue == NULL) {
        ESP_LOGE(TAG, "Create app control command queue failed");
        return;
    }

    BaseType_t ret = xTaskCreatePinnedToCoreWithCaps(app_ctrl_task, "app_ctrl_task", APP_CTRL_TASK_STACK_SIZE,
                                                     NULL, APP_CTRL_TASK_PRIO, NULL, APP_CTRL_TASK_CORE_ID,
                                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Create app control task failed");
        vQueueDelete(app_ctrl_queue);
        app_ctrl_queue = NULL;
    }
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

#if CONFIG_EXAMPLE_BT_UI_ENABLE
static void on_dial_cb(const char *number, void *ctx)
{
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
    esp_err_t ret = esp_bt_audio_call_reject(0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "End call failed: %s", esp_err_to_name(ret));
    }
}

static void on_answer_call_cb(void *ctx)
{
    esp_err_t ret = esp_bt_audio_call_answer(0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Answer call failed: %s", esp_err_to_name(ret));
    }
}

static void on_play_pause_cb(bool want_play, void *ctx)
{
    esp_err_t ret = want_play ? esp_bt_audio_playback_play() : esp_bt_audio_playback_pause();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback %s failed: %s", want_play ? "play" : "pause", esp_err_to_name(ret));
    }
}

static void on_prev_cb(void *ctx)
{
    esp_err_t ret = esp_bt_audio_playback_prev();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback prev failed: %s", esp_err_to_name(ret));
    }
}

static void on_next_cb(void *ctx)
{
    esp_err_t ret = esp_bt_audio_playback_next();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Playback next failed: %s", esp_err_to_name(ret));
    }
}
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */

static esp_err_t setup_device_name(char *device_name, size_t device_name_size)
{
    int written = snprintf(device_name, device_name_size, "%s", CONFIG_GMF_EXAMPLE_BT_DEVICE_NAME);
    if (written < 0 || (size_t)written >= device_name_size) {
        return ESP_ERR_INVALID_SIZE;
    }

#if CONFIG_GMF_EXAMPLE_BT_DEVICE_NAME_APPEND_MAC
    uint8_t mac[6] = {0};
    char default_name[ESP_BT_AUDIO_HOST_MAX_DEV_NAME_LEN] = {0};

    written = snprintf(default_name, sizeof(default_name), "%s", device_name);
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
#endif  /* CONFIG_GMF_EXAMPLE_BT_DEVICE_NAME_APPEND_MAC */
    return ESP_OK;
}

#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static void bytes_from_hex(const char *hex, uint8_t *out, size_t out_len)
{
    if (hex == NULL || out == NULL || out_len == 0) {
        return;
    }

    memset(out, 0, out_len);
    size_t hex_len = strlen(hex);
    for (size_t i = 0; i < out_len; i++) {
        if ((i * 2 + 1) < hex_len) {
            int high = hex_nibble(hex[i * 2]);
            int low = hex_nibble(hex[i * 2 + 1]);
            if (high >= 0 && low >= 0) {
                out[i] = (uint8_t)((high << 4) | low);
            }
        }
    }
}

#ifdef CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMS
static void bytes_from_string(const char *str, uint8_t *out, size_t out_len)
{
    if (str == NULL || out == NULL || out_len == 0) {
        return;
    }

    memset(out, 0, out_len);
    memcpy(out, str, strnlen(str, out_len));
}
#endif  /* CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMS */
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */

static void setup_bt_audio_config_from_kconfig(esp_bt_audio_config_t *bt_config)
{
    if (bt_config == NULL) {
        return;
    }

#if CONFIG_BT_CLASSIC_ENABLED && CONFIG_GMF_EXAMPLE_AUDIO_TECH_CLASSIC
#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
    bt_config->classic.roles = ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC;
#else
    bt_config->classic.roles = ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SNK;
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
#ifdef CONFIG_GMF_EXAMPLE_HFP_HF
    bt_config->classic.roles |= ESP_BT_AUDIO_CLASSIC_ROLE_HFP_HF;
#endif  /* CONFIG_GMF_EXAMPLE_HFP_HF */
#ifdef CONFIG_GMF_EXAMPLE_HFP_AG
    bt_config->classic.roles |= ESP_BT_AUDIO_CLASSIC_ROLE_HFP_AG;
#endif  /* CONFIG_GMF_EXAMPLE_HFP_AG */
#ifdef CONFIG_GMF_EXAMPLE_AVRC_CT
    bt_config->classic.roles |= ESP_BT_AUDIO_CLASSIC_ROLE_AVRC_CT;
#endif  /* CONFIG_GMF_EXAMPLE_AVRC_CT */
#ifdef CONFIG_GMF_EXAMPLE_AVRC_TG
    bt_config->classic.roles |= ESP_BT_AUDIO_CLASSIC_ROLE_AVRC_TG;
#endif  /* CONFIG_GMF_EXAMPLE_AVRC_TG */
#ifdef CONFIG_GMF_EXAMPLE_PBAP_PCE
    bt_config->classic.roles |= ESP_BT_AUDIO_CLASSIC_ROLE_PBAP_PCE;
#endif  /* CONFIG_GMF_EXAMPLE_PBAP_PCE */
#endif  /* CONFIG_BT_CLASSIC_ENABLED && CONFIG_GMF_EXAMPLE_AUDIO_TECH_CLASSIC */

#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
    bt_config->le.user_case = ESP_BT_AUDIO_LE_USER_CASE_TMAP;
    bt_config->le.roles = 0;
#ifdef CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_CT
    bt_config->le.roles |= ESP_BLE_AUDIO_TMAP_ROLE_CT;
#endif  /* CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_CT */
#ifdef CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_UMR
    bt_config->le.roles |= ESP_BLE_AUDIO_TMAP_ROLE_UMR;
#endif  /* CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_UMR */
#ifdef CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMR
    bt_config->le.roles |= ESP_BLE_AUDIO_TMAP_ROLE_BMR;
#endif  /* CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMR */
#ifdef CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMS
    bt_config->le.roles |= ESP_BLE_AUDIO_TMAP_ROLE_BMS;
#endif  /* CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMS */

    bt_config->le.snk_cnt = 1;
#ifdef CONFIG_GMF_EXAMPLE_LE_SOURCE_ENABLE
    bt_config->le.src_cnt = 1;
#else
    bt_config->le.src_cnt = 0;
#endif  /* CONFIG_GMF_EXAMPLE_LE_SOURCE_ENABLE */

    bool pacs_needed = (bt_config->le.roles & (ESP_BLE_AUDIO_TMAP_ROLE_CT |
                                               ESP_BLE_AUDIO_TMAP_ROLE_UMR |
                                               ESP_BLE_AUDIO_TMAP_ROLE_BMR)) != 0;
    bt_config->le.pacs.sink_enabled = pacs_needed;
    bt_config->le.pacs.sink_context_mask = pacs_needed ? ESP_BLE_AUDIO_CONTEXT_TYPE_ANY : 0;
    uint32_t le_locations = 0;
#if CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_LEFT
    le_locations |= ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT;
#endif  /* CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_LEFT */
#if CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_RIGHT
    le_locations |= ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT;
#endif  /* CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_RIGHT */
#if CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_LEFT_RIGHT
    le_locations |= ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT | ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT;
#endif  /* CONFIG_GMF_EXAMPLE_LE_LOCATION_FRONT_LEFT_RIGHT */
    bt_config->le.pacs.sink_locations = pacs_needed ? le_locations : 0;
    bt_config->le.pacs.source_locations = pacs_needed ? le_locations : 0;
#ifdef CONFIG_GMF_EXAMPLE_LE_SOURCE_ENABLE
    bt_config->le.pacs.source_enabled = pacs_needed;
#else
    bt_config->le.pacs.source_enabled = 0;
#endif  /* CONFIG_GMF_EXAMPLE_LE_SOURCE_ENABLE */
    bt_config->le.pacs.source_context_mask = bt_config->le.pacs.source_enabled ? ESP_BLE_AUDIO_CONTEXT_TYPE_ANY : 0;
    if (bt_config->le.pacs.source_enabled == 0) {
        bt_config->le.pacs.source_locations = 0;
    }

    bt_config->le.csip.coordinate_set_size = CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_SIZE;
    bt_config->le.csip.rank = CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_RANK;
#if CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_SIZE > 1
    bytes_from_hex(CONFIG_GMF_EXAMPLE_LE_CSIP_SIRK, bt_config->le.csip.sirk, sizeof(bt_config->le.csip.sirk));
#endif  /* CONFIG_GMF_EXAMPLE_LE_COORDINATE_SET_SIZE > 1 */

#ifdef CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMS
    snprintf((char *)bt_config->le.bsrc.broadcast_name, sizeof(bt_config->le.bsrc.broadcast_name),
             "%s", CONFIG_GMF_EXAMPLE_LE_BSRC_NAME);
    bytes_from_string(CONFIG_GMF_EXAMPLE_LE_BSRC_CODE, bt_config->le.bsrc.broadcast_code,
                      sizeof(bt_config->le.bsrc.broadcast_code));
    bt_config->le.bsrc.stream_num = CONFIG_GMF_EXAMPLE_LE_BSRC_STREAM_NUM;
#endif  /* CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMS */
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */
}

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
    if (event_data->type == ESP_BT_AUDIO_PLAYBACK_METADATA_COVER_ART) {
        esp_bt_audio_playback_cover_art_t *cover_art = (esp_bt_audio_playback_cover_art_t *)event_data->value;
        if (cover_art != NULL && cover_art->data != NULL && cover_art->size > 0) {
            ESP_LOGI(TAG, "Cover art: size %d, format 0x%04X", cover_art->size, cover_art->format_fourcc);
#if CONFIG_EXAMPLE_BT_UI_ENABLE
            bt_ui_post_cover(ui, cover_art->data, cover_art->size);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
        }
    } else {
        ESP_LOGI(TAG, "Metadata: %s:\t%.*s",
                 playback_metadata_type_to_str(event_data->type),
                 event_data->value && event_data->length > 0 ? (int)event_data->length : 0,
                 event_data->value ? (const char *)event_data->value : "");
#if CONFIG_EXAMPLE_BT_UI_ENABLE
        if (event_data->value != NULL && event_data->length > 0) {
            char *target = NULL;
            size_t target_size = 0;
            if (event_data->type == ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE) {
                target = s_playback_title;
                target_size = sizeof(s_playback_title);
            } else if (event_data->type == ESP_BT_AUDIO_PLAYBACK_METADATA_ARTIST) {
                target = s_playback_artist;
                target_size = sizeof(s_playback_artist);
            }
            if (target != NULL) {
                size_t copy_len = event_data->length < target_size - 1 ? event_data->length : target_size - 1;
                memcpy(target, event_data->value, copy_len);
                target[copy_len] = '\0';
                bt_ui_update_track(ui, s_playback_title[0] ? s_playback_title : NULL,
                                   s_playback_artist[0] ? s_playback_artist : NULL);
            }
        }
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
    }
}

#if CONFIG_EXAMPLE_BT_UI_ENABLE
static void playback_metadata_clear_ui(void)
{
    s_playback_title[0] = '\0';
    s_playback_artist[0] = '\0';
    bt_ui_update_track(ui, "", "");
    bt_ui_post_cover(ui, NULL, 0);
}
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */

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
                phone_connected = true;
                active_connected_tech = conn_st->tech;
#if CONFIG_EXAMPLE_BT_UI_ENABLE
                bt_ui_set_connected(ui, true, conn_st->tech);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
                app_ctrl_msg_data_t msg_data = {
                    .tech = conn_st->tech,
                };
                app_ctrl_send_cmd(APP_CTRL_MSG_CONNECTABLE_DISABLE_PEER, &msg_data);
#if CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_PBAP_PCE)
                if (conn_st->tech == ESP_BT_AUDIO_TECH_CLASSIC) {
                    esp_bt_audio_classic_connect(ESP_BT_AUDIO_CLASSIC_ROLE_PBAP_PCE, conn_st->addr);
                }
#endif  /* CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_PBAP_PCE) */
            } else {
                if (!phone_connected || conn_st->tech == active_connected_tech) {
                    phone_connected = false;
                    app_ctrl_msg_data_t msg_data = {
                        .tech = conn_st->tech,
                    };
                    app_ctrl_send_cmd(APP_CTRL_MSG_CONNECTABLE_RESTORE, &msg_data);
#if CONFIG_EXAMPLE_BT_UI_ENABLE
                    bt_ui_set_connected(ui, false, conn_st->tech);
                    playback_metadata_clear_ui();
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
                }
            }
            cli_bt_device_conn_st_chg(conn_st->addr, conn_st->connected);
            break;
        }
        case ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG: {
            esp_bt_audio_event_stream_st_t *stream_state = (esp_bt_audio_event_stream_st_t *)event_data;
            stream_proc_state_chg(stream_state->stream_handle, stream_state->state);
#if CONFIG_EXAMPLE_BT_UI_ENABLE
            bt_ui_update_stream_state(ui, stream_state->stream_handle, stream_state->state);
#endif  /* CONFIG_EXAMPLE_BT_UI_ENABLE */
            esp_bt_audio_stream_dir_t dir = ESP_BT_AUDIO_STREAM_DIR_UNKNOWN;
            esp_bt_audio_stream_get_dir(stream_state->stream_handle, &dir);
            if (stream_state->state == ESP_BT_AUDIO_STREAM_STATE_ALLOCATED &&
                dir == ESP_BT_AUDIO_STREAM_DIR_SINK) {
                esp_bt_audio_playback_reg_notifications(ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE |
                                                                        ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE |
                                                                        ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_END |
                                                                        ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_START |
                                                                        ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED |
                                                                        ESP_BT_AUDIO_PLAYBACK_EVENT_NOW_PLAYING_CHANGE |
                                                                        ESP_BT_AUDIO_PLAYBACK_EVENT_AVAILABLE_PLAYERS_CHANGE |
                                                                        ESP_BT_AUDIO_PLAYBACK_EVENT_ADDRESSED_PLAYER_CHANGE);
            }
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

#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
static void bt_audio_reconfig_lcd(void)
{
    dev_display_lcd_config_t lcd_cfg;
    dev_display_lcd_config_t *origin_cfg = NULL;
    esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&origin_cfg);
    if (origin_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get display config");
        return;
    }
    memcpy(&lcd_cfg, origin_cfg, sizeof(dev_display_lcd_config_t));
    if (strcmp(origin_cfg->sub_type, "rgb") == 0) {
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT
        lcd_cfg.sub_cfg.rgb.panel_config.num_fbs = 2;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_RGB_SUPPORT */
    } else if (strcmp(origin_cfg->sub_type, "dsi") == 0) {
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT
        lcd_cfg.sub_cfg.dsi.dpi_config.num_fbs = 2;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT */
    }
    esp_board_device_override_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &lcd_cfg, sizeof(dev_display_lcd_config_t));
    ESP_LOGI(TAG, "LCD configuration overridden");
}
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */

void app_main()
{
    /* Initialize NVS flash which is used by bluetooth */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* Initialize only the audio codec devices in monitor mode to keep monitor GPIOs available. */
#if CONFIG_ESP_BT_AUDIO_MONITOR
    ESP_ERROR_CHECK(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC));
    ESP_ERROR_CHECK(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC));
#else
    /* Reconfigure LCD to use double framebuffer */
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
    bt_audio_reconfig_lcd();
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */

    /* Initialize all devices with board manager */
    ESP_ERROR_CHECK(esp_board_manager_init());
#endif  /* CONFIG_ESP_BT_AUDIO_MONITOR */

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
    setup_app_ctrl_task();

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

    char device_name[ESP_BT_AUDIO_HOST_MAX_DEV_NAME_LEN] = {0};
    ESP_ERROR_CHECK(setup_device_name(device_name, sizeof(device_name)));

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
    ui = bt_ui_create(device_name, &ui_cfg);
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
    snprintf(host_cfg.dev_name, sizeof(host_cfg.dev_name), "%s", device_name);
    host_config = &host_cfg;
#elif CONFIG_BT_BLUEDROID_ENABLED
    esp_bt_audio_host_bluedroid_cfg_t host_cfg = ESP_BT_AUDIO_HOST_BLUEDROID_CFG_DEFAULT();
    snprintf(host_cfg.dev_name, sizeof(host_cfg.dev_name), "%s", device_name);
    host_config = &host_cfg;
#endif  /* CONFIG_BT_NIMBLE_ENABLED */

    esp_bt_audio_config_t bt_config = {
        .host_config = host_config,
        .event_cb = bt_audio_event_cb,
        .event_user_ctx = NULL,
#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
        .classic.a2dp_src_send_task_core_id = A2DP_SRC_SEND_TASK_CORE_ID,
        .classic.a2dp_src_send_task_prio = A2DP_SRC_SEND_TASK_PRIO,
        .classic.a2dp_src_send_task_stack_size = A2DP_SRC_SEND_TASK_STACK_SIZE,
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
#ifdef CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
        .le.vcp_rend.volume = 50,
        .le.vcp_rend.mute = 0,
        .le.vcp_rend.step = 10,
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */
    };
    setup_bt_audio_config_from_kconfig(&bt_config);
#if CONFIG_BT_CLASSIC_ENABLED && CONFIG_GMF_EXAMPLE_AUDIO_TECH_CLASSIC
    ESP_LOGI(TAG, "Classic Audio configuration:");
    ESP_LOGI(TAG, "  Roles: 0x%08" PRIX32, bt_config.classic.roles);
#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
    ESP_LOGI(TAG, "  A2DP source send task core ID: %u", bt_config.classic.a2dp_src_send_task_core_id);
    ESP_LOGI(TAG, "  A2DP source send task priority: %u", bt_config.classic.a2dp_src_send_task_prio);
    ESP_LOGI(TAG, "  A2DP source send task stack size: %u", bt_config.classic.a2dp_src_send_task_stack_size);
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
#endif  /* CONFIG_BT_CLASSIC_ENABLED && CONFIG_GMF_EXAMPLE_AUDIO_TECH_CLASSIC */
#if CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE
    ESP_LOGI(TAG, "LE Audio configuration:");
    ESP_LOGI(TAG, "  User case: %s",
             bt_config.le.user_case == ESP_BT_AUDIO_LE_USER_CASE_TMAP ? "TMAP" : "UNKNOWN");
    ESP_LOGI(TAG, "  Roles: 0x%08" PRIX32, bt_config.le.roles);
    ESP_LOGI(TAG, "  Sink count: %u, Source count: %u",
             bt_config.le.snk_cnt, bt_config.le.src_cnt);
    ESP_LOGI(TAG, "  Coordinate set size: %u", bt_config.le.csip.coordinate_set_size);
    ESP_LOGI(TAG, "  Coordinate set rank: %u", bt_config.le.csip.rank);
    ESP_LOGI(TAG, "  PACS sink locations: %s", le_audio_locations_to_str(bt_config.le.pacs.sink_locations));
#ifdef CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMS
    ESP_LOGI(TAG, "  Broadcast source name: %s", bt_config.le.bsrc.broadcast_name);
    ESP_LOGI(TAG, "  Broadcast source stream count: %u", bt_config.le.bsrc.stream_num);
#endif  /* CONFIG_GMF_EXAMPLE_LE_TMAP_ROLE_BMS */
#endif  /* CONFIG_GMF_EXAMPLE_AUDIO_TECH_LE */
    ESP_ERROR_CHECK(esp_bt_audio_init(&bt_config));
#if CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_HFP_AG)
    ESP_ERROR_CHECK(esp_bt_audio_classic_set_scan_mode(true, true));
#elif CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE)
    ESP_ERROR_CHECK(esp_bt_audio_classic_set_scan_mode(true, false));
#elif CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_A2DP_SINK)
    ESP_ERROR_CHECK(esp_bt_audio_classic_set_scan_mode(true, true));
#endif  /* CONFIG_BT_CLASSIC_ENABLED && defined(CONFIG_GMF_EXAMPLE_HFP_AG) */
    /* Initialize console for user interaction */
    cli_init();
}
