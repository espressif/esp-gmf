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
#define ESP_BT_AUDIO_LE_BSRC_STREAM_MAX     (2)

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
 * @brief  Structure for LE Audio VCP renderer configuration
 */
typedef struct {
    uint8_t  step;    /*!< Volume control step size */
    uint8_t  mute;    /*!< Initial mute state */
    uint8_t  volume;  /*!< Initial volume level */
} esp_bt_audio_le_vcp_rend_cfg_t;

/**
 * @brief  BAP LC3 broadcast source presets
 *
 *         Presets are ordered by sample rate, frame duration, frame size, and
 *         reliability (`HQ` / `HR`). `DEFAULT` preserves the historical 48 kHz,
 *         10 ms, 100-byte high-quality configuration.
 */
typedef enum {
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_DEFAULT = 0,                  /*!< 48 kHz, 10 ms, 100 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_8KHZ_7_5MS_26B_HQ,            /*!< 8 kHz, 7.5 ms, 26 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_8KHZ_7_5MS_26B_HR,            /*!< 8 kHz, 7.5 ms, 26 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_8KHZ_10MS_30B_HQ,             /*!< 8 kHz, 10 ms, 30 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_8KHZ_10MS_30B_HR,             /*!< 8 kHz, 10 ms, 30 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_16KHZ_7_5MS_30B_HQ,           /*!< 16 kHz, 7.5 ms, 30 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_16KHZ_7_5MS_30B_HR,           /*!< 16 kHz, 7.5 ms, 30 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_16KHZ_10MS_40B_HQ,            /*!< 16 kHz, 10 ms, 40 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_16KHZ_10MS_40B_HR,            /*!< 16 kHz, 10 ms, 40 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_24KHZ_7_5MS_45B_HQ,           /*!< 24 kHz, 7.5 ms, 45 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_24KHZ_7_5MS_45B_HR,           /*!< 24 kHz, 7.5 ms, 45 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_24KHZ_10MS_60B_HQ,            /*!< 24 kHz, 10 ms, 60 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_24KHZ_10MS_60B_HR,            /*!< 24 kHz, 10 ms, 60 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_32KHZ_7_5MS_60B_HQ,           /*!< 32 kHz, 7.5 ms, 60 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_32KHZ_7_5MS_60B_HR,           /*!< 32 kHz, 7.5 ms, 60 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_32KHZ_10MS_80B_HQ,            /*!< 32 kHz, 10 ms, 80 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_32KHZ_10MS_80B_HR,            /*!< 32 kHz, 10 ms, 80 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_44_1KHZ_7_5MS_97B_HQ,         /*!< 44.1 kHz, 7.5 ms, 97 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_44_1KHZ_7_5MS_97B_HR,         /*!< 44.1 kHz, 7.5 ms, 97 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_44_1KHZ_10MS_130B_HQ,         /*!< 44.1 kHz, 10 ms, 130 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_44_1KHZ_10MS_130B_HR,         /*!< 44.1 kHz, 10 ms, 130 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_75B_HQ,           /*!< 48 kHz, 7.5 ms, 75 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_75B_HR,           /*!< 48 kHz, 7.5 ms, 75 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_90B_HQ,           /*!< 48 kHz, 7.5 ms, 90 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_90B_HR,           /*!< 48 kHz, 7.5 ms, 90 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_117B_HQ,          /*!< 48 kHz, 7.5 ms, 117 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_7_5MS_117B_HR,          /*!< 48 kHz, 7.5 ms, 117 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_100B_HQ,           /*!< 48 kHz, 10 ms, 100 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_100B_HR,           /*!< 48 kHz, 10 ms, 100 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_120B_HQ,           /*!< 48 kHz, 10 ms, 120 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_120B_HR,           /*!< 48 kHz, 10 ms, 120 bytes, high reliability */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_155B_HQ,           /*!< 48 kHz, 10 ms, 155 bytes, high quality */
    ESP_BT_AUDIO_LE_BSRC_LC3_PRESET_48KHZ_10MS_155B_HR,           /*!< 48 kHz, 10 ms, 155 bytes, high reliability */
} esp_bt_audio_le_bsrc_lc3_preset_t;

/**
 * @brief  Structure for LE Audio Broadcast Source configuration
 */
typedef struct {
    uint8_t                            broadcast_code[16];                                 /*!< Broadcast code string bytes, zero-padded */
    uint8_t                            broadcast_name[32];                                 /*!< Broadcast name */
    uint8_t                            stream_num;                                         /*!< Number of BIS streams in one BIG, from 1 to CONFIG_BT_BAP_BROADCAST_SRC_STREAM_COUNT. 0 is rejected */
    uint32_t                           stream_locations[ESP_BT_AUDIO_LE_BSRC_STREAM_MAX];  /*!< Unique single location for the first ESP_BT_AUDIO_LE_BSRC_STREAM_MAX BIS (ESP_BT_AUDIO_AUDIO_LOC_*). 0, and any extra BIS, use mono */
    esp_bt_audio_le_bsrc_lc3_preset_t  lc3_preset;                                         /*!< LC3 broadcast preset. 0 uses 48 kHz, 10 ms, 100-byte HQ */
} esp_bt_audio_le_bsrc_cfg_t;

/**
 * @brief  Structure for LE Audio configuration
 */
typedef struct {
    uint32_t                        roles;                     /*!< LE roles for the selected user case, e.g. ESP_BLE_AUDIO_TMAP_ROLE_* for TMAP */
    uint32_t                        user_case;                 /*!< LE Audio use case, e.g. esp_bt_audio_le_user_case_t */
    uint8_t                         snk_cnt;                   /*!< Number of sink ASEs to register as unicast server */
    uint8_t                         src_cnt;                   /*!< Number of source ASEs to register as unicast server */
    uint8_t                         src_send_task_core_id;     /*!< LE source send task core ID (0 or 1) */
    uint8_t                         src_send_task_prio;        /*!< LE source send task priority. Must be less than 24 */
    uint32_t                        src_send_task_stack_size;  /*!< LE source send task stack size in bytes. Must be greater than 0 */
    esp_bt_audio_le_pacs_cfg_t      pacs;                      /*!< PACS configuration */
    esp_bt_audio_le_csip_cfg_t      csip;                      /*!< CSIP set member configuration */
    esp_bt_audio_le_vcp_rend_cfg_t  vcp_rend;                  /*!< VCP renderer configuration */
    esp_bt_audio_le_bsrc_cfg_t      bsrc;                      /*!< Broadcast source configuration */
} esp_bt_audio_le_cfg_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
