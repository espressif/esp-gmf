/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Audio source types enumeration
 */
typedef enum {
    AUDIO_SRC_HTTP   = 0,  /*!< HTTP stream source */
    AUDIO_SRC_SDCARD = 1,  /*!< SD card file source */
    AUDIO_SRC_FLASH  = 2,  /*!< Flash (embed) source */
    AUDIO_SRC_MAX    = 3   /*!< Number of source types */
} audio_source_t;

/**
 * @brief  Audio player state
 */
typedef enum {
    AUDIO_STATE_IDLE    = 0,  /*!< Idle state */
    AUDIO_STATE_PLAYING = 1,  /*!< Playing state */
    AUDIO_STATE_PAUSED  = 2,  /*!< Paused state */
    AUDIO_STATE_STOPPED = 3,  /*!< Stopped state */
    AUDIO_STATE_ERROR   = 4   /*!< Error state */
} audio_state_t;

/**
 * @brief  Audio multi-source player error codes
 */
typedef enum {
    AUDIO_MS_PLAYER_OK                  = 0,  /*!< Success */
    AUDIO_MS_PLAYER_ERR_INVALID_ARG     = 1,  /*!< Invalid argument */
    AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED = 2,  /*!< Player not initialized */
    AUDIO_MS_PLAYER_ERR_ALREADY_PLAYING = 3,  /*!< Same source already playing */
    AUDIO_MS_PLAYER_ERR_NOT_PLAYING     = 4,  /*!< Not in playing/paused state */
    AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL   = 5,  /*!< Pipeline/IO/codec failed */
    AUDIO_MS_PLAYER_ERR_UNKNOWN_SOURCE  = 6,  /*!< Unknown audio source */
    AUDIO_MS_PLAYER_ERR_TIMEOUT         = 7   /*!< Operation timeout */
} audio_ms_player_err_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
