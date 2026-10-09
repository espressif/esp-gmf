/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_cap_api.h"
#include "esp_bt_audio_defs.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize the CAP Initiator unicast client.
 *
 *         Creates LE streams, puts them in one transmit group, and registers CAP/BAP/CSIP
 *         callbacks. The member count is clamped to 1..2; zero is treated as two. Discovery
 *         results determine how many members are awaited and how many CIS carry the media.
 *
 * @param[in]  cfg  LE configuration; member count and source send task settings are used.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If cfg is NULL or a send task setting is out of range
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - ESP_ERR_NO_MEM         If allocation fails
 *       - Others                 Failure codes from CAP/BAP/CSIP registration
 */
esp_err_t bt_audio_le_unicast_client_init(const esp_bt_audio_le_cfg_t *cfg);

/**
 * @brief  Tear down CAP/BAP/CSIP registrations and free resources.
 *
 *         Stops active streams, cancels pending operations, and releases allocated state.
 */
void bt_audio_le_unicast_client_deinit(void);

/**
 * @brief  Start unicast audio (or defer until set is assembled).
 *
 * @param[in]  conn_handle  Primary LE ACL connection handle.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the client is not initialized or is busy
 *       - Others                 Failure codes from CAP discovery or audio start
 */
esp_err_t bt_audio_le_unicast_client_start(uint16_t conn_handle);

/**
 * @brief  Add a GATT-ready ACL as a potential set member. Starts CAP+CSIS+ASE discovery.
 *
 * @param[in]  conn_handle  LE ACL connection handle.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the client is not initialized
 *       - ESP_ERR_NO_MEM         If no peer slot is available
 *       - Others                 Failure codes from CAP discovery
 */
esp_err_t bt_audio_le_unicast_client_add_member(uint16_t conn_handle);

/**
 * @brief  Check an advertising field against the discovered set SIRK.
 *
 * @param[in]  data_type  Advertising data field type.
 * @param[in]  data       Raw advertising data payload.
 * @param[in]  data_len   Length of the advertising data payload.
 *
 * @return
 *       - true   The field contains a matching RSI
 *       - false  The field does not match or the client is not initialized
 */
bool bt_audio_le_unicast_client_match_rsi(uint8_t data_type, const uint8_t *data, uint8_t data_len);

/**
 * @brief  Return true while another coordinated-set member is required.
 *
 * @return
 *       - true   Another coordinated-set member is required
 *       - false  The set is complete or the client is not initialized
 */
bool bt_audio_le_unicast_client_needs_member(void);

/**
 * @brief  Return the ASE-ready connection with the lowest CSIS rank.
 *
 *         Order matches unicast-client member order: a peer with a CSIS instance
 *         sorts before one without, and a lower Set Member Rank sorts first.
 *         The query does not start audio, change bindings, or select a codec.
 *
 * @param[out]  conn_handle  ASE-ready LE ACL connection handle.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If conn_handle is NULL
 *       - ESP_ERR_INVALID_STATE  If the client is not initialized
 *       - ESP_ERR_NOT_FOUND      If no peer is ASE-ready
 */
esp_err_t bt_audio_le_unicast_client_ready_conn(uint16_t *conn_handle);

/**
 * @brief  Stop unicast client streams and release resources.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If the client is not initialized
 *       - Others                 Failure codes from CAP unicast stop
 */
esp_err_t bt_audio_le_unicast_client_stop(void);

/**
 * @brief  Reset runtime state after an ACL disconnect.
 *
 * @param[in]  conn_handle  Disconnected LE ACL connection handle.
 */
void bt_audio_le_unicast_client_on_disconnect(uint16_t conn_handle);

/**
 * @brief  Update the expected coordinated-set size after a CSIS notification.
 *
 * @param[in]  conn_handle  LE ACL connection handle that raised the notification.
 * @param[in]  set_size     New coordinated-set size.
 */
void bt_audio_le_unicast_client_on_set_size_changed(uint16_t conn_handle, uint8_t set_size);

/**
 * @brief  Continue a start that waited for a CSIP release to finish.
 */
void bt_audio_le_unicast_client_on_csip_release_done(void);

/**
 * @brief  Copy ASE-ready CAP set members for commander operations.
 *
 * @param[out]  members    Output member array.
 * @param[in]   max_count  Capacity of @p members.
 * @param[out]  count      Number of members written.
 * @param[out]  type       Coordinated-set type.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If a pointer argument is NULL or max_count is 0
 *       - ESP_ERR_INVALID_STATE  If the client is not initialized or no set is ready
 *       - ESP_ERR_NOT_FOUND      If a ready peer cannot be filled into @p members
 */
esp_err_t bt_audio_le_unicast_client_copy_cap_members(esp_ble_audio_cap_set_member_t *members,
                                                      size_t max_count, size_t *count,
                                                      esp_ble_audio_cap_set_type_t *type);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
