/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * This module provides the remote control and metadata interface for Bluetooth
 * audio playback. In the typical scenario the device acts as A2DP Sink (e.g.
 * headset or speaker) and sends play/pause/stop and track-skip commands to the
 * remote A2DP Source (the playback device). It can also request track metadata
 * (title, artist, album, etc.) and register for remote notifications (play status,
 * track change, position, now playing, etc.).
 */

/**
 * @brief  Cover art image data
 */
typedef struct {
    uint32_t  format_fourcc;  /*!< Image format fourcc */
    uint32_t  size;           /*!< Image size in bytes */
    uint8_t  *data;           /*!< Image data */
} esp_bt_audio_playback_cover_art_t;

/**
 * @brief  Enumeration for Bluetooth audio metadata masks
 */
typedef enum {
    ESP_BT_AUDIO_PLAYBACK_METADATA_TITLE        = 0x1,   /*!< title of the playing track */
    ESP_BT_AUDIO_PLAYBACK_METADATA_ARTIST       = 0x2,   /*!< track artist */
    ESP_BT_AUDIO_PLAYBACK_METADATA_ALBUM        = 0x4,   /*!< album name */
    ESP_BT_AUDIO_PLAYBACK_METADATA_TRACK_NUM    = 0x8,   /*!< track position on the album */
    ESP_BT_AUDIO_PLAYBACK_METADATA_NUM_TRACKS   = 0x10,  /*!< number of tracks on the album */
    ESP_BT_AUDIO_PLAYBACK_METADATA_GENRE        = 0x20,  /*!< track genre */
    ESP_BT_AUDIO_PLAYBACK_METADATA_PLAYING_TIME = 0x40,  /*!< total album playing time in milliseconds */
    ESP_BT_AUDIO_PLAYBACK_METADATA_COVER_ART    = 0x80,  /*!< cover art image: `esp_bt_audio_playback_cover_art_t` */
} esp_bt_audio_playback_metadata_mask_t;

/**
 * @brief  Enumeration for Bluetooth audio playback status
 */
typedef enum {
    ESP_BT_AUDIO_PLAYBACK_STATUS_STOPPED  = 0,     /*!< stopped */
    ESP_BT_AUDIO_PLAYBACK_STATUS_PLAYING  = 1,     /*!< playing */
    ESP_BT_AUDIO_PLAYBACK_STATUS_PAUSED   = 2,     /*!< paused */
    ESP_BT_AUDIO_PLAYBACK_STATUS_FWD_SEEK = 3,     /*!< forward seek */
    ESP_BT_AUDIO_PLAYBACK_STATUS_REV_SEEK = 4,     /*!< reverse seek */
    ESP_BT_AUDIO_PLAYBACK_STATUS_ERROR    = 0xFF,  /*!< error */
} esp_bt_audio_playback_status_t;

/**
 * @brief  Enumeration for Bluetooth audio playback remote notification event masks
 */
typedef enum {
    ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_STATUS_CHANGE       = 0x01,  /*!< track status change, eg. from playing to paused */
    ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_CHANGE             = 0x02,  /*!< new track is loaded */
    ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_END        = 0x04,  /*!< current track reached end */
    ESP_BT_AUDIO_PLAYBACK_EVENT_TRACK_REACHED_START      = 0x08,  /*!< current track reached start position */
    ESP_BT_AUDIO_PLAYBACK_EVENT_PLAY_POS_CHANGED         = 0x10,  /*!< track playing position changed */
    ESP_BT_AUDIO_PLAYBACK_EVENT_NOW_PLAYING_CHANGE       = 0x20,  /*!< now playing content changed */
    ESP_BT_AUDIO_PLAYBACK_EVENT_AVAILABLE_PLAYERS_CHANGE = 0x40,  /*!< available players changed */
    ESP_BT_AUDIO_PLAYBACK_EVENT_ADDRESSED_PLAYER_CHANGE  = 0x80,  /*!< the addressed player changed */
} esp_bt_audio_playback_event_mask_t;

/**
 * @brief  Send a command to the Bluetooth audio playback device to start playback.
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_playback_play(void);

/**
 * @brief  Send a command to the Bluetooth audio playback device to pause playback.
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_playback_pause(void);

/**
 * @brief  Send a command to the Bluetooth audio playback device to stop playback.
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_playback_stop(void);

/**
 * @brief  Send a command to the Bluetooth audio playback device to skip to next track.
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_playback_next(void);

/**
 * @brief  Send a command to the Bluetooth audio playback device to skip to previous track.
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_playback_prev(void);

/**
 * @brief  Send a command to the Bluetooth audio playback device to request metadata.
 *
 * @param[in]  mask  Mask of metadata to request
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_playback_request_metadata(uint32_t mask);

/**
 * @brief  Register remote notifications.
 *
 * @param[in]  mask  Mask of remote notifications to register
 *
 * @return
 *       - Mask  of registered remote notifications
 */
esp_err_t esp_bt_audio_playback_reg_notifications(uint32_t mask);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
