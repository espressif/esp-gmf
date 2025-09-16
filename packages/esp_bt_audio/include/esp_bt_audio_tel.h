/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_host.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/** @brief  Maximum number of calls that can be tracked */
#define ESP_BT_AUDIO_CALL_MAX_NUM  (8)

/** @brief  Maximum length of call URI/number string */
#define ESP_BT_AUDIO_CALL_URI_MAX_LEN  (95)

/**
 * @brief  Call state
 */
typedef enum {
    ESP_BT_AUDIO_CALL_STATE_INACTIVE = 0,               /*!< No call for this index */
    ESP_BT_AUDIO_CALL_STATE_INCOMING,                   /*!< Incoming call */
    ESP_BT_AUDIO_CALL_STATE_DIALING,                    /*!< Outgoing call dialing */
    ESP_BT_AUDIO_CALL_STATE_ALERTING,                   /*!< Outgoing call alerting */
    ESP_BT_AUDIO_CALL_STATE_ACTIVE,                     /*!< Active call */
    ESP_BT_AUDIO_CALL_STATE_LOCALLY_HELD,               /*!< Call locally held */
    ESP_BT_AUDIO_CALL_STATE_REMOTELY_HELD,              /*!< Call remotely held */
    ESP_BT_AUDIO_CALL_STATE_LOCALLY_AND_REMOTELY_HELD,  /*!< Call locally and remotely held */
} esp_bt_audio_call_state_t;

/**
 * @brief  Call direction
 */
typedef enum {
    ESP_BT_AUDIO_CALL_DIR_OUTGOING = 0,  /*!< Outgoing call */
    ESP_BT_AUDIO_CALL_DIR_INCOMING,      /*!< Incoming call */
} esp_bt_audio_call_dir_t;

/**
 * @brief  Call state changed event payload
 */
typedef struct {
    esp_bt_audio_tech_t        tech;                                    /*!< Bluetooth technology (Classic/LE) */
    uint8_t                    idx;                                     /*!< Call index */
    esp_bt_audio_call_dir_t    dir;                                     /*!< Call direction */
    esp_bt_audio_call_state_t  state;                                   /*!< Call state */
    char                       uri[ESP_BT_AUDIO_CALL_URI_MAX_LEN + 1];  /*!< URI/number string of the call (null-terminated) */
} esp_bt_audio_event_call_state_t;

/**
 * @brief  Telephony status change event type
 */
typedef enum {
    ESP_BT_AUDIO_TEL_STATUS_BATTERY,          /*!< Battery level/state */
    ESP_BT_AUDIO_TEL_STATUS_SIGNAL_STRENGTH,  /*!< Signal strength */
    ESP_BT_AUDIO_TEL_STATUS_ROAMING,          /*!< Roaming status */
    ESP_BT_AUDIO_TEL_STATUS_NETWORK,          /*!< Network/service availability */
    ESP_BT_AUDIO_TEL_STATUS_OPERATOR,         /*!< Current operator name */
    ESP_BT_AUDIO_TEL_STATUS_MAX,
} esp_bt_audio_tel_event_t;

/**
 * @brief  Battery level event payload
 */
typedef struct {
    uint8_t  level;        /*!< Battery level 0-100 (percent) */
    uint8_t  instance_id;  /*!< BAS instance/bearer id if available, otherwise 0 */
} esp_bt_audio_tel_status_battery_t;

/**
 * @brief  Signal strength event payload
 */
typedef struct {
    int8_t  value;  /*!< Signal strength (e.g. 0-5) */
} esp_bt_audio_tel_status_signal_t;

/**
 * @brief  Roaming status event payload
 */
typedef struct {
    bool  active;  /*!< true: roaming active, false: inactive */
} esp_bt_audio_tel_status_roaming_t;

/**
 * @brief  Network availability event payload
 */
typedef struct {
    bool  available;  /*!< true: service available, false: unavailable */
} esp_bt_audio_tel_status_network_t;

/**
 * @brief  Operator name event payload
 */
typedef struct {
    char  name[ESP_BT_AUDIO_HOST_MAX_DEV_NAME_LEN];  /*!< Operator name string (null-terminated) */
} esp_bt_audio_tel_status_operator_t;

/**
 * @brief  Union of telephony status change event data
 */
typedef union {
    esp_bt_audio_tel_status_battery_t   battery;          /*!< For ESP_BT_AUDIO_TEL_STATUS_BATTERY */
    esp_bt_audio_tel_status_signal_t    signal_strength;  /*!< For ESP_BT_AUDIO_TEL_STATUS_SIGNAL_STRENGTH */
    esp_bt_audio_tel_status_roaming_t   roaming;          /*!< For ESP_BT_AUDIO_TEL_STATUS_ROAMING */
    esp_bt_audio_tel_status_network_t   network;          /*!< For ESP_BT_AUDIO_TEL_STATUS_NETWORK */
    esp_bt_audio_tel_status_operator_t  operator_name;    /*!< For ESP_BT_AUDIO_TEL_STATUS_OPERATOR */
} esp_bt_audio_tel_status_event_t;

/**
 * @brief  Telephony status changed event payload
 */
typedef struct {
    esp_bt_audio_tech_t              tech;  /*!< Bluetooth technology (Classic/LE) */
    esp_bt_audio_tel_event_t         type;  /*!< Telephony status type */
    esp_bt_audio_tel_status_event_t  data;  /*!< Status payload */
} esp_bt_audio_event_tel_status_chg_t;

/**
 * @brief  Answer an incoming call
 *
 * @param[in]  idx  Call index (ignored for classic Bluetooth)
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_call_answer(uint8_t idx);

/**
 * @brief  Reject an incoming call or hang up current call
 *
 * @param[in]  idx  Call index (ignored for classic Bluetooth)
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_call_reject(uint8_t idx);

/**
 * @brief  Place a call with a specified number
 *
 * @param[in]  number  Number string
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_call_dial(const char *number);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
