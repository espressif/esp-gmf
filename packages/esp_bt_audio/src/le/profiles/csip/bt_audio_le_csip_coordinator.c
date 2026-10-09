/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_ble_audio_csip_api.h"

#include "bt_audio_le_csip_coordinator.h"
#include "bt_audio_le_unicast_client.h"

#define BT_AUDIO_LE_CSIP_COORDINATOR_LOCK_TIMEOUT_DEFAULT_MS  5000
#define BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS                    2
#define BT_AUDIO_LE_CSIP_COORD_RELEASE_RETRY_MAX              3

/**
 * @brief  One member that granted the coordinated-set lock.
 *
 *         The stack does not remember this set. CSIP 4.6.4 still requires a
 *         release after a member drops out, so the grant is stored here.
 */
typedef struct {
    const esp_ble_audio_csip_set_coordinator_set_member_t *member;  /*!< Live member, NULL once disconnected */
    uint8_t                                                rank;    /*!< Set Member Rank at lock time */
    uint8_t                                                retries; /*!< Failed release attempts */
    bool                                                   active;  /*!< Slot records a grant */
    bool                                                   connected; /*!< ACL can accept a write */
    bool                                                   released;  /*!< Unlocked write succeeded, or peer unlocked */
    bool                                                   retire;    /*!< Drop the slot once Unlocked is written */
} bt_audio_le_csip_granted_member_t;

/**
 * @brief  One coordinated-set member accepted under the active SIRK.
 */
typedef struct {
    const esp_ble_audio_csip_set_coordinator_set_member_t *member; /*!< Live member, NULL once disconnected */
    const esp_ble_audio_csip_set_coordinator_set_info_t   *info;   /*!< CSIS info while the ACL is up */
    uint8_t                                                sirk[ESP_BLE_AUDIO_CSIP_SIRK_SIZE]; /*!< SIRK copied at accept time */
    uint16_t                                               conn_handle; /*!< ACL handle while connected */
    uint8_t                                                rank;        /*!< Set Member Rank */
    bool                                                   active;      /*!< Slot records a set member */
    bool                                                   connected;   /*!< ACL is up */
} bt_audio_le_csip_accepted_member_t;

/**
 * @brief  RSI connect captured so a later SIRK change can ignore it.
 */
typedef struct {
    uint16_t conn_handle; /*!< ACL from an RSI match */
    uint32_t generation;  /*!< sirk_generation when the RSI matched */
    bool     used;        /*!< Slot is occupied */
} bt_audio_le_csip_scan_connect_t;

/**
 * @brief  Runtime context of the CSIP set coordinator.
 */
typedef struct {
    esp_timer_handle_t                            lock_timer;                          /*!< Lock/release timeout */
    bt_audio_le_csip_coordinator_lock_done_cb_t   lock_done_cb;                        /*!< Lock completion callback */
    bt_audio_le_csip_coordinator_lock_done_cb_t   deferred_lock_cb;                    /*!< Delivered after cleanup release */
    int                                           deferred_lock_err;                   /*!< Error passed to deferred_lock_cb */
    uint8_t                                       sirk[ESP_BLE_AUDIO_CSIP_SIRK_SIZE];  /*!< Discovered set SIRK */
    uint32_t                                      sirk_generation;                     /*!< Bumped when the active SIRK changes or clears */
    bool                                          sirk_valid;                          /*!< SIRK is stored */
    bool                                          granted_sirk_refresh;                /*!< Copy the active SIRK into granted_info after release */
    bool                                          retire_only;                         /*!< Release walk skips grants that still match */
    bool                                          set_locked;                          /*!< A connected member is still locked */
    bool                                          initialized;                         /*!< True after init */
    bool                                          cb_registered;                       /*!< Callbacks registered */
    bool                                          op_in_progress;                      /*!< Lock/release running */
    bool                                          lock_cb_armed;                       /*!< Accept the next lock_set callback */
    bool                                          release_cb_armed;                    /*!< Accept the next release_set callback */
    bool                                          releasing;                           /*!< Walking granted members */
    bool                                          release_after_lock;                  /*!< Release once the lock procedure ends */
    bt_audio_le_csip_granted_member_t             granted[BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS]; /*!< Members that granted the lock */
    size_t                                        granted_count;                       /*!< Active granted slots */
    esp_ble_audio_csip_set_coordinator_set_info_t granted_info;                        /*!< Set info copied at lock time */
    bool                                          granted_valid;                       /*!< granted_info matches the grant */
    bt_audio_le_csip_accepted_member_t            accepted[BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS]; /*!< Members accepted into the active set */
    bt_audio_le_csip_scan_connect_t               scan_connect[BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS]; /*!< RSI connects waiting on discovery */
    const esp_ble_audio_csip_set_coordinator_set_member_t *release_member;             /*!< Member pointer held for the stack */
    int                                           release_idx;                         /*!< Granted slot being released */
} bt_audio_le_csip_coordinator_ctx_t;

static const char *TAG = "BT_AUD_LE_CSIP_COORD";
static bt_audio_le_csip_coordinator_ctx_t s_csip;
static uint8_t s_release_depth;

static void bt_audio_le_csip_coordinator_refresh_locked(void)
{
    bool connected_unreleased = false;

    for (size_t i = 0; i < s_csip.granted_count; i++) {
        bt_audio_le_csip_granted_member_t *entry = &s_csip.granted[i];
        if (entry->active && entry->connected && !entry->released) {
            connected_unreleased = true;
            break;
        }
    }
    s_csip.set_locked = connected_unreleased;
}

static int bt_audio_le_csip_coordinator_find_member(
    const esp_ble_audio_csip_set_coordinator_set_member_t *member)
{
    if (!member) {
        return -1;
    }
    for (size_t i = 0; i < s_csip.granted_count; i++) {
        if (s_csip.granted[i].active && s_csip.granted[i].member == member) {
            return (int)i;
        }
    }
    return -1;
}

static int bt_audio_le_csip_coordinator_find_disconnected_rank(uint8_t rank)
{
    for (size_t i = 0; i < s_csip.granted_count; i++) {
        bt_audio_le_csip_granted_member_t *entry = &s_csip.granted[i];
        if (entry->active && !entry->released && !entry->connected &&
                entry->member == NULL && entry->rank == rank) {
            return (int)i;
        }
    }
    return -1;
}

static size_t bt_audio_le_csip_coordinator_accepted_connected(void)
{
    size_t count = 0;

    for (size_t i = 0; i < BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS; i++) {
        if (s_csip.accepted[i].active && s_csip.accepted[i].connected) {
            count++;
        }
    }
    return count;
}

static bool bt_audio_le_csip_coordinator_accepted_has(
    const esp_ble_audio_csip_set_coordinator_set_member_t *member)
{
    if (!member) {
        return false;
    }
    for (size_t i = 0; i < BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS; i++) {
        if (s_csip.accepted[i].active && s_csip.accepted[i].member == member) {
            return true;
        }
    }
    return false;
}

static void bt_audio_le_csip_coordinator_refresh_granted_sirk(void)
{
    bool any_active = false;

    if (s_csip.releasing || s_csip.op_in_progress) {
        return;
    }
    for (size_t i = 0; i < s_csip.granted_count; i++) {
        if (s_csip.granted[i].active) {
            any_active = true;
            break;
        }
    }
    if (!any_active) {
        s_csip.granted_valid = false;
        s_csip.granted_sirk_refresh = false;
        return;
    }
    if (!s_csip.granted_sirk_refresh || !s_csip.granted_valid || !s_csip.sirk_valid) {
        return;
    }
    memcpy(s_csip.granted_info.sirk, s_csip.sirk, sizeof(s_csip.sirk));
    s_csip.granted_sirk_refresh = false;
}

static void bt_audio_le_csip_coordinator_clear_active_sirk(void)
{
    if (!s_csip.sirk_valid) {
        return;
    }
    s_csip.sirk_valid = false;
    s_csip.sirk_generation++;
    for (size_t i = 0; i < BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS; i++) {
        s_csip.accepted[i].active = false;
        s_csip.accepted[i].connected = false;
        s_csip.accepted[i].member = NULL;
        s_csip.accepted[i].info = NULL;
        s_csip.accepted[i].conn_handle = UINT16_MAX;
    }
    ESP_LOGI(TAG, "SIRK cleared");
}

static int bt_audio_le_csip_coordinator_find_accepted(
    const esp_ble_audio_csip_set_coordinator_set_member_t *member,
    const esp_ble_audio_csip_set_coordinator_set_info_t *info,
    uint8_t rank)
{
    int disconnected = -1;

    for (size_t i = 0; i < BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS; i++) {
        bt_audio_le_csip_accepted_member_t *entry = &s_csip.accepted[i];

        if (!entry->active) {
            continue;
        }
        if (member && entry->member == member) {
            return (int)i;
        }
        if (info && entry->info == info) {
            return (int)i;
        }
        if (!entry->connected && entry->rank == rank && disconnected < 0) {
            disconnected = (int)i;
        }
    }
    return disconnected;
}

static void bt_audio_le_csip_coordinator_accept_member(
    uint16_t conn_handle,
    const esp_ble_audio_csip_set_coordinator_set_member_t *member,
    const esp_ble_audio_csip_set_coordinator_set_info_t *info)
{
    int slot = bt_audio_le_csip_coordinator_find_accepted(member, info, info->rank);

    if (slot < 0) {
        for (size_t i = 0; i < BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS; i++) {
            if (!s_csip.accepted[i].active) {
                slot = (int)i;
                break;
            }
        }
    }
    if (slot < 0) {
        ESP_LOGW(TAG, "Accepted-member table full, rank %u not recorded", info->rank);
        return;
    }

    bt_audio_le_csip_accepted_member_t *entry = &s_csip.accepted[slot];
    entry->member = member;
    entry->info = info;
    entry->conn_handle = conn_handle;
    entry->rank = info->rank;
    entry->active = true;
    entry->connected = true;
    memcpy(entry->sirk, info->sirk, sizeof(entry->sirk));
}

static bool bt_audio_le_csip_coordinator_mark_accepted_disconnected(
    const esp_ble_audio_csip_set_coordinator_set_member_t *member)
{
    if (!member) {
        return false;
    }
    for (size_t i = 0; i < BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS; i++) {
        bt_audio_le_csip_accepted_member_t *entry = &s_csip.accepted[i];

        if (!entry->active || !entry->connected || entry->member != member) {
            continue;
        }
        entry->connected = false;
        entry->member = NULL;
        entry->info = NULL;
        entry->conn_handle = UINT16_MAX;
        return true;
    }
    return false;
}

static void bt_audio_le_csip_coordinator_retire_mismatched_grants(void)
{
    for (size_t i = 0; i < s_csip.granted_count; i++) {
        bt_audio_le_csip_granted_member_t *entry = &s_csip.granted[i];

        if (!entry->active || bt_audio_le_csip_coordinator_accepted_has(entry->member)) {
            continue;
        }
        if (!entry->connected || entry->released || !entry->member) {
            entry->active = false;
            entry->member = NULL;
            entry->retire = false;
            continue;
        }
        entry->retire = true;
        entry->retries = 0;
    }
}

static void bt_audio_le_csip_coordinator_release_dropped_streams(const uint16_t *handles,
                                                                 size_t count)
{
    esp_err_t ret = bt_audio_le_unicast_client_stop();

    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Stream release after SIRK change failed: %s", esp_err_to_name(ret));
    }
    for (size_t i = 0; i < count; i++) {
        bt_audio_le_unicast_client_on_disconnect(handles[i]);
    }
}

static bool bt_audio_le_csip_coordinator_apply_sirk(const uint8_t *new_sirk)
{
    bool had = s_csip.sirk_valid;
    uint16_t dropped_handles[BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS];
    size_t dropped_count = 0;

    if (!new_sirk) {
        return false;
    }
    if (had && memcmp(s_csip.sirk, new_sirk, ESP_BLE_AUDIO_CSIP_SIRK_SIZE) == 0) {
        return false;
    }

    for (size_t i = 0; i < BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS; i++) {
        bt_audio_le_csip_accepted_member_t *entry = &s_csip.accepted[i];

        if (!entry->active ||
                memcmp(entry->sirk, new_sirk, ESP_BLE_AUDIO_CSIP_SIRK_SIZE) == 0) {
            continue;
        }
        ESP_LOGI(TAG, "Dropping rank %u after SIRK change", entry->rank);
        if (entry->connected && entry->conn_handle != UINT16_MAX) {
            dropped_handles[dropped_count++] = entry->conn_handle;
        }
        entry->active = false;
        entry->connected = false;
        entry->member = NULL;
        entry->info = NULL;
        entry->conn_handle = UINT16_MAX;
    }

    memcpy(s_csip.sirk, new_sirk, sizeof(s_csip.sirk));
    s_csip.sirk_valid = true;
    if (had) {
        s_csip.sirk_generation++;
        if (s_csip.granted_valid) {
            s_csip.granted_sirk_refresh = true;
        }
        bt_audio_le_csip_coordinator_retire_mismatched_grants();
        s_csip.retire_only = true;
        bt_audio_le_csip_coordinator_release_granted();
        if (!s_csip.releasing && !s_csip.op_in_progress) {
            s_csip.retire_only = false;
        }
        bt_audio_le_csip_coordinator_refresh_granted_sirk();
    }
    if (dropped_count > 0) {
        bt_audio_le_csip_coordinator_release_dropped_streams(dropped_handles, dropped_count);
    }
    return true;
}

static void bt_audio_le_csip_coordinator_remember_grant(
    const esp_ble_audio_csip_set_coordinator_set_member_t *members[],
    const esp_ble_audio_csip_set_coordinator_set_info_t *set_info,
    const uint8_t *ranks, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        uint8_t rank = ranks ? ranks[i] : 0;
        int existing = bt_audio_le_csip_coordinator_find_member(members[i]);
        if (existing < 0) {
            existing = bt_audio_le_csip_coordinator_find_disconnected_rank(rank);
        }
        if (existing >= 0) {
            s_csip.granted[existing].member = members[i];
            s_csip.granted[existing].rank = rank;
            s_csip.granted[existing].connected = true;
            s_csip.granted[existing].released = false;
            s_csip.granted[existing].retries = 0;
            continue;
        }
        int slot = -1;
        for (size_t j = 0; j < s_csip.granted_count; j++) {
            if (s_csip.granted[j].active && s_csip.granted[j].released) {
                slot = (int)j;
                break;
            }
        }
        if (slot < 0) {
            if (s_csip.granted_count >= BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS) {
                ESP_LOGW(TAG, "Grant table full, rank %u not recorded", rank);
                continue;
            }
            slot = (int)s_csip.granted_count++;
        }
        s_csip.granted[slot] = (bt_audio_le_csip_granted_member_t) {
            .member = members[i],
            .rank = rank,
            .active = true,
            .connected = true,
        };
    }
    s_csip.granted_info = *set_info;
    s_csip.granted_valid = true;
}

static int bt_audio_le_csip_coordinator_next_release(void)
{
    int best = -1;

    for (size_t i = 0; i < s_csip.granted_count; i++) {
        bt_audio_le_csip_granted_member_t *entry = &s_csip.granted[i];
        if (!entry->active || entry->released || !entry->connected || !entry->member) {
            continue;
        }
        if (s_csip.retire_only && !entry->retire) {
            continue;
        }
        if (entry->retries >= BT_AUDIO_LE_CSIP_COORD_RELEASE_RETRY_MAX) {
            continue;
        }
        if (best < 0 || entry->rank > s_csip.granted[best].rank) {
            best = (int)i;
        }
    }
    return best;
}

static void bt_audio_le_csip_coordinator_arm_timer(void)
{
    if (!s_csip.lock_timer) {
        return;
    }
    esp_timer_stop(s_csip.lock_timer);
    esp_timer_start_once(s_csip.lock_timer,
                         (uint64_t)BT_AUDIO_LE_CSIP_COORDINATOR_LOCK_TIMEOUT_DEFAULT_MS * 1000ULL);
}

static void bt_audio_le_csip_coordinator_release_finish(void)
{
    bt_audio_le_csip_coordinator_lock_done_cb_t cb = s_csip.deferred_lock_cb;
    int err = s_csip.deferred_lock_err;

    s_csip.releasing = false;
    s_csip.op_in_progress = false;
    s_csip.release_cb_armed = false;
    s_csip.deferred_lock_cb = NULL;
    s_csip.retire_only = false;
    bt_audio_le_csip_coordinator_refresh_locked();
    bt_audio_le_csip_coordinator_refresh_granted_sirk();

    if (cb) {
        cb(err);
        return;
    }
    /* A synchronous caller resumes itself. Notifying here would re-enter it. */
    if (s_release_depth == 0) {
        bt_audio_le_unicast_client_on_csip_release_done();
    }
}

static esp_err_t bt_audio_le_csip_coordinator_release_kick(void)
{
    int idx = bt_audio_le_csip_coordinator_next_release();
    if (idx < 0) {
        bool walked = s_csip.releasing || s_csip.deferred_lock_cb != NULL;
        if (walked) {
            bt_audio_le_csip_coordinator_release_finish();
        } else {
            s_csip.retire_only = false;
            bt_audio_le_csip_coordinator_refresh_locked();
            bt_audio_le_csip_coordinator_refresh_granted_sirk();
        }
        return ESP_OK;
    }

    s_csip.release_idx = idx;
    s_csip.releasing = true;
    s_csip.release_member = s_csip.granted[idx].member;
    /* One member per call so a Lock Release Not Allowed on one rank does not
     * skip the others. Highest rank is selected above, per CSIP 4.6.4. */
    esp_err_t ret = esp_ble_audio_csip_set_coordinator_release(
                        &s_csip.release_member, 1, &s_csip.granted_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Release start failed for rank %u: %s",
                 s_csip.granted[idx].rank, esp_err_to_name(ret));
        s_csip.granted[idx].retries++;
        if (s_csip.granted[idx].retries >= BT_AUDIO_LE_CSIP_COORD_RELEASE_RETRY_MAX) {
            ESP_LOGE(TAG, "Rank %u stays locked locally until a later release",
                     s_csip.granted[idx].rank);
        }
        return bt_audio_le_csip_coordinator_release_kick();
    }

    s_csip.op_in_progress = true;
    s_csip.release_cb_armed = true;
    bt_audio_le_csip_coordinator_arm_timer();
    ESP_LOGI(TAG, "Release requested for rank %u", s_csip.granted[idx].rank);
    return ESP_OK;
}

static void bt_audio_le_csip_coordinator_release_result(int err)
{
    if (s_csip.release_idx < 0 || (size_t)s_csip.release_idx >= s_csip.granted_count) {
        bt_audio_le_csip_coordinator_release_finish();
        return;
    }

    bt_audio_le_csip_granted_member_t *entry = &s_csip.granted[s_csip.release_idx];
    s_csip.op_in_progress = false;
    s_csip.release_cb_armed = false;
    if (s_csip.lock_timer) {
        esp_timer_stop(s_csip.lock_timer);
    }

    if (err == 0 || err == BT_CSIP_ERROR_LOCK_RELEASE_DENIED) {
        /* 0x81 means this client does not own the lock, so retrying cannot help. */
        entry->released = true;
        entry->retries = 0;
        if (err == 0) {
            ESP_LOGI(TAG, "Released rank %u", entry->rank);
        } else {
            ESP_LOGW(TAG, "Rank %u is locked by another client", entry->rank);
        }
    } else {
        entry->retries++;
        ESP_LOGW(TAG, "Release rank %u failed, err %d (attempt %u/%u)",
                 entry->rank, err, entry->retries, BT_AUDIO_LE_CSIP_COORD_RELEASE_RETRY_MAX);
        if (entry->retries >= BT_AUDIO_LE_CSIP_COORD_RELEASE_RETRY_MAX) {
            ESP_LOGE(TAG, "Rank %u stays locked locally until a later release", entry->rank);
        }
    }
    if (entry->retire && (entry->released ||
            entry->retries >= BT_AUDIO_LE_CSIP_COORD_RELEASE_RETRY_MAX)) {
        entry->active = false;
        entry->member = NULL;
        entry->connected = false;
        entry->retire = false;
    } else if (!entry->connected) {
        entry->member = NULL;
    }

    if (bt_audio_le_csip_coordinator_release_kick() != ESP_OK) {
        bt_audio_le_csip_coordinator_refresh_locked();
    }
}

static void bt_audio_le_csip_coordinator_defer_lock_cb(int err)
{
    s_csip.deferred_lock_cb = s_csip.lock_done_cb;
    s_csip.deferred_lock_err = err;
    s_csip.lock_done_cb = NULL;
    s_csip.lock_cb_armed = false;
    s_csip.release_after_lock = false;

    for (size_t i = 0; i < s_csip.granted_count; i++) {
        if (s_csip.granted[i].active && !s_csip.granted[i].released) {
            s_csip.granted[i].retries = 0;
        }
    }
    bt_audio_le_csip_coordinator_release_kick();
}

static void bt_audio_le_csip_coordinator_on_lock_set(int err)
{
    ESP_LOGI(TAG, "Lock set complete, err %d", err);
    if (!s_csip.initialized) {
        return;
    }
    if (!s_csip.lock_cb_armed) {
        if (err == 0) {
            s_csip.set_locked = true;
            ESP_LOGW(TAG, "Late lock completion updated coordinated-set state");
            bt_audio_le_csip_coordinator_release_granted();
        }
        return;
    }

    s_csip.lock_cb_armed = false;
    s_csip.op_in_progress = false;
    if (s_csip.lock_timer) {
        esp_timer_stop(s_csip.lock_timer);
    }

    if (err == 0) {
        s_csip.set_locked = true;
    } else {
        /* bt_csip_set_coordinator_lock() does not unlock members that already
         * granted the lock. CSIP 4.6.3 requires that release. */
        ESP_LOGW(TAG, "Lock failed, err %d; releasing members that may already be locked", err);
        s_csip.set_locked = true;
    }

    if (err != 0 || s_csip.release_after_lock) {
        bt_audio_le_csip_coordinator_defer_lock_cb(err);
        return;
    }

    bt_audio_le_csip_coordinator_lock_done_cb_t cb = s_csip.lock_done_cb;
    s_csip.lock_done_cb = NULL;
    if (cb) {
        cb(err);
    }
}

static void bt_audio_le_csip_coordinator_on_release_set(int err)
{
    ESP_LOGI(TAG, "Release set complete, err %d", err);
    if (!s_csip.initialized) {
        return;
    }
    if (!s_csip.release_cb_armed) {
        ESP_LOGW(TAG, "Ignoring late release completion, err %d", err);
        if (err == 0 && s_csip.release_idx >= 0 &&
                (size_t)s_csip.release_idx < s_csip.granted_count) {
            s_csip.granted[s_csip.release_idx].released = true;
            bt_audio_le_csip_coordinator_refresh_locked();
        }
        return;
    }
    bt_audio_le_csip_coordinator_release_result(err);
}

static void bt_audio_le_csip_coordinator_on_lock_changed(
    esp_ble_audio_csip_set_coordinator_csis_inst_t *inst, bool locked)
{
    if (!s_csip.initialized) {
        return;
    }
    ESP_LOGI(TAG, "Peer lock changed: rank %u now %s",
             inst ? inst->info.rank : 0, locked ? "locked" : "unlocked");
    if (!inst || locked || !s_csip.granted_valid) {
        return;
    }
    if (memcmp(inst->info.sirk, s_csip.granted_info.sirk, ESP_BLE_AUDIO_CSIP_SIRK_SIZE) != 0) {
        return;
    }

    for (size_t i = 0; i < s_csip.granted_count; i++) {
        bt_audio_le_csip_granted_member_t *entry = &s_csip.granted[i];
        if (!entry->active || entry->released || entry->rank != inst->info.rank) {
            continue;
        }
        entry->released = true;
        ESP_LOGW(TAG, "Rank %u unlocked by the peer", entry->rank);
    }
    bt_audio_le_csip_coordinator_refresh_locked();
    if (!s_csip.op_in_progress && s_csip.set_locked) {
        bt_audio_le_csip_coordinator_release_granted();
    }
}

static void bt_audio_le_csip_coordinator_on_sirk_changed(
    esp_ble_audio_csip_set_coordinator_csis_inst_t *inst)
{
    if (!s_csip.initialized || !inst) {
        return;
    }
    ESP_LOGI(TAG, "SIRK changed for rank %u", inst->info.rank);
    for (size_t i = 0; i < BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS; i++) {
        bt_audio_le_csip_accepted_member_t *entry = &s_csip.accepted[i];

        if (entry->active && entry->info == &inst->info) {
            memcpy(entry->sirk, inst->info.sirk, sizeof(entry->sirk));
        }
    }
    bt_audio_le_csip_coordinator_apply_sirk(inst->info.sirk);
}

static void bt_audio_le_csip_coordinator_on_size_changed(
    struct bt_conn *conn, const esp_ble_audio_csip_set_coordinator_csis_inst_t *inst)
{
    if (!s_csip.initialized || !conn || !inst) {
        return;
    }
    ESP_LOGI(TAG, "Set size changed to %u for rank %u (handle %u)",
             inst->info.set_size, inst->info.rank, conn->handle);
    bt_audio_le_unicast_client_on_set_size_changed(conn->handle, inst->info.set_size);
}

static void bt_audio_le_csip_coordinator_lock_timeout(void *arg)
{
    (void)arg;
    if (!s_csip.op_in_progress) {
        return;
    }
    if (s_csip.releasing && s_csip.release_cb_armed) {
        ESP_LOGW(TAG, "Release operation timed out");
        bt_audio_le_csip_coordinator_release_result(-ETIMEDOUT);
        return;
    }
    if (s_csip.lock_cb_armed) {
        ESP_LOGW(TAG, "Lock operation timed out");
        bt_audio_le_csip_coordinator_on_lock_set(-ETIMEDOUT);
    }
}

static esp_ble_audio_csip_set_coordinator_cb_t s_csip_cbs = {
    .lock_set = bt_audio_le_csip_coordinator_on_lock_set,
    .release_set = bt_audio_le_csip_coordinator_on_release_set,
    .lock_changed = bt_audio_le_csip_coordinator_on_lock_changed,
    .sirk_changed = bt_audio_le_csip_coordinator_on_sirk_changed,
    .size_changed = bt_audio_le_csip_coordinator_on_size_changed,
};

esp_err_t bt_audio_le_csip_coordinator_init(void)
{
    ESP_RETURN_ON_FALSE(!s_csip.initialized, ESP_ERR_INVALID_STATE, TAG,
                        "Already initialized");

    esp_timer_create_args_t timer_args = {
        .callback = bt_audio_le_csip_coordinator_lock_timeout,
        .name = "csip_coord_lock",
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_csip.lock_timer);
    ESP_RETURN_ON_FALSE(ret == ESP_OK, ret, TAG, "Failed to create lock timer");

    if (!s_csip.cb_registered) {
        ret = esp_ble_audio_csip_set_coordinator_register_cb(&s_csip_cbs);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to register CSIP coordinator CBs: %s", esp_err_to_name(ret));
            esp_timer_delete(s_csip.lock_timer);
            s_csip.lock_timer = NULL;
            return ret;
        }
        s_csip.cb_registered = true;
    }
    s_csip.initialized = true;
    ESP_LOGI(TAG, "Initialized");
    return ESP_OK;
}

void bt_audio_le_csip_coordinator_deinit(void)
{
    bt_audio_le_csip_coordinator_cancel_op();
    if (s_csip.lock_timer) {
        esp_timer_delete(s_csip.lock_timer);
        s_csip.lock_timer = NULL;
    }
    memset(s_csip.granted, 0, sizeof(s_csip.granted));
    s_csip.granted_count = 0;
    s_csip.granted_valid = false;
    s_csip.initialized = false;
    s_csip.set_locked = false;
    bt_audio_le_csip_coordinator_clear_active_sirk();
    memset(s_csip.accepted, 0, sizeof(s_csip.accepted));
    memset(s_csip.scan_connect, 0, sizeof(s_csip.scan_connect));
    s_csip.deferred_lock_cb = NULL;
    ESP_LOGI(TAG, "Deinitialized");
}

void bt_audio_le_csip_coordinator_set_sirk(const uint8_t sirk[ESP_BLE_AUDIO_CSIP_SIRK_SIZE])
{
    if (bt_audio_le_csip_coordinator_apply_sirk(sirk)) {
        ESP_LOGI(TAG, "SIRK stored");
    }
}

const uint8_t *bt_audio_le_csip_coordinator_get_sirk(void)
{
    return s_csip.sirk_valid ? s_csip.sirk : NULL;
}

uint32_t bt_audio_le_csip_coordinator_sirk_generation(void)
{
    return s_csip.sirk_generation;
}

void bt_audio_le_csip_coordinator_clear_sirk(void)
{
    bt_audio_le_csip_coordinator_clear_active_sirk();
}

void bt_audio_le_csip_coordinator_note_scan_connect(uint16_t conn_handle, uint32_t generation)
{
    int free_slot = -1;

    for (size_t i = 0; i < BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS; i++) {
        if (s_csip.scan_connect[i].used && s_csip.scan_connect[i].conn_handle == conn_handle) {
            s_csip.scan_connect[i].generation = generation;
            return;
        }
        if (!s_csip.scan_connect[i].used && free_slot < 0) {
            free_slot = (int)i;
        }
    }
    if (free_slot < 0) {
        free_slot = 0;
    }
    s_csip.scan_connect[free_slot] = (bt_audio_le_csip_scan_connect_t) {
        .conn_handle = conn_handle,
        .generation = generation,
        .used = true,
    };
}

void bt_audio_le_csip_coordinator_forget_scan_connect(uint16_t conn_handle)
{
    for (size_t i = 0; i < BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS; i++) {
        if (s_csip.scan_connect[i].used && s_csip.scan_connect[i].conn_handle == conn_handle) {
            s_csip.scan_connect[i].used = false;
        }
    }
}

bool bt_audio_le_csip_coordinator_scan_connect_is_current(uint16_t conn_handle)
{
    for (size_t i = 0; i < BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS; i++) {
        if (!s_csip.scan_connect[i].used || s_csip.scan_connect[i].conn_handle != conn_handle) {
            continue;
        }
        return s_csip.scan_connect[i].generation == s_csip.sirk_generation;
    }
    return true;
}

bool bt_audio_le_csip_coordinator_needs_member(size_t ready_count, size_t expected_count)
{
    return s_csip.sirk_valid && ready_count < expected_count;
}

bool bt_audio_le_csip_coordinator_match_rsi(uint8_t data_type, const uint8_t *data, uint8_t data_len)
{
    return s_csip.sirk_valid &&
           esp_ble_audio_csip_set_coordinator_is_set_member(s_csip.sirk, data_type, data, data_len);
}

esp_err_t bt_audio_le_csip_coordinator_lock(const esp_ble_audio_csip_set_coordinator_set_member_t *members[],
                                            const esp_ble_audio_csip_set_coordinator_set_info_t *set_info,
                                            const uint8_t *ranks,
                                            size_t count,
                                            uint32_t timeout_ms,
                                            bt_audio_le_csip_coordinator_lock_done_cb_t on_done)
{
    ESP_RETURN_ON_FALSE(members && count >= 2, ESP_ERR_INVALID_ARG, TAG,
                        "Need >= 2 members");
    ESP_RETURN_ON_FALSE(count <= BT_AUDIO_LE_CSIP_COORD_MAX_MEMBERS, ESP_ERR_INVALID_ARG, TAG,
                        "Too many members");
    ESP_RETURN_ON_FALSE(set_info, ESP_ERR_INVALID_ARG, TAG, "set_info is NULL");
    ESP_RETURN_ON_FALSE(on_done, ESP_ERR_INVALID_ARG, TAG, "on_done is NULL");
    ESP_RETURN_ON_FALSE(!s_csip.op_in_progress, ESP_ERR_INVALID_STATE, TAG,
                        "Operation already in progress");

    if (!set_info->lockable) {
        ESP_LOGW(TAG, "Set is not lockable, skipping lock");
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t ret = esp_ble_audio_csip_set_coordinator_lock(members, (uint8_t)count, set_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Lock request failed: %s", esp_err_to_name(ret));
        return ret;
    }

    bt_audio_le_csip_coordinator_remember_grant(members, set_info, ranks, count);
    s_csip.lock_done_cb = on_done;
    s_csip.op_in_progress = true;
    s_csip.lock_cb_armed = true;
    s_csip.releasing = false;

    if (timeout_ms == 0) {
        timeout_ms = BT_AUDIO_LE_CSIP_COORDINATOR_LOCK_TIMEOUT_DEFAULT_MS;
    }
    if (s_csip.lock_timer) {
        esp_timer_stop(s_csip.lock_timer);
        esp_timer_start_once(s_csip.lock_timer, timeout_ms * 1000ULL);
    }
    ESP_LOGI(TAG, "Lock requested (%u members, %lums timeout)",
             (unsigned)count, (unsigned long)timeout_ms);
    return ESP_OK;
}

esp_err_t bt_audio_le_csip_coordinator_release_granted(void)
{
    ESP_RETURN_ON_FALSE(s_csip.initialized, ESP_ERR_INVALID_STATE, TAG, "Not initialized");
    if (!s_csip.granted_valid) {
        return ESP_OK;
    }
    if (s_csip.op_in_progress && !s_csip.releasing) {
        s_csip.release_after_lock = true;
        return ESP_OK;
    }
    if (s_csip.releasing) {
        return ESP_OK;
    }

    for (size_t i = 0; i < s_csip.granted_count; i++) {
        if (s_csip.granted[i].active && !s_csip.granted[i].released) {
            s_csip.granted[i].retries = 0;
        }
    }
    s_release_depth++;
    esp_err_t ret = bt_audio_le_csip_coordinator_release_kick();
    s_release_depth--;
    return ret;
}

void bt_audio_le_csip_coordinator_member_disconnected(
    const esp_ble_audio_csip_set_coordinator_set_member_t *member)
{
    int idx = bt_audio_le_csip_coordinator_find_member(member);
    bool tracked = bt_audio_le_csip_coordinator_mark_accepted_disconnected(member);

    if (idx >= 0) {
        bt_audio_le_csip_granted_member_t *entry = &s_csip.granted[idx];

        if (!entry->retire) {
            entry->connected = false;
            if (s_csip.release_member != member) {
                entry->member = NULL;
            }
            ESP_LOGI(TAG, "Granted rank %u disconnected", entry->rank);
            bt_audio_le_csip_coordinator_refresh_locked();

            if (!entry->released) {
                if (s_csip.op_in_progress && !s_csip.releasing) {
                    s_csip.release_after_lock = true;
                } else {
                    bt_audio_le_csip_coordinator_release_granted();
                }
            }
        }
    }

    if (tracked && bt_audio_le_csip_coordinator_accepted_connected() == 0) {
        bt_audio_le_csip_coordinator_clear_active_sirk();
    }
}

void bt_audio_le_csip_coordinator_member_available(
    uint16_t conn_handle,
    const esp_ble_audio_csip_set_coordinator_set_member_t *member,
    const esp_ble_audio_csip_set_coordinator_set_info_t *info)
{
    if (!s_csip.initialized || !member || !info) {
        return;
    }
    bt_audio_le_csip_coordinator_forget_scan_connect(conn_handle);
    if (s_csip.sirk_valid &&
            memcmp(info->sirk, s_csip.sirk, ESP_BLE_AUDIO_CSIP_SIRK_SIZE) != 0) {
        return;
    }
    bt_audio_le_csip_coordinator_accept_member(conn_handle, member, info);
    if (!s_csip.granted_valid) {
        return;
    }
    if (memcmp(info->sirk, s_csip.granted_info.sirk, ESP_BLE_AUDIO_CSIP_SIRK_SIZE) != 0) {
        return;
    }

    int idx = bt_audio_le_csip_coordinator_find_disconnected_rank(info->rank);
    if (idx < 0) {
        return;
    }

    s_csip.granted[idx].member = member;
    s_csip.granted[idx].connected = true;
    s_csip.granted[idx].retries = 0;
    bt_audio_le_csip_coordinator_refresh_locked();
    /* Leave the write to the unicast client. ASE discovery is still using this
     * ACL, and a parallel Unlocked write would collide with it. An in-progress
     * release walk observes the updated member on its next rank. */
    ESP_LOGI(TAG, "Granted rank %u is connected again and still locked", info->rank);
}

void bt_audio_le_csip_coordinator_cancel_op(void)
{
    if (s_csip.lock_timer) {
        esp_timer_stop(s_csip.lock_timer);
    }
    s_csip.op_in_progress = false;
    s_csip.lock_cb_armed = false;
    s_csip.release_cb_armed = false;
    s_csip.releasing = false;
    s_csip.release_after_lock = false;
    s_csip.lock_done_cb = NULL;
    s_csip.deferred_lock_cb = NULL;
    s_csip.retire_only = false;
    s_csip.granted_sirk_refresh = false;
}

bool bt_audio_le_csip_coordinator_is_locked(void)
{
    return s_csip.set_locked;
}

bool bt_audio_le_csip_coordinator_op_in_progress(void)
{
    return s_csip.op_in_progress;
}
