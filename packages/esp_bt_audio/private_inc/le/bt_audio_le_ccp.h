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
 * @brief  Optional callback after CCP discovery completes on a connection.
 *
 * @param[in]  conn_handle  BLE connection handle.
 * @param[in]  user_ctx     Opaque pointer from @ref bt_audio_le_ccp_init.
 */
typedef void (*bt_audio_le_ccp_ready_cb_t)(uint16_t conn_handle, void *user_ctx);

/**
 * @brief  Initialize Telephone Bearer Service client (CCP) for LE Audio.
 *
 * @param[in]  ready_cb  Optional callback invoked when CCP setup is ready (may be NULL).
 * @param[in]  user_ctx  User context passed to @a ready_cb.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - Other                  non-zero codes from TBS client registration
 */
esp_err_t bt_audio_le_ccp_init(bt_audio_le_ccp_ready_cb_t ready_cb, void *user_ctx);

/**
 * @brief  Deinitialize CCP client.
 */
void bt_audio_le_ccp_deinit(void);

/**
 * @brief  Discover TBS instances on the given connection.
 *
 * @param[in]  conn_handle  BLE connection handle.
 *
 * @return
 *       - Propagated  esp_err_t from the TBS client discovery API.
 */
esp_err_t bt_audio_le_ccp_discover(uint16_t conn_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
