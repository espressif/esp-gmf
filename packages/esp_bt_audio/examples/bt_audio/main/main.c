/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdint.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_bt.h"

#include "nvs_flash.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_host.h"
#include "esp_gmf_pool.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_types.h"
#include "esp_bt_audio_media.h"
#include "esp_bt_audio_playback.h"
#include "esp_bt_audio_tel.h"
#include "esp_bt_audio_classic.h"
#include "esp_bt_audio.h"

#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "dev_audio_codec.h"

#include "cmd_reg.h"
#include "pool_reg.h"
#include "stream_proc.h"
#include "codec_defs.h"

#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
#define A2DP_SRC_SEND_TASK_CORE_ID     1
#define A2DP_SRC_SEND_TASK_PRIO        10
#define A2DP_SRC_SEND_TASK_STACK_SIZE  4096
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */

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
    return roles;
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
    ESP_LOGI(TAG, "Metadata: %s:\t%s",
             playback_metadata_type_to_str(event_data->type), event_data->length > 0 ? (const char *)event_data->value : "");
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
#if defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE) || defined(CONFIG_GMF_EXAMPLE_A2DP_SINK)
                esp_bt_audio_classic_set_scan_mode(false, false);
#endif  /* defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE) || defined(CONFIG_GMF_EXAMPLE_A2DP_SINK) */
            } else {
#if defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE)
                esp_bt_audio_classic_set_scan_mode(true, false);
#elif defined(CONFIG_GMF_EXAMPLE_A2DP_SINK)
                esp_bt_audio_classic_set_scan_mode(true, true);
#endif  /* defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE) || defined(CONFIG_GMF_EXAMPLE_A2DP_SINK) */
            }
            cli_bt_device_conn_st_chg(conn_st->addr, conn_st->connected);
            break;
        }
        case ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG: {
            esp_bt_audio_event_stream_st_t *stream_state = (esp_bt_audio_event_stream_st_t *)event_data;
            stream_proc_state_chg(stream_state->stream_handle, stream_state->state);
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
            uint8_t vol = vol_absolute->vol;
            bool mute = vol_absolute->mute;
            ESP_LOGI(TAG, "ESP_BT_AUDIO_EVENT_VOL_ABSOLUTE vol %d, mute %d, context %d", vol, mute, vol_absolute->context);
#if CONFIG_GMF_EXAMPLE_A2DP_SINK
            dev_audio_codec_handles_t *codec_handle = NULL;
            esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&codec_handle);
            esp_codec_dev_set_out_vol(codec_handle->codec_dev, vol);
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SINK */
            break;
        }
        case ESP_BT_AUDIO_EVENT_VOL_RELATIVE: {
            esp_bt_audio_event_vol_relative_t *vol_relative = (esp_bt_audio_event_vol_relative_t *)event_data;
            ESP_LOGI(TAG, "ESP_BT_AUDIO_EVENT_VOL_RELATIVE up_down %d, context %d", vol_relative->up_down, vol_relative->context);
#if CONFIG_GMF_EXAMPLE_A2DP_SINK
            dev_audio_codec_handles_t *codec_handle = NULL;
            esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&codec_handle);
            int current_volume = 0;
            esp_codec_dev_get_out_vol(codec_handle->codec_dev, &current_volume);
            if (vol_relative->up_down) {
                current_volume = (current_volume >= 90) ? 100 : current_volume + 10;
                esp_codec_dev_set_out_vol(codec_handle->codec_dev, current_volume);
            } else {
                current_volume = (current_volume <= 10) ? 0 : current_volume - 10;
                esp_codec_dev_set_out_vol(codec_handle->codec_dev, current_volume);
            }
            ESP_LOGI(TAG, "Volume set to %d", current_volume);
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SINK */
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

#ifdef CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY
    uint32_t btmode = ESP_BT_MODE_CLASSIC_BT;
#elif CONFIG_BTDM_CTRL_MODE_BLE_ONLY
    uint32_t btmode = ESP_BT_MODE_BLE;
#elif CONFIG_BTDM_CTRL_MODE_BTDM
    uint32_t btmode = ESP_BT_MODE_BTDM;
#else   /* CONFIG_BTDM_CTRL_MODE_BTDM */
    uint32_t btmode = ESP_BT_MODE_BLE;
#endif  /* CONFIG_BTDM_CTRL_MODE_BR_EDR_ONLY */
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(btmode));

    /* Initialize Bluetooth module */
    esp_bt_audio_host_bluedroid_cfg_t host_cfg = ESP_BT_AUDIO_HOST_BLUEDROID_CFG_DEFAULT();
    esp_bt_audio_config_t bt_config = {
        .host_config = &host_cfg,
        .event_cb = bt_audio_event_cb,
        .event_user_ctx = NULL,
#ifdef CONFIG_BT_CLASSIC_ENABLED
        .classic.roles = get_classic_roles(),
#ifdef CONFIG_GMF_EXAMPLE_A2DP_SOURCE
        .classic.a2dp_src_send_task_core_id = A2DP_SRC_SEND_TASK_CORE_ID,
        .classic.a2dp_src_send_task_prio = A2DP_SRC_SEND_TASK_PRIO,
        .classic.a2dp_src_send_task_stack_size = A2DP_SRC_SEND_TASK_STACK_SIZE,
#endif  /* CONFIG_GMF_EXAMPLE_A2DP_SOURCE */
#endif  /* CONFIG_BT_CLASSIC_ENABLED */
    };
    ESP_ERROR_CHECK(esp_bt_audio_init(&bt_config));
#if defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE)
    ESP_ERROR_CHECK(esp_bt_audio_classic_set_scan_mode(true, false));
#elif defined(CONFIG_GMF_EXAMPLE_A2DP_SINK)
    ESP_ERROR_CHECK(esp_bt_audio_classic_set_scan_mode(true, true));
    ESP_ERROR_CHECK(esp_bt_audio_playback_reg_notifications(ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_END |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_START |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_NOW_PLAYING_CHANGE |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_AVAILABLE_PLAYERS_CHANGE |
                                                            ESP_BT_AUDIO_PLAYBACK_EVENT_ADDRESSED_PLAYER_CHANGE));
#endif  /* defined(CONFIG_GMF_EXAMPLE_A2DP_SOURCE) || defined(CONFIG_GMF_EXAMPLE_A2DP_SINK) */

    /* Initialize console for user interaction */
    cli_init();
}
