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

#define ESP_BT_AUDIO_FOURCC_TO_INT(a, b, c, d)  ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

#define ESP_BT_AUDIO_FOURCC_PNG   ESP_BT_AUDIO_FOURCC_TO_INT('P', 'N', 'G', ' ')  /* Portable Network Graphics */
#define ESP_BT_AUDIO_FOURCC_JPEG  ESP_BT_AUDIO_FOURCC_TO_INT('J', 'P', 'E', 'G')  /* JPEG File Interchange Format (JFIF) */
#define ESP_BT_AUDIO_FOURCC_GIF   ESP_BT_AUDIO_FOURCC_TO_INT('G', 'I', 'F', ' ')  /* Graphics Interchange Format */
#define ESP_BT_AUDIO_FOURCC_WEBP  ESP_BT_AUDIO_FOURCC_TO_INT('W', 'E', 'B', 'P')  /* WebP */
#define ESP_BT_AUDIO_FOURCC_BMP   ESP_BT_AUDIO_FOURCC_TO_INT('B', 'M', 'P', ' ')  /* Bitmap */

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
    ESP_BT_AUDIO_CLASSIC_ROLE_PBAP_PCE = 0x0040,  /*!< Phone book client equipment role */
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

/**
 * @brief  Enumeration for LE Audio roles
 */
typedef enum {
    ESP_BT_AUDIO_LE_ROLE_UNICAST_SERVER   = 0x0001,  /*!< LE Audio Unicast Server role */
    ESP_BT_AUDIO_LE_ROLE_BROADCAST_SINK   = 0x0002,  /*!< LE Audio Broadcast Sink role */
    ESP_BT_AUDIO_LE_ROLE_BROADCAST_SOURCE = 0x0004,  /*!< LE Audio Broadcast Source role */
    ESP_BT_AUDIO_LE_ROLE_SCAN_DELEGATOR   = 0x0008,  /*!< LE Audio Scan Delegator role */
} esp_bt_audio_le_role_t;

/**
 * @brief  Enumeration for LE Audio use cases
 */
typedef enum {
    ESP_BT_AUDIO_LE_USER_CASE_UNKNOWN,  /*!< Unknown user case */
    ESP_BT_AUDIO_LE_USER_CASE_TMAP,     /*!< Telephony and Media Audio Profile */
    ESP_BT_AUDIO_LE_USER_CASE_HAP,      /*!< Hearing Access Profile */
    ESP_BT_AUDIO_LE_USER_CASE_PBP,      /*!< Public Broadcast Profile */
} esp_bt_audio_le_user_case_t;

/**
 * @brief  Structure for LE Audio PACS server configuration
 */
typedef struct {
    uint8_t   sink_enabled;         /*!< Sink capability enabled flag */
    uint32_t  sink_context_mask;    /*!< Sink context mask */
    uint32_t  sink_locations;       /*!< Sink locations */
    uint8_t   source_enabled;       /*!< Source capability enabled flag */
    uint32_t  source_context_mask;  /*!< Source context mask */
    uint32_t  source_locations;     /*!< Source locations */
} esp_bt_audio_le_pacs_cfg_t;

/**
 * @brief  Structure for LE Audio CSIP set member configuration
 */
typedef struct {
    uint8_t  coordinate_set_size;  /*!< Coordinated set size */
    uint8_t  rank;                 /*!< Set member rank, starting at 1 */
    uint8_t  sirk[16];             /*!< Set identity resolving key */
} esp_bt_audio_le_csip_cfg_t;

/**
 * @brief  Structure for LE Audio VCP volume renderer configuration
 */
typedef struct {
    uint8_t  step;    /*!< Volume control step size */
    uint8_t  mute;    /*!< Initial mute state */
    uint8_t  volume;  /*!< Initial volume level */
} esp_bt_audio_le_vcp_cfg_t;

/**
 * @brief  Structure for LE Audio Broadcast Source configuration
 */
typedef struct {
    uint8_t  broadcast_code[16];  /*!< Broadcast code */
    uint8_t  broadcast_name[32];  /*!< Broadcast name */
    uint8_t  stream_num;          /*!< Number of streams */
} esp_bt_audio_le_bsrc_cfg_t;

/**
 * @brief  Structure for LE Audio configuration
 */
typedef struct {
    uint32_t                    roles;      /*!< LE roles for the selected user case, e.g. ESP_BLE_AUDIO_TMAP_ROLE_* for TMAP */
    uint32_t                    user_case;  /*!< LE Audio use case, e.g. esp_bt_audio_le_user_case_t */
    uint8_t                     snk_cnt;    /*!< Number of sink ASEs to register as unicast server */
    uint8_t                     src_cnt;    /*!< Number of source ASEs to register as unicast server */
    esp_bt_audio_le_pacs_cfg_t  pacs;       /*!< PACS configuration */
    esp_bt_audio_le_csip_cfg_t  csip;       /*!< CSIP set member configuration */
    esp_bt_audio_le_vcp_cfg_t   vcp;        /*!< VCP volume renderer configuration */
    esp_bt_audio_le_bsrc_cfg_t  bsrc;       /*!< Broadcast source configuration */
} esp_bt_audio_le_cfg_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
