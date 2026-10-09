/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stddef.h>

#include "esp_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#if CONFIG_BT_AUDIO && CONFIG_BT_ISO

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
 * @brief  Enable or disable LE Audio advertising.
 *
 *         Disabling advertising keeps existing LE connections untouched, but
 *         prevents new phones from discovering and connecting to LE Audio.
 *
 * @param[in]  enable  True to start advertising, false to stop advertising.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_set_advertising(bool enable);

/**
 * @brief  Query whether LE Audio advertising is currently running.
 *
 * @return
 *       - true   Extended advertising is running
 *       - false  Advertising is stopped, or LE Audio is not started
 */
bool esp_bt_audio_le_is_advertising(void);

/**
 * @brief  Get the number of locally bonded LE devices.
 *
 * @return  Number of LE bonds, or 0 if no host bond database is available.
 */
size_t esp_bt_audio_le_get_bond_count(void);

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
 * @brief  Cancel a pending LE connection attempt.
 *
 *         Has no effect if the ACL is already established; use
 *         `esp_bt_audio_le_disconnect_peer()` to drop an active link.
 *
 * @return
 *       - ESP_OK                 On success, or if the ACL is already established
 *       - ESP_ERR_INVALID_STATE  If LE Audio is not initialized
 *       - Others                 Failure codes from the host connect-cancel path
 */
esp_err_t esp_bt_audio_le_connect_cancel(void);

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
 * @brief  Start LE Audio broadcast source media streaming.
 *
 *         This starts the BIG/audio path for an initialized broadcast source.
 *         Extended and periodic advertising may already be running after LE
 *         Audio initialization so receivers can discover the broadcast first.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_broadcast_source_start(void);

/**
 * @brief  Stop LE Audio broadcast source media streaming.
 *
 *         This stops the BIG/audio path for an initialized broadcast source.
 *         Extended and periodic advertising remain available so receivers can
 *         discover the broadcast before a later restart.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_broadcast_source_stop(void);

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

/**
 * @brief  Request LE Audio unicast client media streaming to the connected peer.
 *
 *         Discovers remote Sink ASEs and runs BAP config/QoS/enable/ISO connect.
 *         Requires an established LE ACL link and completed GATT discovery.
 *         Local TX streams are reported as LE unicast SOURCE + MEDIA.
 *
 *         `ESP_OK` means the request was accepted. Discovery, CIS setup, and
 *         streaming complete asynchronously via stream state events. If no
 *         usable Sink ASE is found, or discovery times out, the client returns
 *         to idle and `esp_bt_audio_le_unicast_start()` may be called again.
 *
 * @return
 *       - ESP_OK                 Request accepted
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_unicast_start(void);

/**
 * @brief  Stop LE Audio unicast client media streaming and release ASEs.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_le_unicast_stop(void);

#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO */

#ifdef __cplusplus
}
#endif  /* __cplusplus */
