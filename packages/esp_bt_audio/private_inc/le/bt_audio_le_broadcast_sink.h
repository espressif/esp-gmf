/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>

#include "host/ble_gap.h"

#include "esp_err.h"
#include "esp_bt_audio_event.h"
#include "esp_ble_audio_common_api.h"
#include "esp_ble_audio_bap_api.h"
#include "esp_ble_iso_common_api.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Callback invoked for NimBLE GAP events on the PA sync path.
 */
typedef int (*bt_audio_le_gap_event_cb_t)(struct ble_gap_event *event, void *arg);

/**
 * @brief  Initialize the LE broadcast sink profile (single-instance).
 *
 * @param[in]  location  Sink audio location bitmask for PACS.
 * @param[in]  gap_cb    GAP callback for periodic advertising sync (may be NULL if unused).
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - ESP_ERR_NO_MEM         If allocation fails
 *       - Other                  non-zero codes from stream or BLE Audio registration APIs
 */
esp_err_t bt_audio_le_broadcast_sink_init(uint32_t location, bt_audio_le_gap_event_cb_t gap_cb);

/**
 * @brief  Deinitialize the broadcast sink and release resources.
 */
void bt_audio_le_broadcast_sink_deinit(void);

/**
 * @brief  Configure broadcast sink discovery filters (name, code, BIS preference).
 *
 * @param[in]  broadcast_name  Optional broadcast name filter (NULL to ignore).
 * @param[in]  broadcast_code  Optional broadcast code (NULL if not encrypted).
 * @param[in]  bit_field       Requested BIS sync bitfield or 0 for no preference.
 * @param[in]  timeout_ms      Reserved for future use.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If module not initialized
 */
esp_err_t bt_audio_le_broadcast_sink_sync(const uint8_t *broadcast_name, const uint8_t *broadcast_code,
                                          uint32_t bit_field, uint32_t timeout_ms);

/**
 * @brief  Tear down PA sync, BIG, and sink streams for the current session.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If module not initialized
 */
esp_err_t bt_audio_le_broadcast_sink_pa_sync_terminate(void);

/**
 * @brief  Accept a scan delegator request and prepare sink for the given receive state.
 *
 * @param[in]  recv_state      Receive state from the assistant.
 * @param[in]  broadcast_code  Optional broadcast code (NULL if unknown).
 * @param[in]  bit_field       Requested BIS sync bits or 0 for no preference.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If recv_state is NULL
 *       - ESP_ERR_INVALID_STATE  If sink is busy with another operation
 */
esp_err_t bt_audio_le_broadcast_sink_accept_scan_delegator_req(const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
                                                               const uint8_t *broadcast_code,
                                                               uint32_t bit_field);

/**
 * @brief  Enable receiving PA sync through Periodic Advertising Sync Transfer.
 *
 * @param[in]  conn        Connection with the broadcast assistant.
 * @param[in]  recv_state  Receive state from the assistant.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If sink is not ready or PAST is unsupported
 */
esp_err_t bt_audio_le_broadcast_sink_sync_with_past(struct bt_conn *conn,
                                                    const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state);

/**
 * @brief  Enable receiving PA sync without PAST through Periodic Advertising Sync Transfer.
 *
 * @param[in]  recv_state  Receive state from the assistant.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If recv_state is NULL
 */
esp_err_t bt_audio_le_broadcast_sink_sync_without_past(const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state);

/**
 * @brief  Provide broadcast code for an encrypted BIG and retry sync.
 *
 * @param[in]  recv_state      Current receive state reference.
 * @param[in]  broadcast_code  16-byte broadcast code.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If a pointer argument is NULL
 */
esp_err_t bt_audio_le_broadcast_sink_set_broadcast_code(const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
                                                        const uint8_t broadcast_code[ESP_BLE_ISO_BROADCAST_CODE_SIZE]);

/**
 * @brief  Update BIS sync request for the current receive state.
 *
 * @param[in]  recv_state  Receive state from the assistant.
 * @param[in]  bit_field   Requested BIS bits; 0 terminates PA sync (see implementation).
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If recv_state is NULL
 */
esp_err_t bt_audio_le_broadcast_sink_set_bis_sync_req(const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state,
                                                      uint32_t bit_field);

/**
 * @brief  Set the receive state for the broadcast sink.
 *
 * @param[in]  recv_state  Receive state from the assistant.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If recv_state is NULL
 */
esp_err_t bt_audio_le_broadcast_sink_set_recv_state(const esp_ble_audio_bap_scan_delegator_recv_state_t *recv_state);

/**
 * @brief  Handle an application-level device discovered event (sink filters).
 *
 * @param[in]  device  Parsed discovery data from the scanner.
 */
void bt_audio_le_broadcast_sink_on_device(const esp_bt_audio_event_device_discovered_t *device);

/**
 * @brief  Dispatch ESP-BLE-Audio GAP events relevant to broadcast sink state.
 *
 * @param[in]  event  Posted GAP application event.
 */
void bt_audio_le_broadcast_sink_on_gap_event(esp_ble_audio_gap_app_event_t *event);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
