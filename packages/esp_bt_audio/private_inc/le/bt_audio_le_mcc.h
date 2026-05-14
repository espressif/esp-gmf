/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize Media Control Client (MCC) for LE Audio (single-instance).
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - Other                  non-zero codes from esp_ble_audio MCC registration
 */
esp_err_t bt_audio_le_mcc_init(void);

/**
 * @brief  Deinitialize MCC client state.
 */
void bt_audio_le_mcc_deinit(void);

/**
 * @brief  Discover MCS on the given ACL connection.
 *
 * @param[in]  conn_handle  BLE connection handle.
 *
 * @return
 *       - Propagated  esp_err_t from the MCC discovery API.
 */
esp_err_t bt_audio_le_mcc_discover(uint16_t conn_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
