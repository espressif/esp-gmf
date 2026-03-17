/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_bt_audio_event.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Event destination
 */
typedef enum {
    ESP_BT_AUDIO_EVT_DST_USR,      /*!< Event to user */
    ESP_BT_AUDIO_EVT_DST_CLASSIC,  /*!< Event to Classic Bluetooth */
    ESP_BT_AUDIO_EVT_DST_MAX,      /*!< Maximum event destination */
} esp_bt_audio_evt_dst_t;

/**
 * @brief  Initialize the event dispatcher module
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - ESP_ERR_NO_MEM         If memory allocation fails
 */
esp_err_t bt_audio_evt_dispatcher_init(void);

/**
 * @brief  Register an event dispatcher callback
 *
 * @param[in]  dst             Event destination
 * @param[in]  event_cb        Event dispatcher callback function
 * @param[in]  event_user_ctx  User context for the event dispatcher callback
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  If event dispatcher module not initialized
 */
esp_err_t bt_audio_evt_dispatcher_cb_register(esp_bt_audio_evt_dst_t dst, esp_bt_audio_event_cb_t event_cb, void *event_user_ctx);

/**
 * @brief  Dispatch an event
 *
 * @param[in]  dst         Event destination
 * @param[in]  event       Event type
 * @param[in]  event_data  Pointer to event data
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  If event dispatcher module not initialized
 */
esp_err_t bt_audio_evt_dispatch(esp_bt_audio_evt_dst_t dst, esp_bt_audio_event_t event, void *event_data);

/**
 * @brief  Deinitialize the event dispatcher module
 */
void bt_audio_evt_dispatcher_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
