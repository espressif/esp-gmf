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
#include "esp_ble_audio_csip_api.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  CSIP set coordinator lock operation result callback.
 *
 * @param[in]  err  0 on success, negative on failure or timeout (-ETIMEDOUT).
 */
typedef void (*bt_audio_le_csip_coordinator_lock_done_cb_t)(int err);

/**
 * @brief  Initialize CSIP set coordinator callbacks and create lock timer.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - Others                 Failure codes from timer creation or callback registration
 */
esp_err_t bt_audio_le_csip_coordinator_init(void);

/**
 * @brief  Deinitialize the CSIP coordinator.
 */
void bt_audio_le_csip_coordinator_deinit(void);

/**
 * @brief  Start or stop coordinated-set discovery based on current membership.
 */
void bt_audio_le_csip_coordinator_search_update(void);

/**
 * @brief  Store the SIRK discovered from a set member.
 *
 *         A different SIRK drops members whose stored key does not match and
 *         bumps the SIRK generation. The same key is left unchanged.
 *
 * @param[in]  sirk  Resolved Set Identity Resolving Key to store.
 */
void bt_audio_le_csip_coordinator_set_sirk(const uint8_t sirk[ESP_BLE_AUDIO_CSIP_SIRK_SIZE]);

/**
 * @brief  Get the current SIRK.
 *
 * @return
 *       - Pointer  Stored SIRK buffer
 *       - NULL     No SIRK has been stored
 */
const uint8_t *bt_audio_le_csip_coordinator_get_sirk(void);

/**
 * @brief  Generation of the active SIRK.
 *
 *         RSI scan results and pending connects captured under an older
 *         generation are stale.
 *
 * @return  Current generation. It starts at zero and increases when the SIRK changes or clears.
 */
uint32_t bt_audio_le_csip_coordinator_sirk_generation(void);

/**
 * @brief  Forget the active SIRK.
 *
 *         Later coordinated-set discovery can accept another set. Accepted
 *         members are dropped from the coordinator. Lock grants are kept so a
 *         bonded member can still be unlocked.
 */
void bt_audio_le_csip_coordinator_clear_sirk(void);

/**
 * @brief  Remember an RSI connect and the SIRK generation that matched it.
 *
 * @param[in]  conn_handle  LE ACL connection handle.
 * @param[in]  generation   Value from bt_audio_le_csip_coordinator_sirk_generation() at the match.
 */
void bt_audio_le_csip_coordinator_note_scan_connect(uint16_t conn_handle, uint32_t generation);

/**
 * @brief  Drop a remembered RSI connect.
 *
 * @param[in]  conn_handle  LE ACL connection handle.
 */
void bt_audio_le_csip_coordinator_forget_scan_connect(uint16_t conn_handle);

/**
 * @brief  Return false when this ACL was connected from an RSI match that is now stale.
 *
 * @param[in]  conn_handle  LE ACL connection handle.
 *
 * @return
 *       - true   The connect is still current, or it did not come from an RSI match
 *       - false  The SIRK generation changed after the RSI matched
 */
bool bt_audio_le_csip_coordinator_scan_connect_is_current(uint16_t conn_handle);

/**
 * @brief  Return true if more coordinated-set members are needed.
 *
 * @param[in]  ready_count    Number of members currently ready for discovery.
 * @param[in]  expected_count  Number of members required by the coordinated set.
 *
 * @return
 *       - true   Another member is required
 *       - false  The expected member count has been assembled
 */
bool bt_audio_le_csip_coordinator_needs_member(size_t ready_count, size_t expected_count);

/**
 * @brief  Check advertising data against the stored SIRK.
 *
 * @param[in]  data_type  Advertising data field type.
 * @param[in]  data       Raw advertising data payload.
 * @param[in]  data_len   Length of the advertising data payload.
 *
 * @return
 *       - true   The field contains an RSI that matches the stored SIRK
 *       - false  The field does not match or no SIRK is stored
 */
bool bt_audio_le_csip_coordinator_match_rsi(uint8_t data_type, const uint8_t *data, uint8_t data_len);

/**
 * @brief  Lock the coordinated set (async).
 *
 *         Members are locked from lowest rank to highest by the stack. The
 *         granted members are remembered so a later release still reaches a
 *         single remaining member.
 *
 * @param[in]  members     Array of set member pointers (ASE-ready).
 * @param[in]  set_info    Set info from one member's csis_inst->info.
 * @param[in]  ranks       Per-member Set Member Rank, same order as @p members.
 *                         May be NULL when rank is unknown.
 * @param[in]  count       Number of members (>= 2).
 * @param[in]  timeout_ms  Lock timeout in milliseconds.
 * @param[in]  on_done     Completion callback. On failure this runs only after
 *                         members that may already have granted the lock are released.
 *
 * @return
 *       - ESP_OK                 Lock procedure started.
 *       - ESP_ERR_INVALID_ARG    Invalid arguments.
 *       - ESP_ERR_INVALID_STATE  Already in progress.
 */
esp_err_t bt_audio_le_csip_coordinator_lock(const esp_ble_audio_csip_set_coordinator_set_member_t *members[],
                                            const esp_ble_audio_csip_set_coordinator_set_info_t *set_info,
                                            const uint8_t *ranks,
                                            size_t count,
                                            uint32_t timeout_ms,
                                            bt_audio_le_csip_coordinator_lock_done_cb_t on_done);

/**
 * @brief  Release every still-connected member that was granted the lock.
 *
 *         CSIP 4.6.4 requires Unlocked to be written to all members that granted
 *         the lock, starting from the highest rank. A single remaining member is
 *         released. Disconnected bonded members stay recorded and are released
 *         when bt_audio_le_csip_coordinator_member_available() sees them again.
 *         A failed release is retried and does not clear the local lock.
 *
 * @return
 *       - ESP_OK                 Release started, already finished, or nothing to release.
 *       - ESP_ERR_INVALID_STATE  Coordinator is not initialized.
 */
esp_err_t bt_audio_le_csip_coordinator_release_granted(void);

/**
 * @brief  A granted member's ACL dropped.
 *
 *         The disconnected member is no longer written. Every granted member that
 *         is still connected is released immediately, even when only one remains.
 *         A bonded member keeps its lock across disconnect until lock timeout, so
 *         the rank is kept for a later release. When the last accepted coordinated-set
 *         member disconnects, the active SIRK is cleared.
 *
 * @param[in]  member  Set member pointer captured before the peer slot is cleared.
 */
void bt_audio_le_csip_coordinator_member_disconnected(
    const esp_ble_audio_csip_set_coordinator_set_member_t *member);

/**
 * @brief  A set member is connected and its CSIS values are known.
 *
 *         The member is recorded under the active SIRK. If this rank still has
 *         an unreleased lock from an earlier grant, the member pointer is
 *         refreshed and the lock is released before another coordinated procedure
 *         starts.
 *
 * @param[in]  conn_handle  LE ACL connection handle for this member.
 * @param[in]  member       Current set member pointer for this connection.
 * @param[in]  info         CSIS info read from that member.
 */
void bt_audio_le_csip_coordinator_member_available(
    uint16_t conn_handle,
    const esp_ble_audio_csip_set_coordinator_set_member_t *member,
    const esp_ble_audio_csip_set_coordinator_set_info_t *info);

/**
 * @brief  Cancel any in-progress lock/release operation and stop timeout timer.
 *
 *         Does not clear a granted lock. Call bt_audio_le_csip_coordinator_release_granted()
 *         to write Unlocked.
 */
void bt_audio_le_csip_coordinator_cancel_op(void);

/**
 * @brief  Return true if a still-connected member is locked by us.
 *
 *         A bonded member that disconnected while locked does not keep this true;
 *         that lock is released when the member is available again.
 *
 * @return
 *       - true   At least one connected member is still locked by this device
 *       - false  No connected member is locked by this device
 */
bool bt_audio_le_csip_coordinator_is_locked(void);

/**
 * @brief  Return true while a lock or release procedure is outstanding.
 *
 * @return
 *       - true   A CSIP lock or release procedure is in progress
 *       - false  No CSIP procedure is in progress
 */
bool bt_audio_le_csip_coordinator_op_in_progress(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
