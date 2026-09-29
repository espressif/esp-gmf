/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

#include "esp_ble_audio_cap_api.h"
#include "esp_ble_audio_vcp_api.h"

#include "bt_audio_le_unicast_client.h"
#include "bt_audio_le_vcp_ctlr.h"
#include "bt_audio_ops.h"

#define BT_AUDIO_LE_VCP_CTLR_DEFAULT_MAX   2
#define BT_AUDIO_LE_VCP_INVALID_HANDLE     UINT16_MAX
#define BT_AUDIO_LE_VCP_VOL_SETTING_MIN    0
#define BT_AUDIO_LE_VCP_VOL_SETTING_MAX    255
#define BT_AUDIO_LE_VCP_VOL_PERCENT_STEP   10
#define BT_AUDIO_LE_VCP_VOL_SETTING_STEP \
    ((BT_AUDIO_LE_VCP_VOL_SETTING_MAX * BT_AUDIO_LE_VCP_VOL_PERCENT_STEP + 50) / 100)

/**
 * @brief  Per-peer VCP discovery state.
 */
typedef enum {
    BT_AUDIO_LE_VCP_DISC_IDLE = 0,
    BT_AUDIO_LE_VCP_DISC_PENDING,
    BT_AUDIO_LE_VCP_DISC_READY,
    BT_AUDIO_LE_VCP_DISC_FAILED,
} bt_audio_le_vcp_disc_state_t;

/**
 * @brief  VCP volume controller state for one connected peer.
 */
typedef struct {
    uint16_t                      conn_handle;          /*!< ACL connection handle */
    esp_ble_audio_vcp_vol_ctlr_t *ctlr;                 /*!< VCP volume controller */
    bt_audio_le_vcp_disc_state_t  disc_state;           /*!< Discovery state */
    bool                          state_valid;          /*!< Cached volume and mute are valid */
    uint8_t                       volume;               /*!< Cached VCS volume setting */
    uint8_t                       mute;                 /*!< Cached VCS mute */
    bool                          expect_volume;        /*!< Absolute volume write is outstanding */
    uint8_t                       asked_volume;         /*!< Absolute volume asked of this member */
    bool                          expect_mute;          /*!< Mute write is outstanding */
    uint8_t                       asked_mute;           /*!< Mute value asked of this member */
} bt_audio_le_vcp_ctlr_slot_t;

/**
 * @brief  Runtime context of the VCP volume controller.
 */
typedef struct {
    bt_audio_le_vcp_ctlr_slot_t *slots;                 /*!< Peer slots */
    uint8_t                      max_members;           /*!< Number of peer slots */
    bool                         streams_started;       /*!< Unicast streams started */
    bool                         cb_registered;         /*!< VCP callbacks registered */
    bool                         commander_registered;  /*!< CAP commander callbacks set */
    bool                         vol_ops_installed;     /*!< Volume ops installed */
    bool                         destroying;            /*!< Teardown in progress */
    SemaphoreHandle_t            mutex;                 /*!< Recursive lock */
    esp_bt_audio_vol_ops_t       previous_vol_ops;      /*!< Saved volume ops */
    bool                         set_volume_valid;      /*!< Cached set volume is valid */
    bool                         set_mute_valid;        /*!< Cached set mute is valid */
    uint8_t                      set_volume;            /*!< Set-wide VCS volume setting */
    uint8_t                      set_mute;              /*!< Set-wide VCS mute */
    bool                         mute_follow;           /*!< Mute write waits for the volume procedure */
    uint8_t                      mute_follow_value;     /*!< Mute applied when the volume procedure ends */
} bt_audio_le_vcp_ctlr_ctx_t;

static const char *TAG = "BT_AUD_LE_VCP_CTLR";
static bt_audio_le_vcp_ctlr_ctx_t *s_vcp;

static bool vcp_lock(void)
{
    if (!s_vcp || !s_vcp->mutex) {
        return false;
    }
    if (xSemaphoreTakeRecursive(s_vcp->mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    if (!s_vcp || s_vcp->destroying) {
        xSemaphoreGiveRecursive(s_vcp->mutex);
        return false;
    }
    return true;
}

static void vcp_unlock(void)
{
    if (s_vcp && s_vcp->mutex) {
        xSemaphoreGiveRecursive(s_vcp->mutex);
    }
}

static bt_audio_le_vcp_ctlr_slot_t *slot_by_handle(uint16_t conn_handle)
{
    if (!s_vcp || conn_handle == BT_AUDIO_LE_VCP_INVALID_HANDLE) {
        return NULL;
    }
    for (size_t i = 0; i < s_vcp->max_members; i++) {
        if (s_vcp->slots[i].disc_state != BT_AUDIO_LE_VCP_DISC_IDLE &&
                s_vcp->slots[i].conn_handle == conn_handle) {
            return &s_vcp->slots[i];
        }
    }
    return NULL;
}

static bt_audio_le_vcp_ctlr_slot_t *slot_by_ctlr(esp_ble_audio_vcp_vol_ctlr_t *ctlr)
{
    if (!s_vcp || !ctlr) {
        return NULL;
    }
    for (size_t i = 0; i < s_vcp->max_members; i++) {
        if (s_vcp->slots[i].ctlr == ctlr) {
            return &s_vcp->slots[i];
        }
    }
    return NULL;
}

static bt_audio_le_vcp_ctlr_slot_t *slot_for_discover(esp_ble_audio_vcp_vol_ctlr_t *ctlr)
{
    bt_audio_le_vcp_ctlr_slot_t *slot = slot_by_ctlr(ctlr);

    if (slot || !ctlr) {
        return slot;
    }
    for (size_t i = 0; i < s_vcp->max_members; i++) {
        if (s_vcp->slots[i].disc_state == BT_AUDIO_LE_VCP_DISC_PENDING && s_vcp->slots[i].ctlr == NULL) {
            s_vcp->slots[i].ctlr = ctlr;
            return &s_vcp->slots[i];
        }
    }
    return NULL;
}

static bt_audio_le_vcp_ctlr_slot_t *slot_alloc(void)
{
    if (!s_vcp) {
        return NULL;
    }
    for (size_t i = 0; i < s_vcp->max_members; i++) {
        if (s_vcp->slots[i].disc_state == BT_AUDIO_LE_VCP_DISC_IDLE) {
            return &s_vcp->slots[i];
        }
    }
    return NULL;
}

static bool vcp_discovery_ready_locked(void)
{
    bool pending = false;
    bool failed = false;

    for (size_t i = 0; i < s_vcp->max_members; i++) {
        switch (s_vcp->slots[i].disc_state) {
        case BT_AUDIO_LE_VCP_DISC_READY:
            if (s_vcp->slots[i].ctlr != NULL) {
                return true;
            }
            break;
        case BT_AUDIO_LE_VCP_DISC_PENDING:
            pending = true;
            break;
        case BT_AUDIO_LE_VCP_DISC_FAILED:
            failed = true;
            break;
        default:
            break;
        }
    }
    if (pending) {
        ESP_LOGE(TAG, "Volume control failed: VCP discovery is not complete");
    } else if (failed) {
        ESP_LOGE(TAG, "Volume control failed: VCP is not supported");
    } else {
        ESP_LOGE(TAG, "Volume control failed: VCP discovery has not succeeded");
    }
    return false;
}

static esp_err_t bt_audio_le_vcp_ctlr_change_volume(uint8_t volume)
{
    esp_ble_audio_cap_set_member_t members[BT_AUDIO_LE_VCP_CTLR_DEFAULT_MAX] = {0};
    esp_ble_audio_cap_commander_change_volume_param_t param = {0};
    size_t count = 0;
    esp_err_t ret = bt_audio_le_unicast_client_copy_cap_members(members,
                                                               BT_AUDIO_LE_VCP_CTLR_DEFAULT_MAX,
                                                               &count, &param.type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Change volume failed: no unicast set is ready");
        return ret;
    }

    param.members = members;
    param.count = count;
    param.volume = volume;
    ret = esp_ble_audio_cap_commander_change_volume(&param);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Change volume failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t bt_audio_le_vcp_ctlr_change_mute(uint8_t mute)
{
    esp_ble_audio_cap_set_member_t members[BT_AUDIO_LE_VCP_CTLR_DEFAULT_MAX] = {0};
    esp_ble_audio_cap_commander_change_volume_mute_state_param_t param = {0};
    size_t count = 0;
    esp_err_t ret = bt_audio_le_unicast_client_copy_cap_members(members,
                                                               BT_AUDIO_LE_VCP_CTLR_DEFAULT_MAX,
                                                               &count, &param.type);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Change mute failed: no unicast set is ready");
        return ret;
    }

    param.members = members;
    param.count = count;
    param.mute = mute != ESP_BLE_AUDIO_VCP_STATE_UNMUTED;
    ret = esp_ble_audio_cap_commander_change_volume_mute_state(&param);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Change mute failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

static bool vcp_slot_volume_ready(const bt_audio_le_vcp_ctlr_slot_t *slot)
{
    return slot->disc_state == BT_AUDIO_LE_VCP_DISC_READY && slot->ctlr != NULL;
}

static uint8_t vcp_step_setting(uint8_t volume, bool up)
{
    unsigned sum;

    if (up) {
        if (volume >= BT_AUDIO_LE_VCP_VOL_SETTING_MAX) {
            return BT_AUDIO_LE_VCP_VOL_SETTING_MAX;
        }
        sum = (unsigned)volume + BT_AUDIO_LE_VCP_VOL_SETTING_STEP;
        if (sum > BT_AUDIO_LE_VCP_VOL_SETTING_MAX) {
            return BT_AUDIO_LE_VCP_VOL_SETTING_MAX;
        }
        return (uint8_t)sum;
    }
    if (volume < BT_AUDIO_LE_VCP_VOL_SETTING_STEP) {
        return BT_AUDIO_LE_VCP_VOL_SETTING_MIN;
    }
    return (uint8_t)(volume - BT_AUDIO_LE_VCP_VOL_SETTING_STEP);
}

static void vcp_mark_asked_volume(uint8_t volume)
{
    for (size_t i = 0; i < s_vcp->max_members; i++) {
        bt_audio_le_vcp_ctlr_slot_t *slot = &s_vcp->slots[i];
        if (!vcp_slot_volume_ready(slot)) {
            continue;
        }
        slot->expect_volume = true;
        slot->asked_volume = volume;
    }
}

static void vcp_mark_asked_mute(uint8_t mute)
{
    for (size_t i = 0; i < s_vcp->max_members; i++) {
        bt_audio_le_vcp_ctlr_slot_t *slot = &s_vcp->slots[i];
        if (!vcp_slot_volume_ready(slot)) {
            continue;
        }
        slot->expect_mute = true;
        slot->asked_mute = mute;
    }
}

static void vcp_clear_expect_volume(void)
{
    for (size_t i = 0; i < s_vcp->max_members; i++) {
        s_vcp->slots[i].expect_volume = false;
    }
}

static void vcp_clear_expect_mute(void)
{
    for (size_t i = 0; i < s_vcp->max_members; i++) {
        s_vcp->slots[i].expect_mute = false;
    }
}

static bool vcp_others_need_volume(const bt_audio_le_vcp_ctlr_slot_t *src, uint8_t volume)
{
    for (size_t i = 0; i < s_vcp->max_members; i++) {
        bt_audio_le_vcp_ctlr_slot_t *slot = &s_vcp->slots[i];
        if (slot == src || !vcp_slot_volume_ready(slot)) {
            continue;
        }
        if (!slot->state_valid || slot->volume != volume) {
            return true;
        }
    }
    return false;
}

static bool vcp_others_need_mute(const bt_audio_le_vcp_ctlr_slot_t *src, uint8_t mute)
{
    for (size_t i = 0; i < s_vcp->max_members; i++) {
        bt_audio_le_vcp_ctlr_slot_t *slot = &s_vcp->slots[i];
        if (slot == src || !vcp_slot_volume_ready(slot)) {
            continue;
        }
        if (!slot->state_valid || slot->mute != mute) {
            return true;
        }
    }
    return false;
}

static void vcp_rebuild_set_cache_locked(void)
{
    bool volume_valid = false;
    bool mute_valid = false;

    for (size_t i = 0; i < s_vcp->max_members; i++) {
        bt_audio_le_vcp_ctlr_slot_t *slot = &s_vcp->slots[i];
        if (!vcp_slot_volume_ready(slot) || !slot->state_valid) {
            continue;
        }
        s_vcp->set_volume = slot->volume;
        s_vcp->set_mute = slot->mute;
        volume_valid = true;
        mute_valid = true;
    }
    s_vcp->set_volume_valid = volume_valid;
    s_vcp->set_mute_valid = mute_valid;
}

static size_t vcp_copy_ready_ctlrs(esp_ble_audio_vcp_vol_ctlr_t **ctlrs, size_t max_count)
{
    size_t count = 0;

    for (size_t i = 0; i < s_vcp->max_members && count < max_count; i++) {
        if (!vcp_slot_volume_ready(&s_vcp->slots[i])) {
            continue;
        }
        ctlrs[count++] = s_vcp->slots[i].ctlr;
    }
    return count;
}

static esp_err_t vcp_apply_volume_members(uint8_t volume)
{
    esp_ble_audio_vcp_vol_ctlr_t *ctlrs[BT_AUDIO_LE_VCP_CTLR_DEFAULT_MAX];
    size_t count;
    esp_err_t result = ESP_OK;

    if (!vcp_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    count = vcp_copy_ready_ctlrs(ctlrs, BT_AUDIO_LE_VCP_CTLR_DEFAULT_MAX);
    vcp_unlock();
    if (count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    for (size_t i = 0; i < count; i++) {
        esp_err_t ret = esp_ble_audio_vcp_vol_ctlr_set_vol(ctlrs[i], volume);
        if (ret != ESP_OK) {
            result = ret;
        }
    }
    return result;
}

static esp_err_t vcp_apply_mute_members(uint8_t mute)
{
    esp_ble_audio_vcp_vol_ctlr_t *ctlrs[BT_AUDIO_LE_VCP_CTLR_DEFAULT_MAX];
    size_t count;
    esp_err_t result = ESP_OK;
    bool muted = mute != ESP_BLE_AUDIO_VCP_STATE_UNMUTED;

    if (!vcp_lock()) {
        return ESP_ERR_INVALID_STATE;
    }
    count = vcp_copy_ready_ctlrs(ctlrs, BT_AUDIO_LE_VCP_CTLR_DEFAULT_MAX);
    vcp_unlock();
    if (count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    for (size_t i = 0; i < count; i++) {
        esp_err_t ret = muted ? esp_ble_audio_vcp_vol_ctlr_mute(ctlrs[i])
                              : esp_ble_audio_vcp_vol_ctlr_unmute(ctlrs[i]);
        if (ret != ESP_OK) {
            result = ret;
        }
    }
    return result;
}

static esp_err_t vcp_apply_volume(uint8_t volume)
{
    if (s_vcp && s_vcp->commander_registered) {
        return bt_audio_le_vcp_ctlr_change_volume(volume);
    }
    return vcp_apply_volume_members(volume);
}

static esp_err_t vcp_apply_mute(uint8_t mute)
{
    if (s_vcp && s_vcp->commander_registered) {
        return bt_audio_le_vcp_ctlr_change_mute(mute);
    }
    return vcp_apply_mute_members(mute);
}

static bool vcp_take_mute_follow(uint8_t *mute)
{
    if (!s_vcp->mute_follow) {
        return false;
    }
    *mute = s_vcp->mute_follow_value;
    s_vcp->mute_follow = false;
    vcp_mark_asked_mute(*mute);
    return true;
}

static void vcp_finish_mute_follow(bool follow, uint8_t mute)
{
    esp_err_t ret;

    if (!follow) {
        return;
    }
    ret = vcp_apply_mute(mute);
    if (ret != ESP_OK && vcp_lock()) {
        vcp_clear_expect_mute();
        vcp_unlock();
    }
}

static esp_err_t vcp_commit_volume(uint8_t volume)
{
    esp_err_t ret = vcp_apply_volume(volume);
    bool follow = false;
    uint8_t mute = ESP_BLE_AUDIO_VCP_STATE_UNMUTED;

    if (ret == ESP_OK) {
        return ESP_OK;
    }
    if (vcp_lock()) {
        vcp_clear_expect_volume();
        follow = vcp_take_mute_follow(&mute);
        vcp_unlock();
    }
    vcp_finish_mute_follow(follow, mute);
    return ret;
}

static bool bt_audio_le_vcp_ctlr_ready(void)
{
    /* On success the controller mutex is held and the caller must unlock it. */
    if (!vcp_lock()) {
        ESP_LOGE(TAG, "Volume control failed: VCP controller is not initialized");
        return false;
    }
    if (!s_vcp->streams_started) {
        vcp_unlock();
        ESP_LOGE(TAG, "Volume control failed: VCP controller is not streaming");
        return false;
    }
    if (!vcp_discovery_ready_locked()) {
        vcp_unlock();
        return false;
    }
    return true;
}

static esp_err_t bt_audio_le_vcp_ctlr_set_absolute(uint32_t vol)
{
    uint8_t volume;

    if (vol > 100) {
        vol = 100;
    }
    if (!bt_audio_le_vcp_ctlr_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    volume = (uint8_t)((vol * 255 + 50) / 100);
    s_vcp->set_volume = volume;
    s_vcp->set_volume_valid = true;
    vcp_mark_asked_volume(volume);
    vcp_unlock();
    return vcp_commit_volume(volume);
}

static esp_err_t bt_audio_le_vcp_ctlr_set_relative(bool up_down)
{
    uint8_t prev;
    uint8_t next;
    esp_err_t ret;

    if (!bt_audio_le_vcp_ctlr_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_vcp->set_volume_valid) {
        vcp_unlock();
        ESP_LOGE(TAG, "Relative volume failed: volume state is unknown");
        return ESP_ERR_INVALID_STATE;
    }

    prev = s_vcp->set_volume;
    next = vcp_step_setting(prev, up_down);
    if (next == prev && !vcp_others_need_volume(NULL, next)) {
        vcp_unlock();
        return ESP_OK;
    }
    s_vcp->set_volume = next;
    vcp_mark_asked_volume(next);
    vcp_unlock();
    ret = vcp_commit_volume(next);
    if (ret != ESP_OK && vcp_lock()) {
        if (s_vcp->set_volume == next) {
            s_vcp->set_volume = prev;
        }
        vcp_unlock();
    }
    return ret;
}

static void vcs_discover_cb(esp_ble_audio_vcp_vol_ctlr_t *vol_ctlr, int err,
                            uint8_t vocs_count, uint8_t aics_count)
{
    bt_audio_le_vcp_ctlr_slot_t *slot;
    uint16_t handle;
    esp_err_t read_ret;

    (void)vocs_count;
    (void)aics_count;

    if (!vcp_lock()) {
        if (err) {
            ESP_LOGE(TAG, "VCP discovery cb failed, err %d", err);
        }
        return;
    }

    slot = slot_for_discover(vol_ctlr);
    if (!slot) {
        vcp_unlock();
        ESP_LOGW(TAG, "VCP discovery cb for unknown controller, err %d", err);
        return;
    }

    if (err) {
        ESP_LOGE(TAG, "VCP discovery failed on handle %u, err %d", slot->conn_handle, err);
        slot->ctlr = NULL;
        slot->disc_state = BT_AUDIO_LE_VCP_DISC_FAILED;
        vcp_unlock();
        return;
    }

    slot->disc_state = BT_AUDIO_LE_VCP_DISC_READY;
    handle = slot->conn_handle;
    ESP_LOGI(TAG, "VCP discovered handle %u", handle);
    vcp_unlock();

    read_ret = esp_ble_audio_vcp_vol_ctlr_read_state(vol_ctlr);
    if (read_ret != ESP_OK) {
        ESP_LOGW(TAG, "Read volume state failed on handle %u: %s",
                 handle, esp_err_to_name(read_ret));
    }
}

static void vcs_state_cb(esp_ble_audio_vcp_vol_ctlr_t *vol_ctlr, int err,
                         uint8_t volume, uint8_t mute)
{
    bt_audio_le_vcp_ctlr_slot_t *slot;
    bool vol_echo;
    bool mute_echo;
    bool vol_remote;
    bool mute_remote;
    bool need_vol;
    bool need_mute;
    uint8_t sync_volume;
    uint8_t sync_mute;
    uint16_t handle;

    if (err) {
        ESP_LOGE(TAG, "VCP state cb failed, err %d", err);
        return;
    }
    if (!vcp_lock()) {
        return;
    }

    slot = slot_by_ctlr(vol_ctlr);
    if (!slot || slot->disc_state != BT_AUDIO_LE_VCP_DISC_READY) {
        vcp_unlock();
        return;
    }

    vol_echo = slot->expect_volume && volume == slot->asked_volume;
    mute_echo = slot->expect_mute && mute == slot->asked_mute;
    if (vol_echo) {
        slot->expect_volume = false;
    }
    if (mute_echo) {
        slot->expect_mute = false;
    }

    slot->volume = volume;
    slot->mute = mute;
    slot->state_valid = true;
    handle = slot->conn_handle;

    if (vol_echo && s_vcp->mute_follow && !mute_echo) {
        vcp_unlock();
        return;
    }

    vol_remote = !vol_echo && (!s_vcp->set_volume_valid || volume != s_vcp->set_volume);
    mute_remote = !mute_echo && (!s_vcp->set_mute_valid || mute != s_vcp->set_mute);
    if (vol_remote) {
        s_vcp->set_volume = volume;
        s_vcp->set_volume_valid = true;
    }
    if (mute_remote) {
        s_vcp->set_mute = mute;
        s_vcp->set_mute_valid = true;
    }

    need_vol = vol_remote && vcp_others_need_volume(slot, s_vcp->set_volume);
    need_mute = mute_remote && vcp_others_need_mute(slot, s_vcp->set_mute);
    if (!need_vol && !need_mute) {
        vcp_unlock();
        return;
    }

    sync_volume = s_vcp->set_volume;
    sync_mute = s_vcp->set_mute;
    if (need_vol) {
        vcp_mark_asked_volume(sync_volume);
        if (need_mute) {
            s_vcp->mute_follow = true;
            s_vcp->mute_follow_value = sync_mute;
        }
    } else {
        vcp_mark_asked_mute(sync_mute);
    }
    vcp_unlock();

    ESP_LOGI(TAG, "Sync set volume %u mute %u after handle %u", sync_volume, sync_mute, handle);
    if (need_vol) {
        vcp_commit_volume(sync_volume);
        return;
    }
    if (vcp_apply_mute(sync_mute) != ESP_OK && vcp_lock()) {
        vcp_clear_expect_mute();
        vcp_unlock();
    }
}

static void vcs_write_cb(esp_ble_audio_vcp_vol_ctlr_t *vol_ctlr, int err)
{
    if (err) {
        ESP_LOGE(TAG, "VCP write cb failed, err %d", err);
    }
}

static esp_ble_audio_vcp_vol_ctlr_cb_t s_vcp_cbs = {
    .discover = vcs_discover_cb,
    .vol_down = vcs_write_cb,
    .vol_up = vcs_write_cb,
    .mute = vcs_write_cb,
    .unmute = vcs_write_cb,
    .vol_down_unmute = vcs_write_cb,
    .vol_up_unmute = vcs_write_cb,
    .vol_set = vcs_write_cb,
    .state = vcs_state_cb,
};

static void volume_changed_cb(esp_ble_conn_t *conn, int err)
{
    bool follow = false;
    uint8_t mute = ESP_BLE_AUDIO_VCP_STATE_UNMUTED;

    if (err) {
        ESP_LOGE(TAG, "Set volume failed on handle %u, err %d",
                 conn ? conn->handle : BT_AUDIO_LE_VCP_INVALID_HANDLE, err);
    }
    if (!vcp_lock()) {
        return;
    }
    if (err) {
        vcp_clear_expect_volume();
    }
    follow = vcp_take_mute_follow(&mute);
    vcp_unlock();
    vcp_finish_mute_follow(follow, mute);
}

static void volume_mute_changed_cb(esp_ble_conn_t *conn, int err)
{
    if (!err) {
        return;
    }
    ESP_LOGE(TAG, "Set volume mute failed on handle %u, err %d",
             conn ? conn->handle : BT_AUDIO_LE_VCP_INVALID_HANDLE, err);
    if (vcp_lock()) {
        vcp_clear_expect_mute();
        vcp_unlock();
    }
}

static const esp_ble_audio_cap_commander_cb_t s_cap_commander_cbs = {
    .volume_changed = volume_changed_cb,
    .volume_mute_changed = volume_mute_changed_cb,
};

esp_err_t bt_audio_le_vcp_ctlr_init(uint8_t max_members)
{
    esp_err_t ret;

    ESP_RETURN_ON_FALSE(!s_vcp, ESP_ERR_INVALID_STATE, TAG, "Already initialized");
    if (max_members == 0 || max_members > BT_AUDIO_LE_VCP_CTLR_DEFAULT_MAX) {
        max_members = BT_AUDIO_LE_VCP_CTLR_DEFAULT_MAX;
    }

    s_vcp = heap_caps_calloc_prefer(1, sizeof(*s_vcp), 2,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                    MALLOC_CAP_DEFAULT);
    if (!s_vcp) {
        ESP_LOGE(TAG, "No memory for VCP ctlr");
        return ESP_ERR_NO_MEM;
    }

    s_vcp->slots = heap_caps_calloc_prefer(max_members, sizeof(*s_vcp->slots), 2,
                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                           MALLOC_CAP_DEFAULT);
    if (!s_vcp->slots) {
        ESP_LOGE(TAG, "Init VCP controller failed: no memory for peer slots");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }
    s_vcp->mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_vcp->mutex) {
        ESP_LOGE(TAG, "Init VCP controller failed: no memory for mutex");
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }
    s_vcp->max_members = max_members;

    ret = esp_ble_audio_vcp_vol_ctlr_cb_register(&s_vcp_cbs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init VCP controller failed: register callbacks failed: %s",
                 esp_err_to_name(ret));
        goto fail;
    }
    s_vcp->cb_registered = true;

    ret = esp_ble_audio_cap_commander_register_cb(&s_cap_commander_cbs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init VCP controller failed: register commander callbacks failed: %s",
                 esp_err_to_name(ret));
        goto fail;
    }
    s_vcp->commander_registered = true;

    esp_bt_audio_vol_ops_t vol_ops = {0};
    ret = bt_audio_ops_get_vol(&vol_ops);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init VCP controller failed: get volume ops failed: %s",
                 esp_err_to_name(ret));
        goto fail;
    }
    s_vcp->previous_vol_ops = vol_ops;
    vol_ops.set_absolute = bt_audio_le_vcp_ctlr_set_absolute;
    vol_ops.set_relative = bt_audio_le_vcp_ctlr_set_relative;
    ret = bt_audio_ops_set_vol(&vol_ops);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init VCP controller failed: set volume ops failed: %s",
                 esp_err_to_name(ret));
        goto fail;
    }
    s_vcp->vol_ops_installed = true;

    ESP_LOGI(TAG, "VCP volume controller initialized");
    return ESP_OK;
fail:
    bt_audio_le_vcp_ctlr_deinit();
    return ret;
}

void bt_audio_le_vcp_ctlr_deinit(void)
{
    if (!s_vcp) {
        return;
    }
    if (s_vcp->vol_ops_installed) {
        bt_audio_ops_set_vol(&s_vcp->previous_vol_ops);
        s_vcp->vol_ops_installed = false;
    }
    if (s_vcp->commander_registered) {
        esp_ble_audio_cap_commander_unregister_cb(&s_cap_commander_cbs);
        s_vcp->commander_registered = false;
    }
    if (s_vcp->cb_registered) {
        esp_ble_audio_vcp_vol_ctlr_cb_unregister(&s_vcp_cbs);
        s_vcp->cb_registered = false;
    }
    if (s_vcp->mutex) {
        xSemaphoreTakeRecursive(s_vcp->mutex, portMAX_DELAY);
        s_vcp->destroying = true;
        xSemaphoreGiveRecursive(s_vcp->mutex);
        vSemaphoreDelete(s_vcp->mutex);
        s_vcp->mutex = NULL;
    }
    heap_caps_free(s_vcp->slots);
    heap_caps_free(s_vcp);
    s_vcp = NULL;
    ESP_LOGI(TAG, "VCP volume controller deinitialized");
}

esp_err_t bt_audio_le_vcp_ctlr_discover(uint16_t conn_handle)
{
    if (!vcp_lock()) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (conn_handle == BT_AUDIO_LE_VCP_INVALID_HANDLE) {
        vcp_unlock();
        ESP_LOGE(TAG, "Invalid handle");
        return ESP_ERR_INVALID_ARG;
    }

    bt_audio_le_vcp_ctlr_slot_t *existing = slot_by_handle(conn_handle);
    if (existing) {
        bt_audio_le_vcp_disc_state_t state = existing->disc_state;
        vcp_unlock();
        if (state == BT_AUDIO_LE_VCP_DISC_FAILED) {
            ESP_LOGW(TAG, "VCP not supported on handle %u", conn_handle);
            return ESP_ERR_NOT_SUPPORTED;
        }
        return ESP_OK;
    }

    bt_audio_le_vcp_ctlr_slot_t *slot = slot_alloc();
    if (!slot) {
        vcp_unlock();
        ESP_LOGE(TAG, "No VCP ctlr slot");
        return ESP_ERR_NO_MEM;
    }

    slot->conn_handle = conn_handle;
    slot->ctlr = NULL;
    slot->disc_state = BT_AUDIO_LE_VCP_DISC_PENDING;

    esp_ble_audio_vcp_vol_ctlr_t *vol_ctlr = NULL;
    esp_err_t err = esp_ble_audio_vcp_vol_ctlr_discover(conn_handle, &vol_ctlr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "VCP discover failed handle %u: %s", conn_handle, esp_err_to_name(err));
        memset(slot, 0, sizeof(*slot));
        vcp_unlock();
        return err;
    }
    if (slot->disc_state == BT_AUDIO_LE_VCP_DISC_PENDING && slot->ctlr == NULL) {
        slot->ctlr = vol_ctlr;
    }
    vcp_unlock();
    ESP_LOGI(TAG, "VCP discovering handle %u", conn_handle);
    return ESP_OK;
}

void bt_audio_le_vcp_ctlr_forget(uint16_t conn_handle)
{
    if (!vcp_lock()) {
        return;
    }
    bt_audio_le_vcp_ctlr_slot_t *slot = slot_by_handle(conn_handle);
    if (slot) {
        memset(slot, 0, sizeof(*slot));
        vcp_rebuild_set_cache_locked();
    }
    vcp_unlock();
}

static void bt_audio_le_vcp_ctlr_set_streams_started(bool started)
{
    if (vcp_lock()) {
        s_vcp->streams_started = started;
        vcp_unlock();
    }
}

void bt_audio_le_vcp_ctlr_streams_started(void)
{
    bt_audio_le_vcp_ctlr_set_streams_started(true);
}

void bt_audio_le_vcp_ctlr_streams_stopped(void)
{
    bt_audio_le_vcp_ctlr_set_streams_started(false);
}
