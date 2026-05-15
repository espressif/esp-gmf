/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include "bt_audio_le_adv_builder.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Start extended scanning for broadcast assistant requests.
 *
 * @param[in]  timeout_ms  Scan duration hint passed to the registered implementation.
 *
 * @return
 *       - esp_err_t  from the application-provided callback.
 */
typedef esp_err_t (*bt_audio_le_scan_delegator_scan_cb_t)(uint32_t timeout_ms);

/**
 * @brief  Stop extended scanning started for the delegator.
 *
 * @return
 *       - esp_err_t  from the application-provided callback.
 */
typedef esp_err_t (*bt_audio_le_scan_delegator_stop_scan_cb_t)(void);

/**
 * @brief  Initialize BAP Scan Delegator support (single-instance).
 *
 * @param[in]  adv_builder  Extended advertising builder used to announce delegator service.
 * @param[in]  start_scan   Callback to start scanning when the assistant requests PA sync.
 * @param[in]  stop_scan    Callback to stop scanning.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If callbacks are NULL
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - ESP_ERR_NO_MEM         If allocation fails
 */
esp_err_t bt_audio_le_scan_delegator_init(bt_audio_le_adv_builder_t adv_builder,
                                          bt_audio_le_scan_delegator_scan_cb_t start_scan,
                                          bt_audio_le_scan_delegator_stop_scan_cb_t stop_scan);

/**
 * @brief  Deinitialize scan delegator state and callbacks.
 */
void bt_audio_le_scan_delegator_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
