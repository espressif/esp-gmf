/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#if CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO

/**
 * @brief  Start LE device discovery by scanning.
 *
 * @param[in]  timeout_ms  Scan timeout in milliseconds. If 0, default timeout is used.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_scan_start(uint32_t timeout_ms);

/**
 * @brief  Start LE device discovery by scanning, optionally filtering for a target address.
 *
 * @param[in]  target      Target address, or NULL to report all devices.
 * @param[in]  timeout_ms  Scan timeout in milliseconds. If 0, default timeout is used.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_scan_start_ext(const uint8_t *target, uint32_t timeout_ms);

/**
 * @brief  Stop LE device discovery.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_scan_stop(void);

/**
 * @brief  Connect to a LE device.
 *
 * @param[in]  addr_type    LE peer address type.
 * @param[in]  bt_dev_addr  LE peer address.
 * @param[in]  timeout_ms   Connection timeout in milliseconds.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid argument
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_connect(uint8_t addr_type, const uint8_t *bt_dev_addr, uint32_t timeout_ms);

/**
 * @brief  Disconnect current LE ACL connection.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_disconnect(void);

/**
 * @brief  Disconnect from a LE device.
 *
 * @param[in]  bt_dev_addr  LE peer address, or NULL to disconnect the current ACL link.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_disconnect_peer(const uint8_t *bt_dev_addr);

/**
 * @brief  Sync to an LE Audio broadcast.
 *
 * @param[in]  broadcast_name  Broadcast name, or NULL to sync by discovered broadcast ID.
 * @param[in]  broadcast_code  Broadcast code, or NULL for unencrypted broadcast.
 * @param[in]  bit_field       BIS sync bit field.
 * @param[in]  timeout_ms      Sync timeout in milliseconds.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_broadcast_sync(const uint8_t *broadcast_name, const uint8_t *broadcast_code,
                                         uint32_t bit_field, uint32_t timeout_ms);

/**
 * @brief  Terminate LE periodic advertising sync.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_pa_sync_terminate(void);

#endif  /* CONFIG_BT_NIMBLE_ENABLED && CONFIG_BT_AUDIO && CONFIG_BT_ISO */

#ifdef __cplusplus
}
#endif  /* __cplusplus */
