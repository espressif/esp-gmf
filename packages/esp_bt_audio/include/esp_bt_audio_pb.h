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
 * @brief  Fetch target
 */
typedef enum {
    ESP_BT_AUDIO_PB_FETCH_TARGET_MAIN_PB          = 1,
    ESP_BT_AUDIO_PB_FETCH_TARGET_INCOMING_HISTORY = 2,
    ESP_BT_AUDIO_PB_FETCH_TARGET_OUTGOING_HISTORY = 3,
    ESP_BT_AUDIO_PB_FETCH_TARGET_MISSED_HISTORY   = 4,
    ESP_BT_AUDIO_PB_FETCH_TARGET_COMBINED_HISTORY = 5,
} esp_bt_audio_pb_fetch_target_t;

/**
 * @brief  Name property
 */
typedef struct {
    char *last_name;    /*!< Family name */
    char *first_name;   /*!< First name */
    char *middle_name;  /*!< Middle name */
    char *prefix;       /*!< Prefix */
    char *suffix;       /*!< Suffix */
} esp_bt_audio_pb_name_t;

/**
 * @brief  Telephone number
 */
typedef struct {
    char *type;    /*!< Phone number type: HOME, WORK, CELL, etc. */
    char *number;  /*!< Telephone number */
} esp_bt_audio_pb_tel_t;

/**
 * @brief  Phonebook entry
 */
typedef struct {
    esp_bt_audio_pb_name_t  name;       /*!< Name */
    char                   *fullname;   /*!< Full name */
    size_t                  tel_count;  /*!< Number of telephone entries */
    esp_bt_audio_pb_tel_t  *tel;        /*!< Telephone number list */
} esp_bt_audio_pb_entry_t;

/**
 * @brief  Phonebook history entry
 */
typedef struct {
    esp_bt_audio_pb_entry_t  entry;      /*!< Entry */
    char                    *property;   /*!< Property, RECEIVED, DIALED, MISSED, COMBINED */
    char                    *timestamp;  /*!< Timestamp, YYYYMMDDTHHMMSSZ */
} esp_bt_audio_pb_history_t;

/**
 * @brief  Fetch phonebook entries, call history entries
 *
 * @param[in]  target     Fetch target
 * @param[in]  start_idx  Start index
 * @param[in]  count      Number of entries to fetch, if 0, fetch all entries
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_pb_fetch(esp_bt_audio_pb_fetch_target_t target, uint16_t start_idx, uint16_t count);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
