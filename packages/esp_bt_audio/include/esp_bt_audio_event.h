/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */
#pragma once

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_stream.h"
#include "esp_bt_audio_host.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Enumeration for Bluetooth events
 */
typedef enum {
    ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG,  /*!< Connection state changed event, the event data is esp_bt_audio_event_connection_st_t */
    ESP_BT_AUDIO_EVENT_DISCOVERY_STATE_CHG,   /*!< Discovery state changed event, the event data is esp_bt_audio_event_discovery_st_t */
    ESP_BT_AUDIO_EVENT_DEVICE_DISCOVERED,     /*!< Device discovered event, the event data is esp_bt_audio_event_device_discovered_t */
    ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG,      /*!< Stream state changed event, the event data is esp_bt_audio_event_stream_st_t */
    ESP_BT_AUDIO_EVENT_MEDIA_CTRL_CMD,        /*!< Media control command received event, the event data is esp_bt_audio_event_media_ctrl_t */
    ESP_BT_AUDIO_EVENT_PLAYBACK_STATUS_CHG,   /*!< Playback status changed event, the event data is esp_bt_audio_event_playback_status_t */
    ESP_BT_AUDIO_EVENT_PLAYBACK_METADATA,     /*!< Playback metadata event, the event data is esp_bt_audio_event_playback_metadata_t */
    ESP_BT_AUDIO_EVENT_VOL_ABSOLUTE,          /*!< Absolute volume set event, the event data is esp_bt_audio_event_vol_absolute_t */
    ESP_BT_AUDIO_EVENT_VOL_RELATIVE,          /*!< Relative volume set event, the event data is esp_bt_audio_event_vol_relative_t */
    ESP_BT_AUDIO_EVENT_TEL_STATUS_CHG,        /*!< Telephony status changed event, the event data is esp_bt_audio_event_tel_status_chg_t */
    ESP_BT_AUDIO_EVENT_CALL_STATE_CHG,        /*!< Call state changed event, the event data is esp_bt_audio_event_call_state_t */
    ESP_BT_AUDIO_EVENT_PHONEBOOK_COUNT,       /*!< Phonebook count event, the event data is uint16_t */
    ESP_BT_AUDIO_EVENT_PHONEBOOK_ENTRY,       /*!< Phonebook entry event, the event data is esp_bt_audio_pb_entry_t */
    ESP_BT_AUDIO_EVENT_PHONEBOOK_HISTORY,     /*!< Phonebook history event, the event data is esp_bt_audio_pb_history_t */
} esp_bt_audio_event_t;

/**
 * @brief  Connection state changed event data
 */
typedef struct {
    esp_bt_audio_tech_t  tech;       /*!< Bluetooth technology (Classic/LE) */
    bool                 connected;  /*!< Connection state */
    uint8_t              addr[6];    /*!< Remote device Bluetooth address */
} esp_bt_audio_event_connection_st_t;

/**
 * @brief  Discovery state changed event data
 */
typedef struct {
    esp_bt_audio_tech_t  tech;         /*!< Bluetooth technology (Classic/LE) */
    bool                 discovering;  /*!< Discovery state */
} esp_bt_audio_event_discovery_st_t;

/**
 * @brief  Device discovered event data
 */
typedef struct {
    esp_bt_audio_tech_t  tech;                                      /*!< Bluetooth technology (Classic/LE) */
    uint8_t              addr[6];                                   /*!< Bluetooth device address */
    char                 name[ESP_BT_AUDIO_HOST_MAX_DEV_NAME_LEN];  /*!< Device name (null-terminated string) */
    int32_t              rssi;                                      /*!< Signal strength (RSSI) */
    union {
        struct {
            uint32_t  cod;   /*!< Class of Device (for Classic Bluetooth only) */
        } classic;           /*!< Classic Bluetooth specific discovery data */
        uint32_t  reserved;  /*!< Reserved for future use */
    } disc_data;             /*!< Discovery data union */
} esp_bt_audio_event_device_discovered_t;

/**
 * @brief  Stream state changed event data
 */
typedef struct {
    esp_bt_audio_stream_handle_t  stream_handle;  /*!< Stream handle */
    esp_bt_audio_stream_state_t   state;          /*!< Stream state */
} esp_bt_audio_event_stream_st_t;

/**
 * @brief  Media control command received event data
 */
typedef struct {
    esp_bt_audio_media_ctrl_cmd_t  cmd;  /*!< Media control command */
} esp_bt_audio_event_media_ctrl_t;

/**
 * @brief  Playback state changed event data
 */
typedef struct {
    uint32_t  event;  /*!< Playback event: `esp_bt_audio_playback_event_mask_t` */
    union {
        uint32_t  play_status;  /*!< Playback status, data for `ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE` */
        uint32_t  position;     /*!< Playback position in milliseconds, data for `ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED` */
    } evt_param;
} esp_bt_audio_event_playback_st_t;

/**
 * @brief  Playback metadata event data
 */
typedef struct {
    uint32_t  type;    /*!< Metadata type: `esp_bt_audio_playback_metadata_mask_t`*/
    uint32_t  length;  /*!< Metadata value length */
    uint8_t  *value;   /*!< Metadata value */
} esp_bt_audio_event_playback_metadata_t;

/**
 * @brief  Absolute volume set event data
 */
typedef struct {
    esp_bt_audio_stream_context_t  context;  /*!< Audio context */
    bool                           mute;     /*!< Volume mute state: true if muted */
    uint8_t                        vol;      /*!< Volume level */
} esp_bt_audio_event_vol_absolute_t;

/**
 * @brief  Relative volume set event data
 */
typedef struct {
    esp_bt_audio_stream_context_t  context;  /*!< Audio context */
    bool                           up_down;  /*!< Volume up/down direction: true: up, false: down */
} esp_bt_audio_event_vol_relative_t;

/**
 * @brief  Callback function type for Bluetooth events
 *
 * @param[in]  event       Event ID
 * @param[in]  event_data  Pointer to event data
 * @param[in]  user_data   User data pointer
 */
typedef void (*esp_bt_audio_event_cb_t)(esp_bt_audio_event_t event, void *event_data, void *user_data);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
