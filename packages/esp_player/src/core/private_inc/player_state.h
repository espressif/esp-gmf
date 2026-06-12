/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_gmf_data_bus.h"
#include "esp_gmf_element.h"
#include "esp_gmf_err.h"
#include "esp_gmf_event.h"
#include "esp_gmf_task.h"

#include "esp_player_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct esp_player_stream esp_player_stream_t;

/**
 * @brief  Player main state enumeration
 */
typedef enum {
    PLAYER_STATE_IDLE      = 0,  /*!< Idle: not yet configured (no URL) or freshly init-ed */
    PLAYER_STATE_PREPARING = 1,  /*!< Preparing: user requested run, pipelines are being brought up but haven't reached RUNNING yet */
    PLAYER_STATE_PLAYING   = 2,  /*!< Playing: all expected tasks are RUNNING and rendering is happening */
    PLAYER_STATE_PAUSED    = 3,  /*!< Paused: render tasks are suspended */
    PLAYER_STATE_STOPPED   = 4,  /*!< Stopped: playback explicitly stopped by user */
    PLAYER_STATE_FINISHED  = 5,  /*!< Finished: stream reached end-of-media naturally */
    PLAYER_STATE_ERROR     = 6,  /*!< Error: sticky state after a pipeline-level failure */
} esp_player_state_t;

/* -------- Commands and state machine (player_state.c) -------- */

/**
 * @brief  State transition event enumeration
 */
typedef enum {
    ESP_PLAYER_CMD_PREPARE           = 0,   /*!< Request: start preparing playback (user-initiated run) */
    ESP_PLAYER_CMD_PLAYING           = 1,   /*!< Event: pipelines reached RUNNING, playback actually started */
    ESP_PLAYER_CMD_PAUSE             = 2,   /*!< Pause event */
    ESP_PLAYER_CMD_RESUME            = 3,   /*!< Resume event */
    ESP_PLAYER_CMD_SEEK              = 4,   /*!< Seek event */
    ESP_PLAYER_CMD_STOP              = 5,   /*!< Stop event */
    ESP_PLAYER_CMD_QUIT              = 6,   /*!< Quit event */
    ESP_PLAYER_CMD_ERROR             = 7,   /*!< Error event */
    ESP_PLAYER_CMD_FINISHED          = 8,   /*!< Stream finished */
    ESP_PLAYER_CMD_REPORT_AUDIO_INFO = 9,   /*!< Report audio info event */
    ESP_PLAYER_CMD_REPORT_VIDEO_INFO = 10,  /*!< Report video info event */
    ESP_PLAYER_CMD_REPORT_TRACK_INFO = 11,  /*!< Report track info event */
} esp_player_cmd_type_t;

typedef struct {
    esp_player_cmd_type_t  cmd_type;  /*!< Command type */
    void                  *data;      /*!< Command data */
    uint32_t               data_len;  /*!< Command data length */
} esp_player_cmd_msg_t;

/**
 * @brief  Initialize state machine module (cache path ops)
 */
void player_state_init(void);

/**
 * @brief  Handle state machine command
 */
esp_player_err_t handle_state_cmd(esp_player_stream_t *stream, esp_player_cmd_msg_t *cmd);

/**
 * @brief  Send command to player
 */
esp_player_err_t player_send_cmd(esp_player_stream_t *stream, esp_player_cmd_msg_t *cmd);

/**
 * @brief  Get state name
 */
const char *get_state_name(esp_player_state_t state);

/**
 * @brief  Get command type name
 */
const char *get_cmd_name(esp_player_cmd_type_t cmd_type);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
