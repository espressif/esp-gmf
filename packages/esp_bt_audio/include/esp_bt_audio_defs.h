/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */
#pragma once

#include "esp_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT   (0x01)
#define ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT  (0x02)

/**
 * @brief  Enumeration for Bluetooth audio technologies
 */
typedef enum {
    ESP_BT_AUDIO_TECH_UNKNOWN,  /*!< Unknown audio technology */
    ESP_BT_AUDIO_TECH_CLASSIC,  /*!< Classic Bluetooth */
    ESP_BT_AUDIO_TECH_LE,       /*!< LE Audio */
} esp_bt_audio_tech_t;

/**
 * @brief  Enumeration for Bluetooth media control commands
 */
typedef enum {
    ESP_BT_AUDIO_MEDIA_CTRL_CMD_UNKNOWN,  /*!< Unknown command */
    ESP_BT_AUDIO_MEDIA_CTRL_CMD_PLAY,     /*!< Play command */
    ESP_BT_AUDIO_MEDIA_CTRL_CMD_PAUSE,    /*!< Pause command */
    ESP_BT_AUDIO_MEDIA_CTRL_CMD_STOP,     /*!< Stop command */
    ESP_BT_AUDIO_MEDIA_CTRL_CMD_NEXT,     /*!< Next track command */
    ESP_BT_AUDIO_MEDIA_CTRL_CMD_PREV,     /*!< Previous track command */
} esp_bt_audio_media_ctrl_cmd_t;

/**
 * @brief  Enumeration for Classic Bluetooth roles
 */
typedef enum {
    ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC = 0x0001,  /*!< A2DP Source role */
    ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SNK = 0x0002,  /*!< A2DP Sink role */
    ESP_BT_AUDIO_CLASSIC_ROLE_HFP_HF   = 0x0004,  /*!< HFP Hands-Free role */
    ESP_BT_AUDIO_CLASSIC_ROLE_HFP_AG   = 0x0008,  /*!< HFP Audio Gateway role */
    ESP_BT_AUDIO_CLASSIC_ROLE_AVRC_CT  = 0x0010,  /*!< AVRCP Controller role */
    ESP_BT_AUDIO_CLASSIC_ROLE_AVRC_TG  = 0x0020,  /*!< AVRCP Target role */
} esp_bt_audio_role_t;

/**
 * @brief  Structure for Classic Bluetooth configuration
 */
typedef struct {
    uint32_t  roles;                          /*!< Enabled classic profiles/roles. Bitwise OR of esp_bt_audio_role_t */
    uint8_t   a2dp_src_send_task_core_id;     /*!< A2DP source send task core ID (0 or 1). If invalid, defaults to 0 */
    uint8_t   a2dp_src_send_task_prio;        /*!< A2DP source send task priority. If 0, defaults to 10 */
    uint32_t  a2dp_src_send_task_stack_size;  /*!< A2DP source send task stack size in bytes. If 0, defaults to 4096 */
} esp_bt_audio_classic_cfg_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
