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
 * @brief  Initialize VCP volume controller and CAP commander callbacks.
 *
 * @param[in]  max_members  Maximum coordinated-set members to track.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - ESP_ERR_NO_MEM         If allocation fails
 *       - Others                 Failure codes from VCP/CAP callback or volume ops setup
 */
esp_err_t bt_audio_le_vcp_ctlr_init(uint8_t max_members);

/**
 * @brief  Tear down VCP volume controller state.
 */
void bt_audio_le_vcp_ctlr_deinit(void);

/**
 * @brief  Discover VCS on a connected peer.
 *
 * @param[in]  conn_handle  LE ACL connection handle.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the controller is not initialized
 *       - ESP_ERR_INVALID_ARG    If conn_handle is invalid
 *       - ESP_ERR_NO_MEM         If no peer slot is available
 *       - ESP_ERR_NOT_SUPPORTED  If discovery already failed for this handle
 *       - Others                 Failure codes from VCP discovery
 */
esp_err_t bt_audio_le_vcp_ctlr_discover(uint16_t conn_handle);

/**
 * @brief  Forget VCP controller state for a disconnected peer.
 */
void bt_audio_le_vcp_ctlr_forget(uint16_t conn_handle);

/**
 * @brief  Notify that unicast streams have started (enables set-wide volume ops).
 */
void bt_audio_le_vcp_ctlr_streams_started(void);

/**
 * @brief  Notify that unicast streams have stopped.
 */
void bt_audio_le_vcp_ctlr_streams_stopped(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
