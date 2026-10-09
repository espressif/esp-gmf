/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <errno.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "sdkconfig.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "esp_ble_audio_bap_api.h"
#include "esp_ble_audio_cap_api.h"
#include "esp_ble_audio_codec_api.h"
#include "esp_ble_audio_csip_api.h"
#include "esp_ble_audio_defs.h"
#include "esp_ble_iso_common_api.h"

#include "esp_bt_audio_defs.h"
#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_le_stream.h"
#include "bt_audio_le_tx_group.h"
#include "bt_audio_le_unicast_client.h"
#include "bt_audio_le_csip_coordinator.h"
#if CONFIG_BT_VCP_VOL_CTLR && CONFIG_BT_CAP_COMMANDER
#include "bt_audio_le_vcp_ctlr.h"
#endif
#include "bt_audio_le_lc3_preset.h"

#define BT_AUDIO_LE_UC_INVALID_HANDLE       UINT16_MAX
#define BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS  2
#define BT_AUDIO_LE_UC_MAX_CIS              2
#define BT_AUDIO_LE_UC_NO_PEER              UINT8_MAX
#define BT_AUDIO_LE_UC_SINK_EP_MAX          CONFIG_BT_BAP_UNICAST_CLIENT_ASE_SNK_COUNT
#if BT_AUDIO_LE_UC_SINK_EP_MAX < 1
#error "BAP Unicast Client requires at least one Sink ASE"
#endif
#define BT_AUDIO_LE_UC_SET_WAIT_TIMEOUT_US  (10ULL * 1000ULL * 1000ULL)
#define BT_AUDIO_LE_UC_START_TIMEOUT_MS     10000
#define BT_AUDIO_LE_UC_SECONDARY_TIMEOUT_MS 6000
#define BT_AUDIO_LE_UC_SECONDARY_DELAY_MS   1000
#define BT_AUDIO_LE_UC_SECONDARY_RETRY_MS   1500
#define BT_AUDIO_LE_UC_SECONDARY_MAX_RETRY  2
#define BT_AUDIO_LE_UC_STOP_TIMEOUT_MS      5000
#define BT_AUDIO_LE_UC_STOP_POLL_MS         250
#define BT_AUDIO_LE_UC_RELEASE_MAX_RETRY    3
#define BT_AUDIO_LE_UC_LOCK_TIMEOUT_MS      5000
#define BT_AUDIO_LE_UC_LC3_PRESET_NONE      (-1)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))
#endif

typedef enum {
    UC_STATE_IDLE,
    UC_STATE_DISCOVERING,
    UC_STATE_SET_ASSEMBLING,
    UC_STATE_LOCKING,
    UC_STATE_STARTING,
    UC_STATE_STREAMING,
    UC_STATE_STOPPING,
} uc_state_t;

/**
 * @brief  Binding of one CIS to a peer sink endpoint.
 */
typedef struct {
    uint8_t                  peer;        /*!< Index into peers[], or BT_AUDIO_LE_UC_NO_PEER when reserved */
    uint8_t                  sink_idx;    /*!< Sink endpoint of that peer */
    uint8_t                  preset;      /*!< Unicast preset index, or 0xFF when unset */
    esp_ble_audio_location_t chan_alloc;  /*!< Channels this CIS carries, 0 for mono */
} uc_cis_binding_t;

/**
 * @brief  Discovered state of one potential unicast set member.
 */
typedef struct {
    esp_ble_conn_t                                         *conn;                                 /*!< Connection */
    uint16_t                                               conn_handle;                           /*!< ACL handle */
    uint8_t                                                sink_ep_count;                         /*!< Sink ASE count */
    esp_ble_audio_bap_ep_t                                 *sink_eps[BT_AUDIO_LE_UC_SINK_EP_MAX]; /*!< Sink endpoints */
    esp_ble_audio_location_t                               sink_loc;                              /*!< Sink locations */
    uint8_t                                                sink_max_chan;                         /*!< Max channels */
    esp_ble_audio_context_t                                avail_snk_ctx;                         /*!< Available sink contexts */
    esp_ble_audio_context_t                                supp_snk_ctx;                          /*!< Supported sink contexts */
    esp_ble_audio_context_t                                stream_ctx;                            /*!< Context written into the codec config */
    esp_ble_audio_context_t                                pref_snk_ctx;                          /*!< Preferred contexts of usable PAC records */
    bool                                                   avail_ctx_known;                       /*!< Available Audio Contexts was read */
    bool                                                   supp_ctx_known;                        /*!< Supported Audio Contexts was read */
    bool                                                   pref_known;                            /*!< A usable PAC record carried preferred context */
    uint32_t                                               lc3_ch1;                               /*!< Preset bits covered with one channel */
    uint32_t                                               lc3_ch2;                               /*!< Preset bits covered with two channels */
    bool                                                   admit_blocked;                         /*!< Admission failed for this start attempt */
    const esp_ble_audio_csip_set_coordinator_set_member_t  *member;                               /*!< CSIP member */
    esp_ble_audio_csip_set_coordinator_csis_inst_t         *csis_inst;                            /*!< CSIS instance */
    bool                                                   used;                                  /*!< Slot in use */
    bool                                                   ase_ready;                             /*!< ASEs ready */
} uc_peer_t;

/**
 * @brief  Runtime context of the CAP initiator unicast client.
 */
typedef struct {
    uc_peer_t                          peers[BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS]; /*!< Peer slots */
    bt_audio_le_stream_t               *sink_streams[BT_AUDIO_LE_UC_MAX_CIS];     /*!< Sink streams */
    bt_audio_le_tx_group_t             *tx_group;                                 /*!< Shared CIG transmit pacer */
    uc_cis_binding_t                   bindings[BT_AUDIO_LE_UC_MAX_CIS];          /*!< CIS bindings */
    uint8_t                            peer_count;                                /*!< Max peers */
    uint8_t                            cis_count;                                 /*!< Active CIS count */
    uint8_t                            expected_members;                          /*!< Members awaited */
    uc_state_t                         state;                                     /*!< Client state */
    esp_timer_handle_t                 state_timer;                               /*!< Start/stop timer */
    esp_ble_audio_cap_unicast_group_t  *unicast_group;                            /*!< CAP group */
    uint8_t                            group_cis_count;                           /*!< Group CIS count */
    uint16_t                           discovering_handle;                        /*!< Discovering now */
    uint16_t                           pending_handles[BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS]; /*!< Queued handles */
    size_t                             discover_pending;                          /*!< Pending count */
    bool                               cap_cb_registered;                         /*!< CAP callbacks set */
    bool                               bap_cb_registered;                         /*!< BAP callbacks set */
    bool                               start_requested;                           /*!< Start pending */
    bool                               set_partial;                               /*!< Partial set */
    bool                               user_stopping;                             /*!< Stop pending */
    uint8_t                            secondary_retry_count;                     /*!< CIS retries */
    bool                               start_member_fallback;                     /*!< Joint start failed; trying one member */
    uint8_t                            start_fallback_mask;                       /*!< CIS indexes already tried alone */
    bool                               secondary_stage_logged;                    /*!< Staged-start reason already logged */
    bool                               group_needs_rebuild;                       /*!< Rebuild pending */
    bool                               destroying;                                /*!< Teardown in progress */
    bool                               csip_resume_start;                         /*!< Start waited for CSIP release */
    bool                               csip_release_gave_up;                      /*!< One leaked-lock release already attempted */
    bool                               context_wait_expired;                      /*!< Context discovery wait already elapsed */
    int8_t                             lc3_preset;                                /*!< Selected preset, or BT_AUDIO_LE_UC_LC3_PRESET_NONE */
    int8_t                             group_preset;                              /*!< Preset baked into the current CIG */
    esp_ble_audio_codec_cfg_t          start_codec_cfg[BT_AUDIO_LE_UC_MAX_CIS];   /*!< Per-CIS codec config for the active start */
    esp_ble_audio_bap_qos_cfg_t        start_qos[BT_AUDIO_LE_UC_MAX_CIS];         /*!< Per-CIS QoS for the active group */
    uint8_t                            start_codec_data[BT_AUDIO_LE_UC_MAX_CIS][CONFIG_BT_AUDIO_CODEC_CFG_MAX_DATA_SIZE]; /*!< Writable codec data */
    uint8_t                            start_codec_meta[BT_AUDIO_LE_UC_MAX_CIS][CONFIG_BT_AUDIO_CODEC_CFG_MAX_METADATA_SIZE]; /*!< Writable metadata copies */
    uint8_t                            stop_pending_mask;                         /*!< Streams awaiting BAP release */
    uint8_t                            stop_release_requested_mask;               /*!< Streams with Release submitted */
    uint8_t                            stop_release_attempts[BT_AUDIO_LE_UC_MAX_CIS]; /*!< Release attempts per stream */
    int64_t                            stop_started_us;                           /*!< Stop procedure start time */
    bool                               cap_stop_active;                           /*!< CAP stop procedure is running */
    bool                               stop_timeout_reported;                     /*!< Stop timeout was logged */
    esp_timer_handle_t                 secondary_timer;                           /*!< Secondary CIS timer */
    SemaphoreHandle_t                  mutex;                                     /*!< Recursive lock */
} uc_ctx_t;

static const char *TAG = "BT_AUD_LE_UC";
static uc_ctx_t *s_uc;

/* BAP unicast presets the initiator can emit, best first. Above 48 kHz is omitted.
 * Location and context are placeholders. Each CIS copies the preset into
 * start_codec_* before overwriting channel allocation or stream context.
 * The macro QoS SDU is one channel. Templates are const; see bt_audio_le_lc3_preset.h.
 */
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_6_1_DEFINE(s_lc3_48_6_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_6_2_DEFINE(s_lc3_48_6_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_5_1_DEFINE(s_lc3_48_5_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_5_2_DEFINE(s_lc3_48_5_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_4_1_DEFINE(s_lc3_48_4_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_4_2_DEFINE(s_lc3_48_4_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_3_1_DEFINE(s_lc3_48_3_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_3_2_DEFINE(s_lc3_48_3_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_2_1_DEFINE(s_lc3_48_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_2_2_DEFINE(s_lc3_48_2_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_1_1_DEFINE(s_lc3_48_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_48_1_2_DEFINE(s_lc3_48_1_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_441_2_1_DEFINE(s_lc3_441_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                    ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_441_2_2_DEFINE(s_lc3_441_2_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                    ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_441_1_1_DEFINE(s_lc3_441_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                    ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_441_1_2_DEFINE(s_lc3_441_1_2, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                    ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_32_2_1_DEFINE(s_lc3_32_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_32_1_1_DEFINE(s_lc3_32_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_24_2_1_DEFINE(s_lc3_24_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_24_1_1_DEFINE(s_lc3_24_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_16_2_1_DEFINE(s_lc3_16_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_16_1_1_DEFINE(s_lc3_16_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                   ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_8_2_1_DEFINE(s_lc3_8_2_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                  ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);
ESP_BLE_AUDIO_BAP_LC3_UNICAST_PRESET_8_1_1_DEFINE(s_lc3_8_1_1, ESP_BLE_AUDIO_LOCATION_MONO_AUDIO,
                                                  ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA);

static const esp_ble_audio_bap_lc3_preset_t *const s_lc3_presets[] = {
    &s_lc3_48_6_2, &s_lc3_48_6_1, &s_lc3_48_5_2, &s_lc3_48_5_1,
    &s_lc3_48_4_2, &s_lc3_48_4_1, &s_lc3_48_3_2, &s_lc3_48_3_1,
    &s_lc3_48_2_2, &s_lc3_48_2_1, &s_lc3_48_1_2, &s_lc3_48_1_1,
    &s_lc3_441_2_2, &s_lc3_441_2_1, &s_lc3_441_1_2, &s_lc3_441_1_1,
    &s_lc3_32_2_1, &s_lc3_32_1_1, &s_lc3_24_2_1, &s_lc3_24_1_1,
    &s_lc3_16_2_1, &s_lc3_16_1_1, &s_lc3_8_2_1, &s_lc3_8_1_1,
};

#define BT_AUDIO_LE_UC_LC3_PRESET_COUNT  ARRAY_SIZE(s_lc3_presets)
_Static_assert(BT_AUDIO_LE_UC_LC3_PRESET_COUNT <= 32, "LC3 preset mask is 32 bits");

static void bt_audio_le_uc_log_selected(uint8_t preset)
{
    const esp_ble_audio_codec_cfg_t *cfg = &s_lc3_presets[preset]->codec_cfg;
    esp_ble_audio_codec_cfg_freq_t freq = 0;
    uint32_t hz = 0;
    uint16_t octets = 0;

    esp_ble_audio_codec_cfg_get_freq(cfg, &freq);
    esp_ble_audio_codec_cfg_freq_to_freq_hz(freq, &hz);
    esp_ble_audio_codec_cfg_get_octets_per_frame(cfg, &octets);
    ESP_LOGI(TAG, "Selected LC3 %lu Hz, %u octets/frame", (unsigned long)hz, octets);
}

static bool bt_audio_le_uc_lock(void)
{
    if (!s_uc || !s_uc->mutex) {
        return false;
    }
    if (xSemaphoreTakeRecursive(s_uc->mutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    if (!s_uc || s_uc->destroying) {
        xSemaphoreGiveRecursive(s_uc->mutex);
        return false;
    }
    return true;
}

static void bt_audio_le_uc_unlock(void)
{
    if (s_uc && s_uc->mutex) {
        xSemaphoreGiveRecursive(s_uc->mutex);
    }
}

static inline uint32_t uc_lowest_chan(uint32_t loc)
{
    return loc & (~loc + 1u);
}

static uint8_t bt_audio_le_uc_normalize_set_size(uint8_t set_size)
{
    if (set_size == 0) {
        set_size = 1;
    }
    if (set_size > s_uc->peer_count) {
        ESP_LOGW(TAG, "Set size %u exceeds the %u reserved member slots; clamping",
                 set_size, s_uc->peer_count);
        set_size = s_uc->peer_count;
    }
    return set_size;
}

static const char *uc_state_str(uc_state_t state)
{
    static const char *const names[] = {
        "IDLE", "DISCOVERING", "SET_ASSEMBLING", "LOCKING",
        "STARTING", "STREAMING", "STOPPING",
    };
    return state < ARRAY_SIZE(names) ? names[state] : "UNKNOWN";
}

static void bt_audio_le_uc_set_state(uc_state_t next)
{
    if (!s_uc || s_uc->state == next) {
        return;
    }
    s_uc->state = next;
}

static void bt_audio_le_uc_state_timer_stop(void)
{
    if (s_uc && s_uc->state_timer) {
        esp_timer_stop(s_uc->state_timer);
    }
}

static void bt_audio_le_uc_state_timer_start(uint64_t timeout_us)
{
    if (s_uc && s_uc->state_timer) {
        esp_timer_stop(s_uc->state_timer);
        esp_timer_start_once(s_uc->state_timer, timeout_us);
    }
}

#define BT_AUDIO_LE_UC_WANTED_CHANNELS \
    (ESP_BLE_AUDIO_LOCATION_FRONT_LEFT | ESP_BLE_AUDIO_LOCATION_FRONT_RIGHT)

static const char *uc_chan_str(uint32_t loc)
{
    switch (loc) {
        case ESP_BLE_AUDIO_LOCATION_FRONT_LEFT:
            return "L";
        case ESP_BLE_AUDIO_LOCATION_FRONT_RIGHT:
            return "R";
        case BT_AUDIO_LE_UC_WANTED_CHANNELS:
            return "L+R";
        default:
            return "mono";
    }
}

static bool bt_audio_le_uc_peer_is_ready(const uc_peer_t *peer)
{
    return peer && peer->used && peer->conn && peer->sink_ep_count > 0 && peer->ase_ready;
}

static size_t bt_audio_le_uc_group_members(uint8_t *idx, size_t max)
{
    size_t count = 0;

    for (size_t i = 0; i < s_uc->peer_count && count < max; i++) {
        uc_peer_t *peer = &s_uc->peers[i];

        if (!bt_audio_le_uc_peer_is_ready(peer) || peer->admit_blocked) {
            continue;
        }
        if (count > 0) {
            const uc_peer_t *first = &s_uc->peers[idx[0]];
            if (!peer->csis_inst || !first->csis_inst) {
                ESP_LOGI(TAG, "Handle %u is not a coordinated-set member; left out of the group",
                         peer->conn_handle);
                continue;
            }
            if (memcmp(peer->csis_inst->info.sirk, first->csis_inst->info.sirk,
                       ESP_BLE_AUDIO_CSIP_SIRK_SIZE) != 0) {
                ESP_LOGW(TAG, "Handle %u belongs to a different CSIP set; left out of the group",
                         peer->conn_handle);
                continue;
            }
        }
        idx[count++] = (uint8_t)i;
    }
    return count;
}

static inline bool bt_audio_le_uc_binding_is_bound(const uc_cis_binding_t *b)
{
    return b && b->peer != BT_AUDIO_LE_UC_NO_PEER;
}

static size_t bt_audio_le_uc_bound_count(void)
{
    size_t count = 0;

    if (!s_uc) {
        return 0;
    }
    for (size_t i = 0; i < s_uc->cis_count; i++) {
        count += bt_audio_le_uc_binding_is_bound(&s_uc->bindings[i]) ? 1 : 0;
    }
    return count;
}

static void bt_audio_le_uc_bindings_clear(void)
{
    s_uc->cis_count = 0;
    s_uc->lc3_preset = BT_AUDIO_LE_UC_LC3_PRESET_NONE;
    for (size_t i = 0; i < BT_AUDIO_LE_UC_MAX_CIS; i++) {
        s_uc->bindings[i] = (uc_cis_binding_t){
            .peer = BT_AUDIO_LE_UC_NO_PEER,
            .preset = 0xFF,
        };
    }
}

static void bt_audio_le_uc_binding_add(uint8_t peer, uint8_t sink_idx, uint32_t chan_alloc)
{
    if (s_uc->cis_count >= BT_AUDIO_LE_UC_MAX_CIS) {
        return;
    }
    s_uc->bindings[s_uc->cis_count++] = (uc_cis_binding_t){
        .peer = peer,
        .sink_idx = sink_idx,
        .preset = 0xFF,
        .chan_alloc = (esp_ble_audio_location_t)chan_alloc,
    };
}

static void bt_audio_le_uc_binding_reserve(uint32_t chan_alloc)
{
    bt_audio_le_uc_binding_add(BT_AUDIO_LE_UC_NO_PEER, 0, chan_alloc);
}

typedef enum {
    UC_ADMIT_OK = 0,
    UC_ADMIT_CTX_UNKNOWN,
    UC_ADMIT_CTX_UNAVAILABLE,
    UC_ADMIT_CODEC,
    UC_ADMIT_LOCATION,
} uc_admit_t;

/* CAP 7.3.1.2.1: a supported context that is currently unavailable ends the
 * procedure on that acceptor. Unspecified may replace the use-case context
 * only when Supported Audio Contexts does not contain it and Available Audio
 * Contexts does contain Unspecified. A missing read is not availability. */
static uc_admit_t bt_audio_le_uc_peer_select_context(const uc_peer_t *peer,
                                                     esp_ble_audio_context_t *ctx)
{
    if (!peer->avail_ctx_known) {
        return UC_ADMIT_CTX_UNKNOWN;
    }
    if (peer->avail_snk_ctx & ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA) {
        *ctx = ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA;
        return UC_ADMIT_OK;
    }
    if (peer->supp_ctx_known &&
            (peer->supp_snk_ctx & ESP_BLE_AUDIO_CONTEXT_TYPE_MEDIA) == 0 &&
            (peer->avail_snk_ctx & ESP_BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED)) {
        *ctx = ESP_BLE_AUDIO_CONTEXT_TYPE_UNSPECIFIED;
        return UC_ADMIT_OK;
    }
    if (!peer->supp_ctx_known) {
        return UC_ADMIT_CTX_UNKNOWN;
    }
    return UC_ADMIT_CTX_UNAVAILABLE;
}

static bool bt_audio_le_uc_context_pending(void)
{
    for (size_t i = 0; i < s_uc->peer_count; i++) {
        const uc_peer_t *peer = &s_uc->peers[i];
        esp_ble_audio_context_t ctx = 0;

        if (!peer->used || !peer->ase_ready || peer->admit_blocked) {
            continue;
        }
        if (bt_audio_le_uc_peer_select_context(peer, &ctx) == UC_ADMIT_CTX_UNKNOWN) {
            return true;
        }
    }
    return false;
}

static const char *uc_admit_str(uc_admit_t why)
{
    switch (why) {
        case UC_ADMIT_CTX_UNKNOWN:
            return "audio contexts not discovered";
        case UC_ADMIT_CTX_UNAVAILABLE:
            return "Media unavailable";
        case UC_ADMIT_CODEC:
            return "no LC3 config at or below 48 kHz and 2 channels";
        case UC_ADMIT_LOCATION:
            return "sink location covers no free channel";
        default:
            return "ok";
    }
}

static esp_bt_audio_unicast_reject_reason_t uc_reject_reason(uc_admit_t why)
{
    switch (why) {
        case UC_ADMIT_CTX_UNKNOWN:
            return ESP_BT_AUDIO_UNICAST_REJECT_CONTEXT_UNKNOWN;
        case UC_ADMIT_CTX_UNAVAILABLE:
            return ESP_BT_AUDIO_UNICAST_REJECT_CONTEXT_UNAVAILABLE;
        case UC_ADMIT_LOCATION:
            return ESP_BT_AUDIO_UNICAST_REJECT_LOCATION;
        case UC_ADMIT_CODEC:
        default:
            return ESP_BT_AUDIO_UNICAST_REJECT_CODEC;
    }
}

static void bt_audio_le_uc_post_reject(uc_peer_t *peer, uc_admit_t why)
{
    esp_bt_audio_event_unicast_rejected_t evt = {
        .conn_handle = peer->conn_handle,
        .reason = uc_reject_reason(why),
    };

    peer->admit_blocked = true;
    ESP_LOGW(TAG, "Handle %u rejected (%s), available 0x%04x supported 0x%04x",
             peer->conn_handle, uc_admit_str(why),
             (unsigned)peer->avail_snk_ctx, (unsigned)peer->supp_snk_ctx);
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_UNICAST_REJECTED, &evt);
}

static bool bt_audio_le_uc_peer_supports(const uc_peer_t *peer, uint8_t preset, bool two_chan)
{
    uint32_t mask = two_chan ? peer->lc3_ch2 : peer->lc3_ch1;

    return preset < BT_AUDIO_LE_UC_LC3_PRESET_COUNT && (mask & (1u << preset)) != 0;
}

/* A multi-channel claim can use a 2-channel config. One channel per CIS needs
 * the 1-channel bit. Configs above 48 kHz or 2 channels are not in the mask. */
static bool bt_audio_le_uc_peer_can_use(const uc_peer_t *peer, uint32_t claim, uint8_t preset)
{
    if (__builtin_popcount(claim) > 1 && peer->sink_max_chan >= 2 &&
            bt_audio_le_uc_peer_supports(peer, preset, true)) {
        return true;
    }
    return bt_audio_le_uc_peer_supports(peer, preset, false);
}

/* Best common preset. A preset every peer can use wins immediately; otherwise
 * the best preset that the most peers can use. */
static int bt_audio_le_uc_select_preset(const uint8_t *idx, const uint32_t *claim, size_t count)
{
    int best = BT_AUDIO_LE_UC_LC3_PRESET_NONE;
    size_t best_count = 0;

    if (count == 0) {
        return BT_AUDIO_LE_UC_LC3_PRESET_NONE;
    }
    for (uint8_t preset = 0; preset < BT_AUDIO_LE_UC_LC3_PRESET_COUNT; preset++) {
        size_t supported = 0;

        for (size_t i = 0; i < count; i++) {
            if (bt_audio_le_uc_peer_can_use(&s_uc->peers[idx[i]], claim[i], preset)) {
                supported++;
            }
        }
        if (supported == count) {
            return preset;
        }
        if (supported > best_count) {
            best_count = supported;
            best = preset;
        }
    }
    return best;
}

static void bt_audio_le_uc_binding_note_preset(uint8_t preset)
{
    if (s_uc->cis_count == 0) {
        return;
    }
    s_uc->bindings[s_uc->cis_count - 1].preset = preset;
}

static bool bt_audio_le_uc_ase_is_idle(const uc_peer_t *peer, uint8_t sink_idx)
{
    esp_ble_audio_bap_ep_info_t info = {0};

    if (sink_idx >= peer->sink_ep_count || peer->sink_eps[sink_idx] == NULL) {
        return false;
    }
    if (esp_ble_audio_bap_ep_get_info(peer->sink_eps[sink_idx], &info) != ESP_OK) {
        return false;
    }
    return info.state == ESP_BLE_AUDIO_BAP_EP_STATE_IDLE;
}

static bool bt_audio_le_uc_sink_idx_bound(uint8_t peer_idx, uint8_t sink_idx)
{
    for (size_t i = 0; i < s_uc->cis_count; i++) {
        const uc_cis_binding_t *b = &s_uc->bindings[i];

        if (b->peer == peer_idx && b->sink_idx == sink_idx) {
            return true;
        }
    }
    return false;
}

static uint32_t bt_audio_le_uc_ase_location(const uc_peer_t *peer, uint8_t sink_idx)
{
    uint32_t loc = (uint32_t)peer->sink_loc;

    if (loc == ESP_BLE_AUDIO_LOCATION_MONO_AUDIO || peer->sink_ep_count <= 1 ||
            __builtin_popcount(loc) <= 1) {
        return loc;
    }
    for (uint8_t i = 0; i < sink_idx; i++) {
        uint32_t lowest = uc_lowest_chan(loc);

        if (lowest == 0) {
            return 0;
        }
        loc &= ~lowest;
    }
    return uc_lowest_chan(loc);
}

static bool bt_audio_le_uc_ase_supports(const uc_peer_t *peer, uint32_t alloc, uint8_t preset)
{
    unsigned channels = __builtin_popcount(alloc);

    if (channels <= 1) {
        return bt_audio_le_uc_peer_supports(peer, preset, false);
    }
    return peer->sink_max_chan >= channels && bt_audio_le_uc_peer_supports(peer, preset, true);
}

static bool bt_audio_le_uc_ase_location_matches(const uc_peer_t *peer, uint8_t sink_idx,
                                               uint32_t alloc, bool allow_mono)
{
    unsigned channels = __builtin_popcount(alloc);
    uint32_t ase_loc;

    if ((uint32_t)peer->sink_loc == ESP_BLE_AUDIO_LOCATION_MONO_AUDIO) {
        return allow_mono;
    }
    if (channels == 0) {
        channels = 1;
    }
    ase_loc = bt_audio_le_uc_ase_location(peer, sink_idx);
    if (ase_loc == 0) {
        return false;
    }
    if (channels > 1) {
        return peer->sink_max_chan >= channels && ((uint32_t)peer->sink_loc & alloc) == alloc;
    }
    if (peer->sink_ep_count > 1 && __builtin_popcount((uint32_t)peer->sink_loc) > 1) {
        return ase_loc == alloc;
    }
    return (ase_loc & alloc) == alloc;
}

static int bt_audio_le_uc_pick_ase(uint8_t peer_idx, uint32_t alloc, uint8_t preset, bool allow_mono)
{
    const uc_peer_t *peer = &s_uc->peers[peer_idx];

    if (preset >= BT_AUDIO_LE_UC_LC3_PRESET_COUNT ||
            !bt_audio_le_uc_ase_supports(peer, alloc, preset)) {
        return -1;
    }
    for (uint8_t sink_idx = 0; sink_idx < peer->sink_ep_count; sink_idx++) {
        if (bt_audio_le_uc_sink_idx_bound(peer_idx, sink_idx) ||
                !bt_audio_le_uc_ase_is_idle(peer, sink_idx) ||
                !bt_audio_le_uc_ase_location_matches(peer, sink_idx, alloc, allow_mono)) {
            continue;
        }
        return sink_idx;
    }
    return -1;
}

static void bt_audio_le_uc_bindings_resolve(void)
{
    uint8_t idx[BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS] = {0};
    size_t members = bt_audio_le_uc_group_members(idx, ARRAY_SIZE(idx));
    uint32_t remaining = BT_AUDIO_LE_UC_WANTED_CHANNELS;
    size_t awaited = (s_uc->expected_members > members) ? s_uc->expected_members - members : 0;

    uint8_t elig[BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS] = {0};
    uint32_t claims[BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS] = {0};
    size_t elig_count = 0;
    uint32_t cursor = remaining;

    bt_audio_le_uc_bindings_clear();

    for (size_t m = 0; m < members && cursor; m++) {
        uc_peer_t *peer = &s_uc->peers[idx[m]];
        esp_ble_audio_context_t stream_ctx = 0;
        uc_admit_t why = bt_audio_le_uc_peer_select_context(peer, &stream_ctx);
        uint32_t claim;

        if (why == UC_ADMIT_CTX_UNKNOWN && !s_uc->context_wait_expired) {
            ESP_LOGI(TAG, "Handle %u audio contexts not discovered yet", peer->conn_handle);
            continue;
        }
        if (why != UC_ADMIT_OK) {
            bt_audio_le_uc_post_reject(peer, why);
            continue;
        }
        peer->stream_ctx = stream_ctx;
        if (peer->pref_known && (peer->pref_snk_ctx & stream_ctx) == 0) {
            ESP_LOGI(TAG, "Handle %u PAC prefers 0x%04x; using stream context 0x%04x",
                     peer->conn_handle, (unsigned)peer->pref_snk_ctx, (unsigned)stream_ctx);
        }

        if (peer->sink_loc == ESP_BLE_AUDIO_LOCATION_MONO_AUDIO) {
            claim = (members == 1 && awaited == 0) ? cursor : uc_lowest_chan(cursor);
            ESP_LOGW(TAG, "Handle %u reports no sink location; assuming %s",
                     peer->conn_handle, uc_chan_str(claim));
        } else {
            claim = (uint32_t)peer->sink_loc & cursor;
            if (claim == 0) {
                bt_audio_le_uc_post_reject(peer, UC_ADMIT_LOCATION);
                continue;
            }
        }
        elig[elig_count] = idx[m];
        claims[elig_count] = claim;
        elig_count++;
        cursor &= ~claim;
    }

    s_uc->lc3_preset = (int8_t)bt_audio_le_uc_select_preset(elig, claims, elig_count);
    if (s_uc->lc3_preset >= 0) {
        bt_audio_le_uc_log_selected((uint8_t)s_uc->lc3_preset);
    }

    for (size_t i = 0; i < elig_count && remaining; i++) {
        uc_peer_t *peer = &s_uc->peers[elig[i]];
        uint32_t claim = claims[i] & remaining;
        uint8_t preset = (uint8_t)s_uc->lc3_preset;

        if (claim == 0) {
            continue;
        }
        if (s_uc->lc3_preset < 0 || !bt_audio_le_uc_peer_can_use(peer, claim, preset)) {
            bt_audio_le_uc_post_reject(peer, UC_ADMIT_CODEC);
            continue;
        }
        if (__builtin_popcount(claim) > 1 && peer->sink_max_chan >= 2 &&
                bt_audio_le_uc_peer_supports(peer, preset, true)) {
            int ase = bt_audio_le_uc_pick_ase(elig[i], claim, preset, true);

            if (ase >= 0) {
                size_t before = s_uc->cis_count;
                bt_audio_le_uc_binding_add(elig[i], (uint8_t)ase, claim);
                if (s_uc->cis_count > before) {
                    bt_audio_le_uc_binding_note_preset(preset);
                    remaining &= ~claim;
                    continue;
                }
            }
        }

        size_t placed_from = s_uc->cis_count;
        while (claim && s_uc->cis_count < BT_AUDIO_LE_UC_MAX_CIS) {
            uint32_t bit = uc_lowest_chan(claim);
            int ase = bt_audio_le_uc_pick_ase(elig[i], bit, preset, true);
            size_t before;

            claim &= ~bit;
            if (ase < 0) {
                continue;
            }
            before = s_uc->cis_count;
            bt_audio_le_uc_binding_add(elig[i], (uint8_t)ase, bit);
            if (s_uc->cis_count == before) {
                continue;
            }
            bt_audio_le_uc_binding_note_preset(preset);
            remaining &= ~bit;
        }
        if (s_uc->cis_count == placed_from && placed_from < BT_AUDIO_LE_UC_MAX_CIS) {
            bt_audio_le_uc_post_reject(peer, UC_ADMIT_LOCATION);
        }
    }

    for (size_t i = 0; i < awaited && remaining && s_uc->cis_count < BT_AUDIO_LE_UC_MAX_CIS; i++) {
        uint32_t bit = uc_lowest_chan(remaining);
        bt_audio_le_uc_binding_reserve(bit);
        remaining &= ~bit;
    }

    for (size_t i = 0; i < s_uc->cis_count; i++) {
        const uc_cis_binding_t *b = &s_uc->bindings[i];
        if (!bt_audio_le_uc_binding_is_bound(b)) {
            ESP_LOGI(TAG, "[SNK #%zu] reserved for a member still to join, carries %s", i,
                     uc_chan_str((uint32_t)b->chan_alloc));
            continue;
        }
        ESP_LOGI(TAG, "[SNK #%zu] handle %u ASE %u carries %s", i,
                 s_uc->peers[b->peer].conn_handle, b->sink_idx,
                 uc_chan_str((uint32_t)b->chan_alloc));
    }
    if (remaining) {
        ESP_LOGI(TAG, "No sink for channel %s; the source downmixes", uc_chan_str(remaining));
    }
}

static int bt_audio_le_uc_binding_of_peer(uint8_t peer)
{
    for (size_t i = 0; i < s_uc->cis_count; i++) {
        if (s_uc->bindings[i].peer == peer) {
            return (int)i;
        }
    }
    return -1;
}

static bool bt_audio_le_uc_any_stream_live(void);
static uint8_t bt_audio_le_uc_binding_preset(const uc_cis_binding_t *binding);

static bool bt_audio_le_uc_bindings_claim_reserved(void)
{
    uint8_t idx[BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS] = {0};
    size_t members;
    bool claimed = false;

    if (!bt_audio_le_uc_any_stream_live()) {
        bt_audio_le_uc_bindings_resolve();
        return bt_audio_le_uc_bound_count() > 0;
    }

    members = bt_audio_le_uc_group_members(idx, ARRAY_SIZE(idx));
    for (size_t m = 0; m < members; m++) {
        uc_peer_t *peer = &s_uc->peers[idx[m]];
        esp_ble_audio_context_t stream_ctx = 0;
        uc_admit_t why;
        bool placed = false;

        if (bt_audio_le_uc_binding_of_peer(idx[m]) >= 0) {
            continue;
        }
        why = bt_audio_le_uc_peer_select_context(peer, &stream_ctx);
        if (why == UC_ADMIT_CTX_UNKNOWN && !s_uc->context_wait_expired) {
            continue;
        }
        if (why != UC_ADMIT_OK) {
            bt_audio_le_uc_post_reject(peer, why);
            continue;
        }

        for (size_t slot = 0; slot < s_uc->cis_count; slot++) {
            uc_cis_binding_t *b = &s_uc->bindings[slot];
            uint32_t alloc;
            uint8_t preset;
            int ase;

            if (bt_audio_le_uc_binding_is_bound(b)) {
                continue;
            }
            alloc = (uint32_t)b->chan_alloc;
            preset = bt_audio_le_uc_binding_preset(b);
            ase = bt_audio_le_uc_pick_ase(idx[m], alloc, preset, false);
            if (ase < 0) {
                continue;
            }
            peer->stream_ctx = stream_ctx;
            b->preset = preset;
            b->peer = idx[m];
            b->sink_idx = (uint8_t)ase;
            placed = true;
            claimed = true;
            ESP_LOGI(TAG, "[SNK #%zu] handle %u ASE %u takes the reserved %s CIS", slot,
                     peer->conn_handle, b->sink_idx, uc_chan_str(alloc));
        }
        if (!placed) {
            ESP_LOGW(TAG, "Handle %u has no ASE matching a reserved slot; left unstarted",
                     peer->conn_handle);
            bt_audio_le_uc_post_reject(peer, UC_ADMIT_LOCATION);
        }
    }
    return claimed;
}

static void bt_audio_le_uc_bindings_unbind_peer(uint8_t peer)
{
    for (size_t i = 0; i < s_uc->cis_count; i++) {
        if (s_uc->bindings[i].peer != peer) {
            continue;
        }
        s_uc->bindings[i].peer = BT_AUDIO_LE_UC_NO_PEER;
        s_uc->bindings[i].sink_idx = 0;
    }
}

static bool bt_audio_le_uc_has_unclaimed_member(void)
{
    uint8_t idx[BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS] = {0};
    size_t members;

    if (!s_uc || bt_audio_le_uc_bound_count() >= s_uc->cis_count) {
        return false;
    }
    members = bt_audio_le_uc_group_members(idx, ARRAY_SIZE(idx));
    for (size_t m = 0; m < members; m++) {
        if (bt_audio_le_uc_binding_of_peer(idx[m]) < 0) {
            return true;
        }
    }
    return false;
}

static void bt_audio_le_uc_peer_reset(uc_peer_t *peer)
{
    if (!peer) {
        return;
    }
    memset(peer, 0, sizeof(*peer));
    peer->conn_handle = BT_AUDIO_LE_UC_INVALID_HANDLE;
}

static uc_peer_t *bt_audio_le_uc_peer_by_handle(uint16_t conn_handle)
{
    if (!s_uc || conn_handle == BT_AUDIO_LE_UC_INVALID_HANDLE) {
        return NULL;
    }
    for (size_t i = 0; i < s_uc->peer_count; i++) {
        if (s_uc->peers[i].used && s_uc->peers[i].conn_handle == conn_handle) {
            return &s_uc->peers[i];
        }
    }
    return NULL;
}

static uc_peer_t *bt_audio_le_uc_peer_alloc(uint16_t conn_handle)
{
    uc_peer_t *peer = bt_audio_le_uc_peer_by_handle(conn_handle);
    if (peer) {
        return peer;
    }
    for (size_t i = 0; i < s_uc->peer_count; i++) {
        if (!s_uc->peers[i].used) {
            peer = &s_uc->peers[i];
            bt_audio_le_uc_peer_reset(peer);
            peer->used = true;
            peer->conn_handle = conn_handle;
            return peer;
        }
    }
    return NULL;
}

static size_t bt_audio_le_uc_ready_count(void)
{
    size_t count = 0;
    if (!s_uc) {
        return 0;
    }
    for (size_t i = 0; i < s_uc->peer_count; i++) {
        if (s_uc->peers[i].used && s_uc->peers[i].ase_ready) {
            count++;
        }
    }
    return count;
}

static bool bt_audio_le_uc_ready_outranks(const uc_peer_t *peer, const uc_peer_t *best)
{
    if (!peer->csis_inst) {
        return false;
    }
    if (!best->csis_inst) {
        return true;
    }
    return peer->csis_inst->info.rank < best->csis_inst->info.rank;
}

static uc_peer_t *bt_audio_le_uc_lowest_rank_ready(void)
{
    uc_peer_t *best = NULL;

    if (!s_uc) {
        return NULL;
    }
    for (size_t i = 0; i < s_uc->peer_count; i++) {
        uc_peer_t *peer = &s_uc->peers[i];

        if (!peer->used || !peer->ase_ready) {
            continue;
        }
        if (!best || bt_audio_le_uc_ready_outranks(peer, best)) {
            best = peer;
        }
    }
    return best;
}

static void bt_audio_le_uc_on_members_complete(void)
{
    if (bt_audio_le_uc_ready_count() >= s_uc->expected_members) {
        bt_audio_le_uc_state_timer_stop();
        s_uc->set_partial = false;
    }
}

static size_t bt_audio_le_uc_used_count(void)
{
    size_t count = 0;
    if (!s_uc) {
        return 0;
    }
    for (size_t i = 0; i < s_uc->peer_count; i++) {
        count += s_uc->peers[i].used ? 1 : 0;
    }
    return count;
}

static uint8_t bt_audio_le_uc_binding_preset(const uc_cis_binding_t *binding)
{
    if (binding->preset < BT_AUDIO_LE_UC_LC3_PRESET_COUNT) {
        return binding->preset;
    }
    if (s_uc->lc3_preset >= 0) {
        return (uint8_t)s_uc->lc3_preset;
    }
    return BT_AUDIO_LE_UC_LC3_PRESET_COUNT;
}

static esp_err_t bt_audio_le_uc_fill_qos(size_t cis)
{
    const uc_cis_binding_t *binding = &s_uc->bindings[cis];
    uint8_t preset = bt_audio_le_uc_binding_preset(binding);
    unsigned channels;
    const esp_ble_audio_bap_lc3_preset_t *src;

    if (preset >= BT_AUDIO_LE_UC_LC3_PRESET_COUNT) {
        return ESP_ERR_INVALID_STATE;
    }
    src = s_lc3_presets[preset];
    channels = __builtin_popcount((uint32_t)binding->chan_alloc);
    if (channels == 0) {
        channels = 1;
    }
    s_uc->start_qos[cis] = src->qos;
    s_uc->start_qos[cis].sdu = (uint16_t)(src->qos.sdu * channels);
    return ESP_OK;
}

#if CONFIG_BT_MCS
extern bool media_proxy_local_player_is_registered(void);
extern uint8_t media_proxy_sctrl_get_content_ctrl_id(void);
#endif

static esp_err_t bt_audio_le_uc_append_ccid_list(esp_ble_audio_codec_cfg_t *codec_cfg, size_t meta_cap)
{
    static bool ccid_warned;
#if CONFIG_BT_MCS
    uint8_t ccid;
    const size_t ltv_len = 2U + sizeof(ccid);

    if (media_proxy_local_player_is_registered()) {
        ccid = media_proxy_sctrl_get_content_ctrl_id();
        ESP_RETURN_ON_FALSE(codec_cfg->meta_len <= meta_cap &&
                            meta_cap - codec_cfg->meta_len >= ltv_len,
                            ESP_ERR_INVALID_SIZE, TAG, "No room for CCID list in CIS metadata");
        if (esp_ble_audio_codec_cfg_meta_set_ccid_list(codec_cfg, &ccid, sizeof(ccid)) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to append CCID list to CIS metadata");
            return ESP_ERR_INVALID_SIZE;
        }
        return ESP_OK;
    }
#else
    (void)codec_cfg;
    (void)meta_cap;
#endif
    if (!ccid_warned) {
        ccid_warned = true;
        ESP_LOGW(TAG, "MCS content control ID unavailable, omitting CCID list");
    }
    return ESP_OK;
}

static esp_err_t bt_audio_le_uc_fill_codec(size_t cis, esp_ble_audio_context_t stream_ctx)
{
    const uc_cis_binding_t *binding = &s_uc->bindings[cis];
    uint8_t preset = bt_audio_le_uc_binding_preset(binding);
    const esp_ble_audio_codec_cfg_t *src;

    if (preset >= BT_AUDIO_LE_UC_LC3_PRESET_COUNT || stream_ctx == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    src = &s_lc3_presets[preset]->codec_cfg;
    if (!src->data || !src->meta || src->data_len > sizeof(s_uc->start_codec_data[cis]) ||
            src->meta_len > sizeof(s_uc->start_codec_meta[cis])) {
        return ESP_ERR_INVALID_SIZE;
    }
    memcpy(s_uc->start_codec_data[cis], src->data, src->data_len);
    memcpy(s_uc->start_codec_meta[cis], src->meta, src->meta_len);
    s_uc->start_codec_cfg[cis] = *src;
    s_uc->start_codec_cfg[cis].data = s_uc->start_codec_data[cis];
    s_uc->start_codec_cfg[cis].meta = s_uc->start_codec_meta[cis];
    ESP_RETURN_ON_ERROR(esp_ble_audio_codec_cfg_set_chan_allocation(&s_uc->start_codec_cfg[cis],
                                                                    binding->chan_alloc),
                        TAG, "Failed to set CIS channel allocation");
    ESP_RETURN_ON_ERROR(esp_ble_audio_codec_cfg_meta_set_stream_context(&s_uc->start_codec_cfg[cis], stream_ctx),
                        TAG, "Failed to set CIS stream context");
    return bt_audio_le_uc_append_ccid_list(&s_uc->start_codec_cfg[cis],
                                            sizeof(s_uc->start_codec_meta[cis]));
}

static bool bt_audio_le_uc_stream_is_live(const bt_audio_le_stream_t *stream)
{
    esp_ble_audio_bap_ep_info_t ep_info = {0};
    esp_ble_iso_info_t iso_info = {0};

    if (!stream || stream->bap_stream.conn == NULL || stream->bap_stream.ep == NULL ||
            stream->bap_stream.iso == NULL) {
        return false;
    }
    if (esp_ble_audio_bap_ep_get_info(stream->bap_stream.ep, &ep_info) != 0) {
        return false;
    }
    if (ep_info.state == ESP_BLE_AUDIO_BAP_EP_STATE_IDLE) {
        return false;
    }
    return esp_ble_iso_chan_get_info(stream->bap_stream.iso, &iso_info) == ESP_OK &&
           iso_info.can_send;
}

static void bt_audio_le_uc_peers_reorder(void)
{
    uc_peer_t ordered[BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS];
    bool taken[BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS] = {0};

    memset(ordered, 0, sizeof(ordered));
    for (size_t i = 0; i < s_uc->peer_count; i++) {
        bt_audio_le_uc_peer_reset(&ordered[i]);
    }

    for (size_t slot = 0; slot < s_uc->peer_count; slot++) {
        int best = -1;
        uint8_t best_rank = UINT8_MAX;

        for (size_t i = 0; i < s_uc->peer_count; i++) {
            uc_peer_t *peer = &s_uc->peers[i];
            if (!peer->used || taken[i] || !peer->csis_inst) {
                continue;
            }
            if (peer->csis_inst->info.rank < best_rank) {
                best_rank = peer->csis_inst->info.rank;
                best = (int)i;
            }
        }
        if (best < 0) {
            break;
        }
        ordered[slot] = s_uc->peers[best];
        taken[best] = true;
    }

    size_t next = 0;
    while (next < s_uc->peer_count && ordered[next].used) {
        next++;
    }
    for (size_t i = 0; i < s_uc->peer_count && next < s_uc->peer_count; i++) {
        if (!s_uc->peers[i].used || taken[i]) {
            continue;
        }
        ordered[next++] = s_uc->peers[i];
    }

    memcpy(s_uc->peers, ordered, sizeof(s_uc->peers));
}

static esp_ble_audio_cap_set_type_t bt_audio_le_uc_set_type(void)
{
    size_t ready = bt_audio_le_uc_ready_count();
    if (ready == 0) {
        return ESP_BLE_AUDIO_CAP_SET_TYPE_AD_HOC;
    }
    for (size_t i = 0; i < s_uc->peer_count; i++) {
        if (!s_uc->peers[i].used || !s_uc->peers[i].ase_ready) {
            continue;
        }
        if (s_uc->peers[i].csis_inst == NULL) {
            return ESP_BLE_AUDIO_CAP_SET_TYPE_AD_HOC;
        }
    }
    return ESP_BLE_AUDIO_CAP_SET_TYPE_CSIP;
}

static bool bt_audio_le_uc_fill_set_member(esp_ble_audio_cap_set_member_t *member,
                                           esp_ble_audio_cap_set_type_t type,
                                           uint16_t conn_handle)
{
    uc_peer_t *peer = bt_audio_le_uc_peer_by_handle(conn_handle);
    if (!peer || !member) {
        return false;
    }
    if (type == ESP_BLE_AUDIO_CAP_SET_TYPE_CSIP) {
        member->csip = peer->csis_inst;
        return member->csip != NULL;
    }
    member->member = peer->conn;
    return member->member != NULL;
}

static bool bt_audio_le_uc_any_stream_live(void)
{
    if (!s_uc) {
        return false;
    }
    for (size_t i = 0; i < s_uc->cis_count; i++) {
        if (bt_audio_le_uc_stream_is_live(s_uc->sink_streams[i])) {
            return true;
        }
    }
    return false;
}

static bool bt_audio_le_uc_groups_delete(void)
{
    if (!s_uc->unicast_group) {
        s_uc->group_cis_count = 0;
        s_uc->group_preset = BT_AUDIO_LE_UC_LC3_PRESET_NONE;
        return true;
    }
    esp_err_t err = esp_ble_audio_cap_unicast_group_delete(s_uc->unicast_group);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete unicast group: %s", esp_err_to_name(err));
        return false;
    }
    s_uc->unicast_group = NULL;
    s_uc->group_cis_count = 0;
    s_uc->group_preset = BT_AUDIO_LE_UC_LC3_PRESET_NONE;
    ESP_LOGI(TAG, "Deleted unicast group");
    return true;
}

static esp_err_t bt_audio_le_uc_group_build(void)
{
    esp_ble_audio_cap_unicast_group_stream_param_t tx_params[BT_AUDIO_LE_UC_MAX_CIS] = {0};
    esp_ble_audio_cap_unicast_group_stream_pair_param_t pair_params[BT_AUDIO_LE_UC_MAX_CIS] = {0};

    for (size_t i = 0; i < s_uc->cis_count; i++) {
        const uc_cis_binding_t *b = &s_uc->bindings[i];

        if (bt_audio_le_uc_binding_is_bound(b)) {
            const uc_peer_t *peer = &s_uc->peers[b->peer];

            if (!peer->used || b->sink_idx >= peer->sink_ep_count) {
                ESP_LOGE(TAG, "[SNK #%zu] Peer %u missing sink endpoint %u",
                         i, b->peer, b->sink_idx);
                return ESP_ERR_INVALID_STATE;
            }
        }

        if (bt_audio_le_uc_fill_qos(i) != ESP_OK) {
            ESP_LOGE(TAG, "[SNK #%zu] No LC3 preset for this CIS", i);
            return ESP_ERR_INVALID_STATE;
        }
        tx_params[i].qos_cfg = &s_uc->start_qos[i];
        tx_params[i].stream = &s_uc->sink_streams[i]->cap_stream;
        pair_params[i].tx_param = &tx_params[i];
    }

    esp_ble_audio_cap_unicast_group_param_t group_param = {
        .packing = ESP_BLE_ISO_PACKING_SEQUENTIAL,
        .params_count = s_uc->cis_count,
        .params = pair_params,
    };
    esp_err_t err = esp_ble_audio_cap_unicast_group_create(&group_param,
                                                          &s_uc->unicast_group);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create unicast group: %s", esp_err_to_name(err));
        return err;
    }
    s_uc->group_cis_count = s_uc->cis_count;
    s_uc->group_preset = s_uc->lc3_preset;
    return ESP_OK;
}

static esp_err_t bt_audio_le_uc_group_create(void)
{
    if (s_uc->unicast_group && !bt_audio_le_uc_any_stream_live() &&
            (s_uc->group_cis_count != s_uc->cis_count || s_uc->group_preset != s_uc->lc3_preset)) {
        ESP_LOGI(TAG, "Rebuilding unicast group, CIS %u -> %u",
                 s_uc->group_cis_count, s_uc->cis_count);
        if (!bt_audio_le_uc_groups_delete()) {
            return ESP_FAIL;
        }
    }
    if (s_uc->unicast_group) {
        return ESP_OK;
    }

    esp_err_t err = bt_audio_le_uc_group_build();
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Created 1 unicast group for %u CIS", s_uc->cis_count);
    return ESP_OK;
}

static esp_err_t bt_audio_le_uc_group_rebuild(void)
{
    if (!s_uc->unicast_group) {
        return bt_audio_le_uc_group_build();
    }
    if (bt_audio_le_uc_any_stream_live()) {
        ESP_LOGI(TAG, "Keeping unicast group, a CIS is still live");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_ble_audio_cap_unicast_group_delete(s_uc->unicast_group);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Group delete before retry failed: %s", esp_err_to_name(err));
        return err;
    }
    s_uc->unicast_group = NULL;

    err = bt_audio_le_uc_group_build();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Rebuilt unicast group for retry");
    }
    return err;
}

static void bt_audio_le_uc_groups_rebuild_released(void)
{
    if (!s_uc || !s_uc->group_needs_rebuild) {
        return;
    }
    if (bt_audio_le_uc_group_rebuild() == ESP_OK) {
        s_uc->group_needs_rebuild = false;
    }
}

static esp_err_t bt_audio_le_uc_audio_start(int only_cis)
{
    esp_ble_audio_cap_unicast_audio_start_stream_param_t stream_params[BT_AUDIO_LE_UC_MAX_CIS] = {0};
    esp_ble_audio_cap_unicast_audio_start_param_t param = {0};

    param.type = bt_audio_le_uc_set_type();

    for (size_t i = 0; i < s_uc->cis_count; i++) {
        const uc_cis_binding_t *b = &s_uc->bindings[i];

        if (only_cis >= 0 && (int)i != only_cis) {
            continue;
        }
        if (!bt_audio_le_uc_binding_is_bound(b)) {
            continue;
        }
        if (bt_audio_le_uc_stream_is_live(s_uc->sink_streams[i])) {
            ESP_LOGI(TAG, "[SNK #%zu] Already streaming, left alone", i);
            continue;
        }
        const uc_peer_t *peer = &s_uc->peers[b->peer];

        if (!bt_audio_le_uc_fill_set_member(&stream_params[param.count].member,
                                            param.type, peer->conn_handle)) {
            ESP_LOGE(TAG, "No set member for handle %u", peer->conn_handle);
            return ESP_ERR_NOT_FOUND;
        }
        if (bt_audio_le_uc_fill_codec(i, peer->stream_ctx) != ESP_OK) {
            ESP_LOGE(TAG, "Handle %u has no admitted LC3 configuration", peer->conn_handle);
            return ESP_ERR_INVALID_STATE;
        }

        stream_params[param.count].stream = &s_uc->sink_streams[i]->cap_stream;
        stream_params[param.count].ep = peer->sink_eps[b->sink_idx];
        stream_params[param.count].codec_cfg = &s_uc->start_codec_cfg[i];
        param.count++;
    }

    if (param.count == 0) {
        size_t reserved = s_uc->cis_count - bt_audio_le_uc_bound_count();
        if (reserved > 0) {
            ESP_LOGI(TAG, "Nothing left to start; %u CIS held for the member(s) still to join",
                     (unsigned)reserved);
        } else {
            ESP_LOGW(TAG, "Nothing left to start");
        }
        return ESP_ERR_INVALID_STATE;
    }

    param.stream_params = stream_params;
    esp_err_t err = esp_ble_audio_cap_initiator_unicast_audio_start(&param);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start unicast audio: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Started %u unicast stream(s) as %s set", (unsigned)param.count,
             (param.type == ESP_BLE_AUDIO_CAP_SET_TYPE_CSIP) ? "a coordinated" : "an ad-hoc");
    return ESP_OK;
}

static size_t bt_audio_le_uc_collect_csip_members(
    const esp_ble_audio_csip_set_coordinator_set_member_t **members,
    const esp_ble_audio_csip_set_coordinator_set_info_t **set_info,
    uint8_t *ranks, size_t max_count)
{
    size_t count = 0;
    *set_info = NULL;
    for (size_t i = 0; i < s_uc->peer_count && count < max_count; i++) {
        uc_peer_t *peer = &s_uc->peers[i];
        if (!peer->used || !peer->ase_ready || !peer->member || !peer->csis_inst) {
            continue;
        }
        if (ranks) {
            ranks[count] = peer->csis_inst->info.rank;
        }
        members[count++] = peer->member;
        if (*set_info == NULL) {
            *set_info = &peer->csis_inst->info;
        }
    }
    return count;
}

static esp_err_t bt_audio_le_uc_release_lock(void)
{
    /* Release the members that actually granted the lock. A later collect of
     * ASE-ready peers drops a disconnected member and used to skip the release
     * entirely when only one member was left. */
    return bt_audio_le_csip_coordinator_release_granted();
}

static bool bt_audio_le_uc_try_other_member(int failed_cis);
static void bt_audio_le_uc_start_give_up(void);

static void bt_audio_le_uc_peers_ready(void)
{
    if (!s_uc || !s_uc->start_requested) {
        return;
    }

    if (bt_audio_le_uc_ready_count() == 0) {
        ESP_LOGW(TAG, "No ready members to start");
        return;
    }
    s_uc->set_partial = (bt_audio_le_uc_ready_count() < s_uc->expected_members);

    if (!bt_audio_le_uc_any_stream_live()) {
        bt_audio_le_uc_peers_reorder();
        bt_audio_le_uc_bindings_resolve();
    } else {
        bt_audio_le_uc_bindings_claim_reserved();
    }

    if (bt_audio_le_uc_bound_count() == 0 && bt_audio_le_uc_context_pending() &&
            !s_uc->context_wait_expired) {
        ESP_LOGI(TAG, "Waiting for Available and Supported Audio Contexts");
        bt_audio_le_uc_set_state(UC_STATE_SET_ASSEMBLING);
        bt_audio_le_uc_state_timer_start(BT_AUDIO_LE_UC_SET_WAIT_TIMEOUT_US);
        return;
    }

    if (bt_audio_le_uc_bound_count() == 0) {
        if (s_uc->cis_count > 0 && !s_uc->context_wait_expired &&
                bt_audio_le_uc_ready_count() < s_uc->expected_members) {
            ESP_LOGI(TAG, "No admitted sink yet; %u CIS held for a member still to join",
                     (unsigned)s_uc->cis_count);
            bt_audio_le_uc_set_state(UC_STATE_SET_ASSEMBLING);
            bt_audio_le_uc_state_timer_start(BT_AUDIO_LE_UC_SET_WAIT_TIMEOUT_US);
            return;
        }
        ESP_LOGW(TAG, "No acceptor passed unicast admission");
        s_uc->start_requested = false;
        bt_audio_le_uc_set_state(UC_STATE_IDLE);
        bt_audio_le_uc_release_lock();
        return;
    }

    if (bt_audio_le_uc_group_create() != ESP_OK) {
        bt_audio_le_uc_release_lock();
        bt_audio_le_uc_set_state(UC_STATE_IDLE);
        return;
    }
    bt_audio_le_uc_groups_rebuild_released();

    bt_audio_le_uc_set_state(UC_STATE_STARTING);
    bt_audio_le_uc_state_timer_start((uint64_t)BT_AUDIO_LE_UC_START_TIMEOUT_MS * 1000ULL);

    s_uc->start_member_fallback = false;
    s_uc->start_fallback_mask = 0;
    if (bt_audio_le_uc_audio_start(-1) != ESP_OK) {
        if (bt_audio_le_uc_any_stream_live()) {
            bt_audio_le_uc_state_timer_stop();
            bt_audio_le_uc_release_lock();
            bt_audio_le_uc_set_state(UC_STATE_STREAMING);
        } else if (!bt_audio_le_uc_try_other_member(-1)) {
            bt_audio_le_uc_start_give_up();
        }
    }
}

static void bt_audio_le_uc_on_lock_done_impl(int err)
{
    if (!s_uc) {
        return;
    }

    if (err) {
        ESP_LOGW(TAG, "Proceeding without set lock, err %d", err);
    }

    bt_audio_le_uc_peers_ready();
}

static void bt_audio_le_uc_on_lock_done(int err)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_on_lock_done_impl(err);
    bt_audio_le_uc_unlock();
}

static void bt_audio_le_uc_maybe_start(void)
{
    if (!s_uc || !s_uc->start_requested) {
        return;
    }
    if (s_uc->state == UC_STATE_STARTING || s_uc->state == UC_STATE_STOPPING ||
            s_uc->state == UC_STATE_LOCKING) {
        return;
    }
    if (s_uc->state == UC_STATE_STREAMING && !bt_audio_le_uc_has_unclaimed_member()) {
        return;
    }
    if (s_uc->discover_pending > 0 || s_uc->discovering_handle != BT_AUDIO_LE_UC_INVALID_HANDLE) {
        return;
    }
    if (bt_audio_le_csip_coordinator_op_in_progress()) {
        s_uc->csip_resume_start = true;
        return;
    }
    /* A connected member is still locked from an earlier procedure. Release it
     * once before locking again, including when only one member remains.
     * LOCKING and STARTING still hold the lock for the procedure in progress. */
    if (bt_audio_le_csip_coordinator_is_locked() && !s_uc->csip_release_gave_up &&
            s_uc->state != UC_STATE_LOCKING && s_uc->state != UC_STATE_STARTING) {
        s_uc->csip_release_gave_up = true;
        s_uc->csip_resume_start = true;
        bt_audio_le_uc_release_lock();
        if (bt_audio_le_csip_coordinator_op_in_progress()) {
            return;
        }
        s_uc->csip_resume_start = false;
    }
    if (!bt_audio_le_csip_coordinator_is_locked()) {
        s_uc->csip_release_gave_up = false;
    }

    size_t ready = bt_audio_le_uc_ready_count();
    if (ready == 0) {
        if (s_uc->state == UC_STATE_DISCOVERING || s_uc->state == UC_STATE_SET_ASSEMBLING) {
            ESP_LOGE(TAG, "Unicast start aborted: no usable Sink ASE after discovery");
            s_uc->start_requested = false;
            s_uc->discover_pending = 0;
            s_uc->discovering_handle = BT_AUDIO_LE_UC_INVALID_HANDLE;
            bt_audio_le_uc_state_timer_stop();
            bt_audio_le_uc_set_state(UC_STATE_IDLE);
        }
        return;
    }

    if (ready < s_uc->expected_members && !s_uc->set_partial) {
        bt_audio_le_uc_set_state(UC_STATE_SET_ASSEMBLING);
        bt_audio_le_uc_state_timer_start(BT_AUDIO_LE_UC_SET_WAIT_TIMEOUT_US);
        bt_audio_le_csip_coordinator_search_update();
        return;
    }

    s_uc->set_partial = (ready < s_uc->expected_members);
    bt_audio_le_csip_coordinator_search_update();

    /* Discover Available and Supported Audio Contexts before taking the CSIP
     * lock. A zero or missing value is not treated as available. */
    if (bt_audio_le_uc_context_pending() && !s_uc->context_wait_expired) {
        ESP_LOGI(TAG, "Waiting for Available and Supported Audio Contexts");
        bt_audio_le_uc_set_state(UC_STATE_SET_ASSEMBLING);
        bt_audio_le_uc_state_timer_start(BT_AUDIO_LE_UC_SET_WAIT_TIMEOUT_US);
        return;
    }

    if (s_uc->peer_count > 1 && ready >= 2 && bt_audio_le_uc_set_type() == ESP_BLE_AUDIO_CAP_SET_TYPE_CSIP &&
            !bt_audio_le_csip_coordinator_is_locked()) {
        const esp_ble_audio_csip_set_coordinator_set_member_t *members[BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS] = {0};
        const esp_ble_audio_csip_set_coordinator_set_info_t *set_info = NULL;
        uint8_t ranks[BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS] = {0};

        size_t count = bt_audio_le_uc_collect_csip_members(
            members, &set_info, ranks, BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS);
        if (count >= 2 && set_info && set_info->lockable) {
            bt_audio_le_uc_set_state(UC_STATE_LOCKING);
            esp_err_t ret = bt_audio_le_csip_coordinator_lock(members, set_info, ranks, count,
                                                     BT_AUDIO_LE_UC_LOCK_TIMEOUT_MS,
                                                     bt_audio_le_uc_on_lock_done);
            if (ret == ESP_OK) {
                return;
            }
            ESP_LOGW(TAG, "CSIP lock start failed (%s), continuing unlocked", esp_err_to_name(ret));
        }
    }

    bt_audio_le_uc_peers_ready();
}

static void bt_audio_le_uc_peer_caps_changed(uc_peer_t *peer)
{
    if (!peer || !s_uc->start_requested) {
        if (peer) {
            peer->admit_blocked = false;
        }
        return;
    }
    peer->admit_blocked = false;
    if (s_uc->discovering_handle != BT_AUDIO_LE_UC_INVALID_HANDLE) {
        return;
    }
    if (s_uc->state == UC_STATE_IDLE || s_uc->state == UC_STATE_SET_ASSEMBLING ||
            s_uc->state == UC_STATE_STREAMING) {
        bt_audio_le_uc_maybe_start();
    }
}

static void bt_audio_le_uc_pending_queue(uint16_t conn_handle)
{
    if (!s_uc || conn_handle == BT_AUDIO_LE_UC_INVALID_HANDLE) {
        return;
    }
    size_t slot = ARRAY_SIZE(s_uc->pending_handles);

    for (size_t i = 0; i < ARRAY_SIZE(s_uc->pending_handles); i++) {
        if (s_uc->pending_handles[i] == conn_handle) {
            return;
        }
        if (slot == ARRAY_SIZE(s_uc->pending_handles) &&
                s_uc->pending_handles[i] == BT_AUDIO_LE_UC_INVALID_HANDLE) {
            slot = i;
        }
    }
    if (slot < ARRAY_SIZE(s_uc->pending_handles)) {
        s_uc->pending_handles[slot] = conn_handle;
    }
}

static void bt_audio_le_uc_pending_clear(uint16_t conn_handle)
{
    if (!s_uc) {
        return;
    }
    for (size_t i = 0; i < ARRAY_SIZE(s_uc->pending_handles); i++) {
        if (s_uc->pending_handles[i] == conn_handle) {
            s_uc->pending_handles[i] = BT_AUDIO_LE_UC_INVALID_HANDLE;
        }
    }
}

static esp_err_t bt_audio_le_uc_start_discovery(uint16_t conn_handle)
{
    ESP_RETURN_ON_FALSE(s_uc, ESP_ERR_INVALID_STATE, TAG, "Not initialized");

    if (s_uc->discovering_handle != BT_AUDIO_LE_UC_INVALID_HANDLE) {
        bt_audio_le_uc_pending_queue(conn_handle);
        return ESP_OK;
    }

    uc_peer_t *peer = bt_audio_le_uc_peer_alloc(conn_handle);
    ESP_RETURN_ON_FALSE(peer, ESP_ERR_NO_MEM, TAG, "No peer slot for handle %u", conn_handle);

    if (bt_audio_le_uc_peer_is_ready(peer)) {
        bt_audio_le_uc_maybe_start();
        return ESP_OK;
    }

    s_uc->discovering_handle = conn_handle;
    if (s_uc->state == UC_STATE_IDLE || s_uc->state == UC_STATE_SET_ASSEMBLING) {
        bt_audio_le_uc_set_state(UC_STATE_DISCOVERING);
    }
    bt_audio_le_uc_state_timer_start(BT_AUDIO_LE_UC_SET_WAIT_TIMEOUT_US);
    s_uc->discover_pending++;

    esp_err_t ret = esp_ble_audio_cap_initiator_unicast_discover(conn_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "CAP discover failed handle %u: %s", conn_handle, esp_err_to_name(ret));
        s_uc->discovering_handle = BT_AUDIO_LE_UC_INVALID_HANDLE;
        if (s_uc->discover_pending > 0) {
            s_uc->discover_pending--;
        }
        bt_audio_le_uc_peer_reset(peer);
        if (s_uc->discover_pending == 0 && bt_audio_le_uc_ready_count() == 0 &&
            !bt_audio_le_uc_any_stream_live()) {
            s_uc->start_requested = false;
            bt_audio_le_uc_state_timer_stop();
            bt_audio_le_uc_set_state(UC_STATE_IDLE);
        }
        return ret;
    }
    return ESP_OK;
}

static void bt_audio_le_uc_drain_pending(void)
{
    if (!s_uc || s_uc->discovering_handle != BT_AUDIO_LE_UC_INVALID_HANDLE) {
        return;
    }
    for (size_t i = 0; i < ARRAY_SIZE(s_uc->pending_handles); i++) {
        uint16_t handle = s_uc->pending_handles[i];
        if (handle == BT_AUDIO_LE_UC_INVALID_HANDLE) {
            continue;
        }
        s_uc->pending_handles[i] = BT_AUDIO_LE_UC_INVALID_HANDLE;
        bt_audio_le_uc_start_discovery(handle);
        return;
    }
}

static void bt_audio_le_uc_peer_discovery_done(void)
{
    if (!s_uc) {
        return;
    }
    if (s_uc->discover_pending > 0) {
        s_uc->discover_pending--;
    }
    s_uc->discovering_handle = BT_AUDIO_LE_UC_INVALID_HANDLE;
    bt_audio_le_uc_drain_pending();

    if (s_uc->discover_pending > 0) {
        return;
    }
    bt_audio_le_uc_maybe_start();
}

static void bt_audio_le_uc_group_delete_if_empty(void)
{
    if (!s_uc || !s_uc->unicast_group) {
        return;
    }
    if (bt_audio_le_uc_used_count() > 0) {
        return;
    }
    bt_audio_le_uc_groups_delete();
}

static bool bt_audio_le_uc_all_streams_live(void)
{
    if (!s_uc) {
        return false;
    }
    for (size_t i = 0; i < s_uc->cis_count; i++) {
        if (!bt_audio_le_uc_binding_is_bound(&s_uc->bindings[i])) {
            continue;
        }
        if (!bt_audio_le_uc_stream_is_live(s_uc->sink_streams[i])) {
            return false;
        }
    }
    return true;
}

static bool bt_audio_le_uc_stream_ep_state(const bt_audio_le_stream_t *stream,
                                           esp_ble_audio_bap_ep_state_t *state)
{
    esp_ble_audio_bap_ep_info_t info = {0};

    if (!stream || !stream->bap_stream.conn || !stream->bap_stream.ep ||
            esp_ble_audio_bap_ep_get_info(stream->bap_stream.ep, &info) != ESP_OK) {
        return false;
    }
    *state = info.state;
    return true;
}

static void bt_audio_le_uc_stop_schedule(void)
{
    uint32_t delay_ms = s_uc->stop_timeout_reported ? 1000 : BT_AUDIO_LE_UC_STOP_POLL_MS;
    bt_audio_le_uc_state_timer_start((uint64_t)delay_ms * 1000ULL);
}

static void bt_audio_le_uc_stop_finish(void)
{
    bt_audio_le_uc_state_timer_stop();
    s_uc->stop_pending_mask = 0;
    s_uc->stop_release_requested_mask = 0;
    s_uc->cap_stop_active = false;
    s_uc->stop_timeout_reported = false;
    s_uc->start_requested = false;
    s_uc->user_stopping = false;
    bt_audio_le_uc_set_state(UC_STATE_IDLE);
    bt_audio_le_uc_group_delete_if_empty();
    bt_audio_le_uc_release_lock();
#if CONFIG_BT_VCP_VOL_CTLR && CONFIG_BT_CAP_COMMANDER
    bt_audio_le_vcp_ctlr_streams_stopped();
#endif
}

static void bt_audio_le_uc_stop_progress(void)
{
    if (!s_uc || s_uc->state != UC_STATE_STOPPING) {
        return;
    }

    int64_t elapsed_us = esp_timer_get_time() - s_uc->stop_started_us;
    if (!s_uc->stop_timeout_reported &&
            elapsed_us >= (int64_t)BT_AUDIO_LE_UC_STOP_TIMEOUT_MS * 1000) {
        s_uc->stop_timeout_reported = true;
        ESP_LOGE(TAG, "Stop timed out with pending ASE mask 0x%02x; forcing BAP Release",
                 s_uc->stop_pending_mask);
        if (s_uc->cap_stop_active) {
            esp_ble_audio_cap_initiator_unicast_audio_cancel();
            s_uc->cap_stop_active = false;
        }
    }

    for (size_t i = 0; i < s_uc->cis_count; i++) {
        uint8_t bit = BIT(i);
        if (!(s_uc->stop_pending_mask & bit)) {
            continue;
        }

        bt_audio_le_stream_t *stream = s_uc->sink_streams[i];
        esp_ble_audio_bap_ep_state_t state;
        if (!stream || !stream->bap_stream.conn || !stream->bap_stream.ep) {
            s_uc->stop_pending_mask &= ~bit;
            continue;
        }
        if (!bt_audio_le_uc_stream_ep_state(stream, &state)) {
            continue;
        }
        if (state == ESP_BLE_AUDIO_BAP_EP_STATE_IDLE) {
            s_uc->stop_pending_mask &= ~bit;
            continue;
        }
        if (state == ESP_BLE_AUDIO_BAP_EP_STATE_RELEASING) {
            s_uc->stop_release_requested_mask |= bit;
            continue;
        }
        if (s_uc->cap_stop_active || (s_uc->stop_release_requested_mask & bit) ||
                s_uc->stop_release_attempts[i] >= BT_AUDIO_LE_UC_RELEASE_MAX_RETRY) {
            continue;
        }

        s_uc->stop_release_attempts[i]++;
        s_uc->stop_release_requested_mask |= bit;
        esp_err_t err = esp_ble_audio_bap_stream_release(&stream->bap_stream);
        if (err == ESP_OK) {
            s_uc->group_needs_rebuild = true;
            ESP_LOGI(TAG, "[SNK #%zu] Release requested from ASE state 0x%02x",
                     i, (unsigned)state);
        } else {
            s_uc->stop_release_requested_mask &= ~bit;
            ESP_LOGW(TAG, "[SNK #%zu] Release attempt %u/%u failed from ASE state 0x%02x: %s",
                     i, s_uc->stop_release_attempts[i], BT_AUDIO_LE_UC_RELEASE_MAX_RETRY,
                     (unsigned)state, esp_err_to_name(err));
        }
    }

    if (s_uc->stop_pending_mask == 0) {
        bt_audio_le_uc_stop_finish();
        return;
    }

    bt_audio_le_uc_stop_schedule();
}

static void bt_audio_le_uc_stream_released(bt_audio_le_stream_t *stream, void *user_ctx)
{
    size_t index = (size_t)(uintptr_t)user_ctx;

    if (!bt_audio_le_uc_lock()) {
        return;
    }
    if (s_uc->state == UC_STATE_STOPPING && index < s_uc->cis_count &&
            s_uc->sink_streams[index] == stream) {
        s_uc->stop_pending_mask &= ~BIT(index);
        s_uc->group_needs_rebuild = true;
        bt_audio_le_uc_stop_schedule();
    }
    bt_audio_le_uc_unlock();
}

static esp_err_t bt_audio_le_uc_stop_begin(void)
{
    esp_ble_audio_cap_stream_t *streams[BT_AUDIO_LE_UC_MAX_CIS] = {0};
    size_t stream_count = 0;

    s_uc->stop_pending_mask = 0;
    s_uc->stop_release_requested_mask = 0;
    memset(s_uc->stop_release_attempts, 0, sizeof(s_uc->stop_release_attempts));
    s_uc->stop_started_us = esp_timer_get_time();
    s_uc->stop_timeout_reported = false;
    s_uc->cap_stop_active = false;

    for (size_t i = 0; i < s_uc->cis_count; i++) {
        bt_audio_le_stream_t *stream = s_uc->sink_streams[i];
        esp_ble_audio_bap_ep_state_t state;

        if (!bt_audio_le_uc_binding_is_bound(&s_uc->bindings[i]) ||
                !bt_audio_le_uc_stream_ep_state(stream, &state) ||
                state == ESP_BLE_AUDIO_BAP_EP_STATE_IDLE) {
            continue;
        }

        s_uc->stop_pending_mask |= BIT(i);
        if (state == ESP_BLE_AUDIO_BAP_EP_STATE_ENABLING ||
                state == ESP_BLE_AUDIO_BAP_EP_STATE_STREAMING) {
            streams[stream_count++] = &stream->cap_stream;
        } else if (state == ESP_BLE_AUDIO_BAP_EP_STATE_RELEASING) {
            s_uc->stop_release_requested_mask |= BIT(i);
        }
    }

    if (s_uc->stop_pending_mask == 0) {
        bt_audio_le_uc_stop_finish();
        return ESP_OK;
    }

    if (stream_count == 0) {
        bt_audio_le_uc_stop_progress();
        return ESP_OK;
    }

    esp_ble_audio_cap_unicast_audio_stop_param_t param = {
        .type = bt_audio_le_uc_set_type(),
        .count = stream_count,
        .streams = streams,
        .release = true,
    };
    s_uc->cap_stop_active = true;
    esp_err_t ret = esp_ble_audio_cap_initiator_unicast_audio_stop(&param);
    if (ret != ESP_OK) {
        s_uc->cap_stop_active = false;
        ESP_LOGW(TAG, "CAP stop failed (%s); releasing ASEs directly", esp_err_to_name(ret));
        bt_audio_le_uc_stop_progress();
        return ESP_OK;
    }

    bt_audio_le_uc_stop_schedule();
    return ESP_OK;
}

static void bt_audio_le_uc_release_failed_streams(void)
{
    if (!s_uc) {
        return;
    }
    for (size_t i = 0; i < s_uc->cis_count; i++) {
        bt_audio_le_stream_t *stream = s_uc->sink_streams[i];
        esp_ble_audio_bap_ep_state_t state;
        if (!stream || !bt_audio_le_uc_binding_is_bound(&s_uc->bindings[i]) ||
                !bt_audio_le_uc_stream_ep_state(stream, &state) ||
                state == ESP_BLE_AUDIO_BAP_EP_STATE_IDLE ||
                state == ESP_BLE_AUDIO_BAP_EP_STATE_RELEASING ||
                state == ESP_BLE_AUDIO_BAP_EP_STATE_STREAMING) {
            continue;
        }
        esp_err_t err = esp_ble_audio_bap_stream_release(&stream->bap_stream);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "[SNK #%zu] Release after failed CIS: %s", i, esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "[SNK #%zu] Released after failed CIS", i);
            s_uc->group_needs_rebuild = true;
        }
    }
}

static int bt_audio_le_uc_cis_by_conn(const void *conn)
{
    if (!s_uc || conn == NULL) {
        return -1;
    }
    for (size_t i = 0; i < s_uc->cis_count; i++) {
        const uc_cis_binding_t *b = &s_uc->bindings[i];
        if (!bt_audio_le_uc_binding_is_bound(b)) {
            continue;
        }
        if ((const void *)s_uc->peers[b->peer].conn == conn) {
            return (int)i;
        }
    }
    return -1;
}

static bool bt_audio_le_uc_try_other_member(int failed_cis)
{
    if (!s_uc) {
        return false;
    }
    if (failed_cis >= 0) {
        s_uc->start_fallback_mask |= BIT(failed_cis);
    }
    for (size_t n = 0; n < s_uc->cis_count; n++) {
        size_t i = (failed_cis < 0) ? (s_uc->cis_count - 1 - n)
                                    : ((size_t)failed_cis + 1U + n) % s_uc->cis_count;
        if (s_uc->start_fallback_mask & BIT(i)) {
            continue;
        }
        if (!bt_audio_le_uc_binding_is_bound(&s_uc->bindings[i]) ||
                bt_audio_le_uc_stream_is_live(s_uc->sink_streams[i])) {
            continue;
        }
        s_uc->start_member_fallback = true;
        s_uc->start_fallback_mask |= BIT(i);
        ESP_LOGI(TAG, "[SNK #%zu] Primary CIS procedure failed; trying this member", i);
        bt_audio_le_uc_set_state(UC_STATE_STARTING);
        bt_audio_le_uc_state_timer_start((uint64_t)BT_AUDIO_LE_UC_START_TIMEOUT_MS * 1000ULL);
        if (bt_audio_le_uc_audio_start((int)i) == ESP_OK) {
            return true;
        }
        bt_audio_le_uc_state_timer_stop();
    }
    return false;
}

static void bt_audio_le_uc_start_give_up(void)
{
    if (!s_uc) {
        return;
    }
    s_uc->start_requested = false;
    s_uc->start_member_fallback = false;
    bt_audio_le_uc_state_timer_stop();
    bt_audio_le_uc_set_state(UC_STATE_STOPPING);
    bt_audio_le_uc_stop_begin();
}

static void bt_audio_le_uc_schedule_secondary(uint32_t delay_ms)
{
    if (!s_uc || !s_uc->secondary_timer ||
            (bt_audio_le_uc_all_streams_live() && !bt_audio_le_uc_has_unclaimed_member())) {
        return;
    }
    esp_timer_stop(s_uc->secondary_timer);
    if (!s_uc->secondary_stage_logged) {
        const char *why = bt_audio_le_uc_has_unclaimed_member()
                              ? "a set member has not joined"
                              : "a targeted CIS is not streaming";
        ESP_LOGI(TAG, "Staged CIS start in %u ms: %s", (unsigned)delay_ms, why);
        s_uc->secondary_stage_logged = true;
    }
    esp_timer_start_once(s_uc->secondary_timer, (uint64_t)delay_ms * 1000ULL);
}

static void bt_audio_le_uc_secondary_timer_cb_impl(void *arg)
{
    if (!s_uc || !s_uc->start_requested) {
        return;
    }
    bt_audio_le_uc_bindings_claim_reserved();
    if (bt_audio_le_uc_all_streams_live()) {
        return;
    }
    if (s_uc->secondary_retry_count >= BT_AUDIO_LE_UC_SECONDARY_MAX_RETRY) {
        ESP_LOGW(TAG, "Secondary CIS did not start after %u attempts; primary continues",
                 (unsigned)BT_AUDIO_LE_UC_SECONDARY_MAX_RETRY);
        bt_audio_le_uc_set_state(UC_STATE_STREAMING);
        return;
    }

    s_uc->secondary_retry_count++;
    ESP_LOGI(TAG, "Starting secondary CIS (attempt %u/%u)",
             (unsigned)s_uc->secondary_retry_count,
             (unsigned)BT_AUDIO_LE_UC_SECONDARY_MAX_RETRY);
    bt_audio_le_uc_groups_rebuild_released();
    bt_audio_le_uc_set_state(UC_STATE_STARTING);
    bt_audio_le_uc_state_timer_start((uint64_t)BT_AUDIO_LE_UC_SECONDARY_TIMEOUT_MS * 1000ULL);
    if (bt_audio_le_uc_audio_start(-1) != ESP_OK) {
        bt_audio_le_uc_state_timer_stop();
        if (bt_audio_le_uc_any_stream_live()) {
            bt_audio_le_uc_set_state(UC_STATE_STREAMING);
            bt_audio_le_uc_schedule_secondary(BT_AUDIO_LE_UC_SECONDARY_RETRY_MS);
        } else if (!bt_audio_le_uc_try_other_member(-1)) {
            bt_audio_le_uc_start_give_up();
        }
    }
}

static void bt_audio_le_uc_secondary_timer_cb(void *arg)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_secondary_timer_cb_impl(arg);
    bt_audio_le_uc_unlock();
}

static void bt_audio_le_uc_on_start_complete_impl(int err, struct bt_conn *conn)
{
    if (!s_uc) {
        return;
    }
    if (s_uc->state == UC_STATE_STOPPING) {
        ESP_LOGI(TAG, "Ignoring start completion while stopping, err %d", err);
        return;
    }

    bt_audio_le_uc_state_timer_stop();
    bt_audio_le_uc_release_lock();

    if (err) {
        if (err == -ECANCELED) {
            ESP_LOGI(TAG, "CAP start cancelled");
        } else {
            ESP_LOGE(TAG, "CAP start failed, err %d", err);
        }
        if (bt_audio_le_uc_any_stream_live()) {
            bt_audio_le_uc_set_state(UC_STATE_STREAMING);
            if (err != -ECANCELED) {
                bt_audio_le_uc_release_failed_streams();
            }
            bt_audio_le_uc_schedule_secondary(BT_AUDIO_LE_UC_SECONDARY_RETRY_MS);
        } else if (err != -ECANCELED &&
                   bt_audio_le_uc_try_other_member(bt_audio_le_uc_cis_by_conn(conn))) {
            return;
        } else {
            bt_audio_le_uc_start_give_up();
        }
        return;
    }

    ESP_LOGI(TAG, "CAP start completed");
    bt_audio_le_uc_set_state(UC_STATE_STREAMING);
    if (s_uc->start_member_fallback && !bt_audio_le_uc_all_streams_live()) {
        bt_audio_le_uc_release_failed_streams();
    }
    s_uc->start_member_fallback = false;
    if (bt_audio_le_uc_all_streams_live()) {
        s_uc->secondary_retry_count = 0;
    }
    bt_audio_le_uc_schedule_secondary(BT_AUDIO_LE_UC_SECONDARY_DELAY_MS);
    bt_audio_le_csip_coordinator_search_update();
#if CONFIG_BT_VCP_VOL_CTLR && CONFIG_BT_CAP_COMMANDER
    bt_audio_le_vcp_ctlr_streams_started();
#endif
}

static void bt_audio_le_uc_on_stop_complete_impl(int err, struct bt_conn *conn)
{
    bt_audio_le_uc_state_timer_stop();
    if (!s_uc || s_uc->state != UC_STATE_STOPPING) {
        return;
    }

    if (err && err != -ECANCELED) {
        ESP_LOGW(TAG, "CAP stop completed with err %d", err);
    } else {
        ESP_LOGI(TAG, "CAP stop completed");
    }

    s_uc->cap_stop_active = false;
    bt_audio_le_uc_stop_progress();
}

static void bt_audio_le_uc_on_cap_discovery_impl(
    esp_ble_conn_t *conn, int err,
    const esp_ble_audio_csip_set_coordinator_set_member_t *member,
    const esp_ble_audio_csip_set_coordinator_csis_inst_t *csis_inst)
{
    if (!s_uc || !conn) {
        return;
    }
    if (s_uc->discovering_handle != conn->handle) {
        ESP_LOGW(TAG, "Ignoring late CAP discovery callback for handle %u", conn->handle);
        return;
    }

    uc_peer_t *peer = bt_audio_le_uc_peer_by_handle(conn->handle);
    if (!peer) {
        ESP_LOGW(TAG, "CAP discovery for unknown handle %u", conn->handle);
        bt_audio_le_uc_peer_discovery_done();
        return;
    }

    if (err) {
        ESP_LOGE(TAG, "CAP discovery failed on handle %u, err %d", conn->handle, err);
        bt_audio_le_uc_peer_reset(peer);
        bt_audio_le_uc_peer_discovery_done();
        return;
    }

    peer->member = member;
    peer->csis_inst = (esp_ble_audio_csip_set_coordinator_csis_inst_t *)csis_inst;
    peer->conn = conn;

    if (csis_inst) {
        const uint8_t *active_sirk = bt_audio_le_csip_coordinator_get_sirk();
        bool stale = !bt_audio_le_csip_coordinator_scan_connect_is_current(conn->handle);
        bool other_set = active_sirk &&
            memcmp(active_sirk, csis_inst->info.sirk, ESP_BLE_AUDIO_CSIP_SIRK_SIZE) != 0;

        if (stale || other_set) {
            if (stale) {
                ESP_LOGW(TAG, "Handle %u ignored; coordinated-set SIRK changed before discovery finished",
                         conn->handle);
            } else {
                ESP_LOGW(TAG, "Handle %u belongs to a different CSIP set; not joining this UMS group",
                         conn->handle);
            }
            bt_audio_le_csip_coordinator_forget_scan_connect(conn->handle);
            bt_audio_le_uc_peer_reset(peer);
            bt_audio_le_uc_peer_discovery_done();
            return;
        }
        s_uc->expected_members = bt_audio_le_uc_normalize_set_size(csis_inst->info.set_size);
        bt_audio_le_csip_coordinator_set_sirk(csis_inst->info.sirk);
        bt_audio_le_csip_coordinator_member_available(conn->handle, member, &csis_inst->info);
        ESP_LOGI(TAG, "CSIP member: handle %u rank %u set_size %u expected %u",
                 conn->handle, csis_inst->info.rank, csis_inst->info.set_size, s_uc->expected_members);
        bt_audio_le_csip_coordinator_search_update();
    } else {
        ESP_LOGI(TAG, "Handle %u has no CSIS; treated as a single-device group", conn->handle);
        if (bt_audio_le_uc_used_count() <= 1) {
            s_uc->expected_members = 1;
        }
    }

    esp_err_t ret = esp_ble_audio_bap_unicast_client_discover(conn->handle, ESP_BLE_AUDIO_DIR_SINK);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Sink ASE discovery failed: %s", esp_err_to_name(ret));
        bt_audio_le_uc_peer_reset(peer);
        bt_audio_le_uc_peer_discovery_done();
    }
}

static void bt_audio_le_uc_on_start_complete(int err, struct bt_conn *conn)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_on_start_complete_impl(err, conn);
    bt_audio_le_uc_unlock();
}

static void bt_audio_le_uc_on_stop_complete(int err, struct bt_conn *conn)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_on_stop_complete_impl(err, conn);
    bt_audio_le_uc_unlock();
}

static void bt_audio_le_uc_on_cap_discovery(
    esp_ble_conn_t *conn, int err,
    const esp_ble_audio_csip_set_coordinator_set_member_t *member,
    const esp_ble_audio_csip_set_coordinator_csis_inst_t *csis_inst)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_on_cap_discovery_impl(conn, err, member, csis_inst);
    bt_audio_le_uc_unlock();
}

static esp_ble_audio_cap_initiator_cb_t s_cap_cb = {
    .unicast_discovery_complete = bt_audio_le_uc_on_cap_discovery,
    .unicast_start_complete = bt_audio_le_uc_on_start_complete,
    .unicast_stop_complete = bt_audio_le_uc_on_stop_complete,
};

static void bt_audio_le_uc_endpoint_cb_impl(esp_ble_conn_t *conn, esp_ble_audio_dir_t dir,
                                            esp_ble_audio_bap_ep_t *ep)
{
    uc_peer_t *peer = conn ? bt_audio_le_uc_peer_by_handle(conn->handle) : NULL;
    if (!peer || !ep) {
        return;
    }
    peer->conn = conn;

    if (dir == ESP_BLE_AUDIO_DIR_SINK) {
        if (peer->sink_ep_count < ARRAY_SIZE(peer->sink_eps)) {
            ESP_LOGI(TAG, "[SNK #%u] Endpoint on handle %u", peer->sink_ep_count, conn->handle);
            peer->sink_eps[peer->sink_ep_count++] = ep;
        } else {
            ESP_LOGI(TAG, "[SNK] Spare endpoint on handle %u", conn->handle);
        }
    }
}

static void bt_audio_le_uc_location_cb_impl(esp_ble_conn_t *conn, esp_ble_audio_dir_t dir,
                                            esp_ble_audio_location_t location)
{
    uc_peer_t *peer = conn ? bt_audio_le_uc_peer_by_handle(conn->handle) : NULL;
    if (peer && dir == ESP_BLE_AUDIO_DIR_SINK) {
        peer->conn = conn;
        peer->sink_loc = location;
        ESP_LOGI(TAG, "[handle %u] Sink location 0x%08lx", conn->handle,
                 (unsigned long)location);
        bt_audio_le_uc_peer_caps_changed(peer);
    }
}

static void bt_audio_le_uc_pac_record_cb_impl(esp_ble_conn_t *conn, esp_ble_audio_dir_t dir,
                                              const esp_ble_audio_codec_cap_t *codec_cap)
{
    uc_peer_t *peer = conn ? bt_audio_le_uc_peer_by_handle(conn->handle) : NULL;
    if (!peer || !codec_cap || dir != ESP_BLE_AUDIO_DIR_SINK) {
        return;
    }

    if (codec_cap->id != ESP_BLE_ISO_CODING_FORMAT_LC3) {
        return;
    }

    esp_ble_audio_codec_cap_chan_count_t counts = ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_1;
    if (esp_ble_audio_codec_cap_get_supported_audio_chan_counts(codec_cap, &counts, true) != ESP_OK) {
        counts = ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_1;
    }
    uint8_t max_chan = (uint8_t)(32 - __builtin_clz((unsigned)counts | 1u));
    if (max_chan > peer->sink_max_chan) {
        peer->sink_max_chan = max_chan;
        ESP_LOGI(TAG, "[handle %u] Sink ASE takes up to %u channel(s)", conn->handle, max_chan);
    }

    esp_ble_audio_codec_cap_freq_t freq = 0;
    esp_ble_audio_codec_cap_frame_dur_t dur = 0;
    esp_ble_audio_codec_octets_per_codec_frame_t octets = {0};
    if (esp_ble_audio_codec_cap_get_freq(codec_cap, &freq) != ESP_OK ||
            esp_ble_audio_codec_cap_get_frame_dur(codec_cap, &dur) != ESP_OK ||
            esp_ble_audio_codec_cap_get_octets_per_frame(codec_cap, &octets) != ESP_OK) {
        return;
    }
    bool matched = false;
    for (uint8_t preset = 0; preset < BT_AUDIO_LE_UC_LC3_PRESET_COUNT; preset++) {
        const esp_ble_audio_codec_cfg_t *cfg = &s_lc3_presets[preset]->codec_cfg;
        esp_ble_audio_codec_cfg_freq_t cfg_freq;
        esp_ble_audio_codec_cfg_frame_dur_t cfg_dur;
        uint16_t frame_octets;
        uint32_t freq_bit;
        uint32_t dur_bit;

        if (esp_ble_audio_codec_cfg_get_freq(cfg, &cfg_freq) != ESP_OK ||
                esp_ble_audio_codec_cfg_get_frame_dur(cfg, &cfg_dur) != ESP_OK ||
                esp_ble_audio_codec_cfg_get_octets_per_frame(cfg, &frame_octets) != ESP_OK ||
                cfg_freq < ESP_BLE_AUDIO_CODEC_CFG_FREQ_8KHZ ||
                cfg_freq > ESP_BLE_AUDIO_CODEC_CFG_FREQ_48KHZ ||
                octets.min > frame_octets || octets.max < frame_octets) {
            continue;
        }
        freq_bit = 1u << (cfg_freq - ESP_BLE_AUDIO_CODEC_CFG_FREQ_8KHZ);
        if (cfg_dur == ESP_BLE_AUDIO_CODEC_CFG_DURATION_7_5) {
            dur_bit = ESP_BLE_AUDIO_CODEC_CAP_DURATION_7_5;
        } else if (cfg_dur == ESP_BLE_AUDIO_CODEC_CFG_DURATION_10) {
            dur_bit = ESP_BLE_AUDIO_CODEC_CAP_DURATION_10;
        } else {
            continue;
        }
        if ((freq & freq_bit) == 0 || (dur & dur_bit) == 0) {
            continue;
        }
        if (counts & ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_1) {
            peer->lc3_ch1 |= (uint32_t)(1u << preset);
        }
        if (counts & ESP_BLE_AUDIO_CODEC_CAP_CHAN_COUNT_2) {
            peer->lc3_ch2 |= (uint32_t)(1u << preset);
        }
        matched = true;
    }
    if (!matched) {
        return;
    }
    esp_ble_audio_context_t pref = 0;
    if (esp_ble_audio_codec_cap_meta_get_pref_context(codec_cap, &pref) == ESP_OK) {
        peer->pref_snk_ctx |= pref;
        peer->pref_known = true;
    }
    ESP_LOGI(TAG, "[handle %u] Sink PAC usable LC3 ch1 0x%08lx ch2 0x%08lx, octets %u-%u",
             conn->handle, (unsigned long)peer->lc3_ch1, (unsigned long)peer->lc3_ch2,
             octets.min, octets.max);
    bt_audio_le_uc_peer_caps_changed(peer);
}

static void bt_audio_le_uc_avail_ctx_cb_impl(esp_ble_conn_t *conn, esp_ble_audio_context_t snk_ctx,
                                             esp_ble_audio_context_t src_ctx)
{
    uc_peer_t *peer = conn ? bt_audio_le_uc_peer_by_handle(conn->handle) : NULL;
    if (!peer) {
        return;
    }
    peer->avail_snk_ctx = snk_ctx;
    peer->avail_ctx_known = true;
    ESP_LOGI(TAG, "[handle %u] Available sink contexts 0x%04x", conn->handle, (unsigned)snk_ctx);
    bt_audio_le_uc_peer_caps_changed(peer);
}

static void bt_audio_le_uc_supp_ctx_cb_impl(esp_ble_conn_t *conn, esp_ble_audio_context_t snk_ctx,
                                            esp_ble_audio_context_t src_ctx)
{
    uc_peer_t *peer = conn ? bt_audio_le_uc_peer_by_handle(conn->handle) : NULL;
    if (!peer) {
        return;
    }
    (void)src_ctx;
    peer->supp_snk_ctx = snk_ctx;
    peer->supp_ctx_known = true;
    ESP_LOGI(TAG, "[handle %u] Supported sink contexts 0x%04x", conn->handle, (unsigned)snk_ctx);
    bt_audio_le_uc_peer_caps_changed(peer);
}

static void bt_audio_le_uc_discover_cb_impl(esp_ble_conn_t *conn, int err, esp_ble_audio_dir_t dir)
{
    if (!conn || s_uc->discovering_handle != conn->handle) {
        if (conn) {
            ESP_LOGW(TAG, "Ignoring late ASE discovery callback for handle %u", conn->handle);
        }
        return;
    }
    uc_peer_t *peer = bt_audio_le_uc_peer_by_handle(conn->handle);
    if (!peer) {
        bt_audio_le_uc_peer_discovery_done();
        return;
    }

    if (dir == ESP_BLE_AUDIO_DIR_SINK) {
        if (err || peer->sink_ep_count == 0) {
            ESP_LOGE(TAG, "Sink ASE discovery failed handle %u err %d", conn->handle, err);
            bt_audio_le_uc_peer_reset(peer);
            bt_audio_le_uc_peer_discovery_done();
            return;
        }

        ESP_LOGI(TAG, "Sink discover complete: handle %u (%u ep)", conn->handle, peer->sink_ep_count);
        peer->ase_ready = true;
        ESP_LOGI(TAG, "ASE discovery complete on handle %u (%u/%u ready)",
                 conn->handle, (unsigned)bt_audio_le_uc_ready_count(), s_uc->expected_members);
#if CONFIG_BT_VCP_VOL_CTLR && CONFIG_BT_CAP_COMMANDER
        bt_audio_le_vcp_ctlr_discover(conn->handle);
#endif
        bt_audio_le_uc_on_members_complete();
        bt_audio_le_csip_coordinator_search_update();
        bt_audio_le_uc_peer_discovery_done();
    }
}

static void bt_audio_le_uc_endpoint_cb(esp_ble_conn_t *conn, esp_ble_audio_dir_t dir,
                                       esp_ble_audio_bap_ep_t *ep)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_endpoint_cb_impl(conn, dir, ep);
    bt_audio_le_uc_unlock();
}

static void bt_audio_le_uc_location_cb(esp_ble_conn_t *conn, esp_ble_audio_dir_t dir,
                                       esp_ble_audio_location_t location)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_location_cb_impl(conn, dir, location);
    bt_audio_le_uc_unlock();
}

static void bt_audio_le_uc_pac_record_cb(esp_ble_conn_t *conn, esp_ble_audio_dir_t dir,
                                         const esp_ble_audio_codec_cap_t *codec_cap)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_pac_record_cb_impl(conn, dir, codec_cap);
    bt_audio_le_uc_unlock();
}

static void bt_audio_le_uc_avail_ctx_cb(esp_ble_conn_t *conn, esp_ble_audio_context_t snk_ctx,
                                        esp_ble_audio_context_t src_ctx)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_avail_ctx_cb_impl(conn, snk_ctx, src_ctx);
    bt_audio_le_uc_unlock();
}

static void bt_audio_le_uc_supp_ctx_cb(esp_ble_conn_t *conn, esp_ble_audio_context_t snk_ctx,
                                       esp_ble_audio_context_t src_ctx)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_supp_ctx_cb_impl(conn, snk_ctx, src_ctx);
    bt_audio_le_uc_unlock();
}

static void bt_audio_le_uc_discover_cb(esp_ble_conn_t *conn, int err, esp_ble_audio_dir_t dir)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_discover_cb_impl(conn, err, dir);
    bt_audio_le_uc_unlock();
}

static esp_ble_audio_bap_unicast_client_cb_t s_bap_cb = {
    .location = bt_audio_le_uc_location_cb,
    .supported_contexts = bt_audio_le_uc_supp_ctx_cb,
    .available_contexts = bt_audio_le_uc_avail_ctx_cb,
    .pac_record = bt_audio_le_uc_pac_record_cb,
    .endpoint = bt_audio_le_uc_endpoint_cb,
    .discover = bt_audio_le_uc_discover_cb,
};

static void bt_audio_le_uc_state_timeout_impl(void *arg)
{
    if (!s_uc) {
        return;
    }

    switch (s_uc->state) {
    case UC_STATE_DISCOVERING:
        ESP_LOGE(TAG, "CAP/BAP discovery timed out");
        if (s_uc->discovering_handle != BT_AUDIO_LE_UC_INVALID_HANDLE) {
            uc_peer_t *peer = bt_audio_le_uc_peer_by_handle(s_uc->discovering_handle);
            if (peer) {
                bt_audio_le_uc_peer_reset(peer);
            }
        }
        s_uc->discovering_handle = BT_AUDIO_LE_UC_INVALID_HANDLE;
        if (s_uc->discover_pending > 0) {
            s_uc->discover_pending--;
        }
        bt_audio_le_uc_drain_pending();
        if (s_uc->discovering_handle != BT_AUDIO_LE_UC_INVALID_HANDLE) {
            break;
        }
        if (bt_audio_le_uc_ready_count() == 0 && !bt_audio_le_uc_any_stream_live()) {
            s_uc->start_requested = false;
            bt_audio_le_uc_set_state(UC_STATE_IDLE);
        } else {
            bt_audio_le_uc_maybe_start();
        }
        break;
    case UC_STATE_SET_ASSEMBLING:
        s_uc->context_wait_expired = true;
        if (bt_audio_le_uc_ready_count() >= s_uc->expected_members &&
                !bt_audio_le_uc_context_pending()) {
            return;
        }
        if (bt_audio_le_uc_ready_count() < s_uc->expected_members) {
            s_uc->set_partial = true;
            ESP_LOGW(TAG, "Set assembly timeout (SET_PARTIAL, %u/%u ready)",
                     (unsigned)bt_audio_le_uc_ready_count(), s_uc->expected_members);
        } else {
            ESP_LOGW(TAG, "Audio context discovery timed out");
        }
        bt_audio_le_uc_maybe_start();
        break;
    case UC_STATE_STARTING:
        ESP_LOGE(TAG, "CAP start timeout — cancelling");
        s_uc->start_requested = false;
        bt_audio_le_uc_set_state(UC_STATE_STOPPING);
        esp_ble_audio_cap_initiator_unicast_audio_cancel();
        bt_audio_le_uc_stop_begin();
        break;
    case UC_STATE_STOPPING:
        bt_audio_le_uc_stop_progress();
        break;
    default:
        break;
    }
}

static void bt_audio_le_uc_state_timeout(void *arg)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_uc_state_timeout_impl(arg);
    bt_audio_le_uc_unlock();
}

esp_err_t bt_audio_le_unicast_client_init(const esp_bt_audio_le_cfg_t *cfg)
{
    esp_err_t ret = ESP_OK;

    ESP_RETURN_ON_FALSE(cfg, ESP_ERR_INVALID_ARG, TAG, "LE configuration is NULL");
    ESP_RETURN_ON_FALSE(!s_uc, ESP_ERR_INVALID_STATE, TAG, "Already initialized");
    uint8_t max_members = cfg->max_unicast_members;
    if (max_members == 0) {
        max_members = BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS;
    }
    if (max_members > BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS) {
        ESP_LOGW(TAG, "Max members %u exceeds %u; clamping",
                 max_members, BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS);
        max_members = BT_AUDIO_LE_UC_DEFAULT_MAX_MEMBERS;
    }

    s_uc = heap_caps_calloc_prefer(1, sizeof(*s_uc), 2,
                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                   MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(s_uc, ESP_ERR_NO_MEM, TAG, "No memory for UC context");
    s_uc->mutex = xSemaphoreCreateRecursiveMutex();
    if (!s_uc->mutex) {
        ret = ESP_ERR_NO_MEM;
        goto fail;
    }

    s_uc->peer_count = max_members;
    s_uc->expected_members = 1;
    s_uc->lc3_preset = BT_AUDIO_LE_UC_LC3_PRESET_NONE;
    s_uc->group_preset = BT_AUDIO_LE_UC_LC3_PRESET_NONE;
    bt_audio_le_uc_bindings_clear();

    for (size_t i = 0; i < ARRAY_SIZE(s_uc->pending_handles); i++) {
        s_uc->pending_handles[i] = BT_AUDIO_LE_UC_INVALID_HANDLE;
    }
    s_uc->discovering_handle = BT_AUDIO_LE_UC_INVALID_HANDLE;

    ret = bt_audio_le_tx_group_create("uc_tx", &s_uc->tx_group);
    if (ret != ESP_OK) {
        goto fail;
    }
    ret = bt_audio_le_tx_group_set_task_cfg(s_uc->tx_group, cfg->src_send_task_core_id,
                                            cfg->src_send_task_prio, cfg->src_send_task_stack_size);
    if (ret != ESP_OK) {
        goto fail;
    }

    for (size_t i = 0; i < ARRAY_SIZE(s_uc->sink_streams); i++) {
        ret = bt_audio_le_stream_create(&s_uc->sink_streams[i]);
        if (ret != ESP_OK) {
            goto fail;
        }
        s_uc->sink_streams[i]->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_LE_UNICAST;
        s_uc->sink_streams[i]->base.direction = ESP_BT_AUDIO_STREAM_DIR_SOURCE;
        s_uc->sink_streams[i]->base.context = ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA;
        ret = bt_audio_le_stream_register_cap_ops(s_uc->sink_streams[i]);
        if (ret != ESP_OK) {
            goto fail;
        }
        bt_audio_le_stream_set_released_cb(s_uc->sink_streams[i],
                                           bt_audio_le_uc_stream_released,
                                           (void *)(uintptr_t)i);
        ret = bt_audio_le_tx_group_add(s_uc->tx_group, s_uc->sink_streams[i]);
        if (ret != ESP_OK) {
            goto fail;
        }
    }

    esp_timer_create_args_t timer_args = {
        .callback = bt_audio_le_uc_state_timeout,
        .name = "uc_state",
    };
    ret = esp_timer_create(&timer_args, &s_uc->state_timer);
    if (ret != ESP_OK) {
        goto fail;
    }

    esp_timer_create_args_t secondary_timer_args = {
        .callback = bt_audio_le_uc_secondary_timer_cb,
        .name = "uc_secondary",
    };
    ret = esp_timer_create(&secondary_timer_args, &s_uc->secondary_timer);
    if (ret != ESP_OK) {
        goto fail;
    }

    ret = esp_ble_audio_cap_initiator_register_cb(&s_cap_cb);
    if (ret != ESP_OK) {
        goto fail;
    }
    s_uc->cap_cb_registered = true;

    ret = esp_ble_audio_bap_unicast_client_register_cb(&s_bap_cb);
    if (ret != ESP_OK) {
        goto fail;
    }
    s_uc->bap_cb_registered = true;

    ret = bt_audio_le_csip_coordinator_init();
    if (ret != ESP_OK) {
        goto fail;
    }

    bt_audio_le_uc_set_state(UC_STATE_IDLE);
    ESP_LOGI(TAG, "Unicast client init: up to %u member(s), %u CIS; layout comes from discovery",
             s_uc->peer_count, (unsigned)BT_AUDIO_LE_UC_MAX_CIS);
    return ESP_OK;

fail:
    bt_audio_le_unicast_client_deinit();
    return ret;
}

void bt_audio_le_unicast_client_deinit(void)
{
    if (!s_uc) {
        return;
    }

    if (s_uc->mutex) {
        xSemaphoreTakeRecursive(s_uc->mutex, portMAX_DELAY);
        s_uc->destroying = true;
        xSemaphoreGiveRecursive(s_uc->mutex);
    }

    if (s_uc->state_timer) {
        esp_timer_stop_blocking(s_uc->state_timer, portMAX_DELAY);
    }
    if (s_uc->secondary_timer) {
        esp_timer_stop_blocking(s_uc->secondary_timer, portMAX_DELAY);
    }
    if (s_uc->state == UC_STATE_STARTING || s_uc->state == UC_STATE_STOPPING) {
        esp_ble_audio_cap_initiator_unicast_audio_cancel();
    }
    if (!bt_audio_le_uc_groups_delete()) {
        ESP_LOGE(TAG, "Aborting unicast client deinit: CAP group still referenced; "
                 "local streams are kept to avoid dangling stack pointers");
        if (s_uc->mutex) {
            xSemaphoreTakeRecursive(s_uc->mutex, portMAX_DELAY);
            s_uc->destroying = false;
            xSemaphoreGiveRecursive(s_uc->mutex);
        }
        return;
    }
    bt_audio_le_csip_coordinator_deinit();

    if (s_uc->bap_cb_registered) {
        esp_ble_audio_bap_unicast_client_unregister_cb(&s_bap_cb);
    }
    if (s_uc->cap_cb_registered) {
        esp_ble_audio_cap_initiator_unregister_cb(&s_cap_cb);
    }
    if (s_uc->state_timer) {
        esp_timer_delete(s_uc->state_timer);
    }
    if (s_uc->secondary_timer) {
        esp_timer_delete(s_uc->secondary_timer);
    }
    bt_audio_le_tx_group_destroy(s_uc->tx_group);
    s_uc->tx_group = NULL;
    for (size_t i = 0; i < ARRAY_SIZE(s_uc->sink_streams); i++) {
        bt_audio_le_stream_destroy(s_uc->sink_streams[i]);
        s_uc->sink_streams[i] = NULL;
    }
    if (s_uc->mutex) {
        vSemaphoreDelete(s_uc->mutex);
        s_uc->mutex = NULL;
    }
    heap_caps_free(s_uc);
    s_uc = NULL;
    ESP_LOGI(TAG, "Deinitialized");
}

static esp_err_t bt_audio_le_unicast_client_start_impl(uint16_t conn_handle)
{
    ESP_RETURN_ON_FALSE(s_uc, ESP_ERR_INVALID_STATE, TAG, "Not initialized");
    ESP_RETURN_ON_FALSE(s_uc->state == UC_STATE_IDLE ||
                        s_uc->state == UC_STATE_SET_ASSEMBLING ||
                        s_uc->state == UC_STATE_DISCOVERING ||
                        s_uc->state == UC_STATE_STREAMING,
                        ESP_ERR_INVALID_STATE, TAG, "Busy: state=%s",
                        uc_state_str(s_uc->state));

    s_uc->start_requested = true;
    s_uc->user_stopping = false;
    s_uc->set_partial = false;
    s_uc->context_wait_expired = false;
    for (size_t i = 0; i < s_uc->peer_count; i++) {
        s_uc->peers[i].admit_blocked = false;
    }
    s_uc->csip_release_gave_up = false;
    s_uc->secondary_retry_count = 0;
    s_uc->start_member_fallback = false;
    s_uc->start_fallback_mask = 0;
    s_uc->secondary_stage_logged = false;
    if (s_uc->secondary_timer) {
        esp_timer_stop(s_uc->secondary_timer);
    }

    ESP_RETURN_ON_ERROR(bt_audio_le_uc_start_discovery(conn_handle), TAG,
                        "Failed to start discovery");
    bt_audio_le_uc_maybe_start();
    return ESP_OK;
}

static esp_err_t bt_audio_le_unicast_client_stop_impl(void)
{
    ESP_RETURN_ON_FALSE(s_uc, ESP_ERR_INVALID_STATE, TAG, "Not initialized");

    bool cancel_start = s_uc->state == UC_STATE_STARTING;
    s_uc->user_stopping = true;
    s_uc->start_requested = false;
    if (s_uc->secondary_timer) {
        esp_timer_stop(s_uc->secondary_timer);
    }
    bt_audio_le_uc_state_timer_stop();

    if (s_uc->state == UC_STATE_STOPPING) {
        return ESP_OK;
    }

    bt_audio_le_uc_set_state(UC_STATE_STOPPING);
    if (cancel_start) {
        esp_ble_audio_cap_initiator_unicast_audio_cancel();
    }
    return bt_audio_le_uc_stop_begin();
}

static void bt_audio_le_unicast_client_on_disconnect_impl(uint16_t conn_handle)
{
    if (!s_uc) {
        return;
    }

    uc_peer_t *peer = bt_audio_le_uc_peer_by_handle(conn_handle);
    if (!peer) {
        return;
    }

    ESP_LOGI(TAG, "Peer disconnect handle %u (state=%s)", conn_handle, uc_state_str(s_uc->state));
    const esp_ble_audio_csip_set_coordinator_set_member_t *lost_member = peer->member;
    bt_audio_le_uc_pending_clear(conn_handle);
#if CONFIG_BT_VCP_VOL_CTLR && CONFIG_BT_CAP_COMMANDER
    bt_audio_le_vcp_ctlr_forget(conn_handle);
#endif
    if (s_uc->discovering_handle == conn_handle) {
        s_uc->discovering_handle = BT_AUDIO_LE_UC_INVALID_HANDLE;
        if (s_uc->discover_pending > 0) {
            s_uc->discover_pending--;
        }
    }
    bt_audio_le_uc_bindings_unbind_peer((uint8_t)(peer - s_uc->peers));
    bt_audio_le_uc_peer_reset(peer);
    bt_audio_le_csip_coordinator_member_disconnected(lost_member);
    bt_audio_le_csip_coordinator_search_update();

    if (bt_audio_le_uc_used_count() == 0) {
        if (s_uc->state == UC_STATE_STREAMING || s_uc->state == UC_STATE_STARTING) {
            s_uc->user_stopping = true;
            s_uc->start_requested = false;
            if (s_uc->state == UC_STATE_STARTING) {
                esp_ble_audio_cap_initiator_unicast_audio_cancel();
            }
            bt_audio_le_uc_set_state(UC_STATE_IDLE);
        }
        bt_audio_le_uc_group_delete_if_empty();
        bt_audio_le_uc_release_lock();
        s_uc->start_requested = false;
    }

    bt_audio_le_uc_drain_pending();
}

static void bt_audio_le_unicast_client_on_set_size_changed_impl(uint16_t conn_handle,
                                                                uint8_t set_size)
{
    uc_peer_t *peer = bt_audio_le_uc_peer_by_handle(conn_handle);
    if (!peer || !peer->csis_inst) {
        return;
    }
    s_uc->expected_members = bt_audio_le_uc_normalize_set_size(set_size);
    bt_audio_le_uc_on_members_complete();
    ESP_LOGI(TAG, "CSIS set size updated on handle %u: expected %u",
             conn_handle, s_uc->expected_members);
    bt_audio_le_csip_coordinator_search_update();
    bt_audio_le_uc_maybe_start();
}

esp_err_t bt_audio_le_unicast_client_start(uint16_t conn_handle)
{
    if (!bt_audio_le_uc_lock()) {
        ESP_LOGE(TAG, "Unicast start failed: client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = bt_audio_le_unicast_client_start_impl(conn_handle);
    bt_audio_le_uc_unlock();
    return ret;
}

esp_err_t bt_audio_le_unicast_client_add_member(uint16_t conn_handle)
{
    if (!bt_audio_le_uc_lock()) {
        ESP_LOGE(TAG, "Add member failed: client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = bt_audio_le_uc_start_discovery(conn_handle);
    bt_audio_le_uc_unlock();
    return ret;
}

bool bt_audio_le_unicast_client_match_rsi(uint8_t data_type, const uint8_t *data, uint8_t data_len)
{
    if (!bt_audio_le_uc_lock()) {
        return false;
    }
    bool matched = bt_audio_le_csip_coordinator_match_rsi(data_type, data, data_len);
    bt_audio_le_uc_unlock();
    return matched;
}

bool bt_audio_le_unicast_client_needs_member(void)
{
    if (!bt_audio_le_uc_lock()) {
        return false;
    }
    bool needed = bt_audio_le_csip_coordinator_needs_member(
        bt_audio_le_uc_ready_count(), s_uc->expected_members);
    bt_audio_le_uc_unlock();
    return needed;
}

esp_err_t bt_audio_le_unicast_client_ready_conn(uint16_t *conn_handle)
{
    if (!conn_handle) {
        ESP_LOGE(TAG, "Ready connection query failed: invalid argument");
        return ESP_ERR_INVALID_ARG;
    }
    if (!bt_audio_le_uc_lock()) {
        ESP_LOGE(TAG, "Ready connection query failed: client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    uc_peer_t *peer = bt_audio_le_uc_lowest_rank_ready();
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (peer) {
        *conn_handle = peer->conn_handle;
        ret = ESP_OK;
    }
    bt_audio_le_uc_unlock();
    return ret;
}

esp_err_t bt_audio_le_unicast_client_stop(void)
{
    if (!bt_audio_le_uc_lock()) {
        ESP_LOGE(TAG, "Unicast stop failed: client not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = bt_audio_le_unicast_client_stop_impl();
    bt_audio_le_uc_unlock();
    return ret;
}

void bt_audio_le_unicast_client_on_disconnect(uint16_t conn_handle)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_unicast_client_on_disconnect_impl(conn_handle);
    bt_audio_le_uc_unlock();
}

void bt_audio_le_unicast_client_on_set_size_changed(uint16_t conn_handle, uint8_t set_size)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    bt_audio_le_unicast_client_on_set_size_changed_impl(conn_handle, set_size);
    bt_audio_le_uc_unlock();
}

void bt_audio_le_unicast_client_on_csip_release_done(void)
{
    if (!bt_audio_le_uc_lock()) {
        return;
    }
    if (s_uc->csip_resume_start) {
        s_uc->csip_resume_start = false;
        bt_audio_le_uc_maybe_start();
    }
    bt_audio_le_uc_unlock();
}

esp_err_t bt_audio_le_unicast_client_copy_cap_members(esp_ble_audio_cap_set_member_t *members,
                                                      size_t max_count, size_t *count,
                                                      esp_ble_audio_cap_set_type_t *type)
{
    if (!members || !count || !type || max_count == 0) {
        ESP_LOGE(TAG, "Copy CAP members failed: invalid argument");
        return ESP_ERR_INVALID_ARG;
    }
    if (!bt_audio_le_uc_lock()) {
        ESP_LOGE(TAG, "Copy CAP members failed: client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    size_t ready = bt_audio_le_uc_ready_count();
    if (ready == 0 || ready > max_count) {
        bt_audio_le_uc_unlock();
        ESP_LOGE(TAG, "Copy CAP members failed: ready count %u max %u",
                 (unsigned)ready, (unsigned)max_count);
        return ESP_ERR_INVALID_STATE;
    }

    *type = bt_audio_le_uc_set_type();
    size_t written = 0;
    for (size_t i = 0; i < s_uc->peer_count && written < ready; i++) {
        if (!s_uc->peers[i].used || !s_uc->peers[i].ase_ready) {
            continue;
        }
        if (!bt_audio_le_uc_fill_set_member(&members[written], *type, s_uc->peers[i].conn_handle)) {
            uint16_t handle = s_uc->peers[i].conn_handle;
            bt_audio_le_uc_unlock();
            ESP_LOGE(TAG, "Copy CAP members failed: handle %u is not ready", handle);
            return ESP_ERR_NOT_FOUND;
        }
        written++;
    }
    *count = written;
    bt_audio_le_uc_unlock();
    return ESP_OK;
}
