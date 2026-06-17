/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_le.h"
#include "esp_bt_audio_tel.h"
#include "esp_bt_audio_pb.h"
#include "bt_audio_host_ops.h"
#include "bt_audio_ops.h"

#define BT_AUDIO_RETURN_ON_NO_OP(ops, op) do { \
    if (!bt_ops || !bt_ops->ops.op) {          \
        ESP_LOGE(TAG, "Operation failed: " #ops "." #op " is not registered"); \
        return ESP_ERR_INVALID_STATE;          \
    }                                          \
} while (0)

#define BT_AUDIO_HOST_RETURN_ON_NO_OP(op) BT_AUDIO_RETURN_ON_NO_OP(host_ops, op)

static const char *TAG = "BT_AUD_OPS";

typedef struct {
    bt_audio_host_ops_t          host_ops;
    esp_bt_audio_playback_ops_t  playback_ops;
    esp_bt_audio_media_ops_t     media_ops;
    esp_bt_audio_vol_ops_t       vol_ops;
    esp_bt_audio_classic_ops_t   classic_ops;
    esp_bt_audio_le_ops_t        le_ops;
    esp_bt_audio_call_ops_t      call_ops;
    esp_bt_audio_pb_ops_t        pb_ops;
} gmf_bt_ops_t;

static gmf_bt_ops_t *bt_ops;

esp_err_t bt_audio_ops_init(void)
{
    if (bt_ops) {
        ESP_LOGE(TAG, "Init failed: operations already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    bt_ops = heap_caps_calloc_prefer(1, sizeof(gmf_bt_ops_t), 2,
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT,
                                     MALLOC_CAP_DEFAULT);
    if (!bt_ops) {
        ESP_LOGE(TAG, "Init failed: no memory for operations");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void bt_audio_ops_deinit(void)
{
    if (bt_ops) {
        free(bt_ops);
        bt_ops = NULL;
    }
}

esp_err_t bt_audio_ops_set_host(const bt_audio_host_ops_t *host_ops)
{
    if (!bt_ops) {
        ESP_LOGE(TAG, "Set host ops failed: operations are not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (host_ops) {
        memcpy(&bt_ops->host_ops, host_ops, sizeof(bt_audio_host_ops_t));
    } else {
        bt_ops->host_ops = (bt_audio_host_ops_t) {0};
    }
    return ESP_OK;
}

esp_err_t bt_audio_ops_get_playback(esp_bt_audio_playback_ops_t *playback_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!playback_ops) {
        return ESP_ERR_INVALID_ARG;
    }
    *playback_ops = bt_ops->playback_ops;
    return ESP_OK;
}

esp_err_t bt_audio_ops_set_playback(esp_bt_audio_playback_ops_t *playback_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (playback_ops) {
        memcpy(&bt_ops->playback_ops, playback_ops, sizeof(esp_bt_audio_playback_ops_t));
    } else {
        bt_ops->playback_ops = (esp_bt_audio_playback_ops_t) {0};
    }
    return ESP_OK;
}

esp_err_t bt_audio_ops_get_media(esp_bt_audio_media_ops_t *media_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!media_ops) {
        return ESP_ERR_INVALID_ARG;
    }
    *media_ops = bt_ops->media_ops;
    return ESP_OK;
}

esp_err_t bt_audio_ops_set_media(esp_bt_audio_media_ops_t *media_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (media_ops) {
        memcpy(&bt_ops->media_ops, media_ops, sizeof(esp_bt_audio_media_ops_t));
    } else {
        bt_ops->media_ops = (esp_bt_audio_media_ops_t) {0};
    }
    return ESP_OK;
}

esp_err_t bt_audio_ops_get_vol(esp_bt_audio_vol_ops_t *vol_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!vol_ops) {
        return ESP_ERR_INVALID_ARG;
    }
    *vol_ops = bt_ops->vol_ops;
    return ESP_OK;
}

esp_err_t bt_audio_ops_set_vol(esp_bt_audio_vol_ops_t *vol_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (vol_ops) {
        memcpy(&bt_ops->vol_ops, vol_ops, sizeof(esp_bt_audio_vol_ops_t));
    } else {
        bt_ops->vol_ops = (esp_bt_audio_vol_ops_t) {0};
    }
    return ESP_OK;
}

esp_err_t bt_audio_ops_get_classic(esp_bt_audio_classic_ops_t *classic_discovery_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!classic_discovery_ops) {
        return ESP_ERR_INVALID_ARG;
    }
    *classic_discovery_ops = bt_ops->classic_ops;
    return ESP_OK;
}

esp_err_t bt_audio_ops_set_classic(esp_bt_audio_classic_ops_t *classic_discovery_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (classic_discovery_ops) {
        memcpy(&bt_ops->classic_ops, classic_discovery_ops, sizeof(esp_bt_audio_classic_ops_t));
    } else {
        bt_ops->classic_ops = (esp_bt_audio_classic_ops_t) {0};
    }
    return ESP_OK;
}

esp_err_t bt_audio_ops_get_le(esp_bt_audio_le_ops_t *le_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!le_ops) {
        return ESP_ERR_INVALID_ARG;
    }
    *le_ops = bt_ops->le_ops;
    return ESP_OK;
}

esp_err_t bt_audio_ops_set_le(esp_bt_audio_le_ops_t *le_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (le_ops) {
        memcpy(&bt_ops->le_ops, le_ops, sizeof(esp_bt_audio_le_ops_t));
    } else {
        bt_ops->le_ops = (esp_bt_audio_le_ops_t) {0};
    }
    return ESP_OK;
}

esp_err_t bt_audio_ops_get_call(esp_bt_audio_call_ops_t *call_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!call_ops) {
        return ESP_ERR_INVALID_ARG;
    }
    *call_ops = bt_ops->call_ops;
    return ESP_OK;
}

esp_err_t bt_audio_ops_set_call(esp_bt_audio_call_ops_t *call_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (call_ops) {
        memcpy(&bt_ops->call_ops, call_ops, sizeof(esp_bt_audio_call_ops_t));
    } else {
        bt_ops->call_ops = (esp_bt_audio_call_ops_t) {0};
    }
    return ESP_OK;
}

esp_err_t bt_audio_ops_get_pb(esp_bt_audio_pb_ops_t *pb_ops)
{
    if (!pb_ops) {
        return ESP_ERR_INVALID_ARG;
    }
    *pb_ops = bt_ops->pb_ops;
    return ESP_OK;
}

esp_err_t bt_audio_ops_set_pb(esp_bt_audio_pb_ops_t *pb_ops)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pb_ops) {
        memcpy(&bt_ops->pb_ops, pb_ops, sizeof(esp_bt_audio_pb_ops_t));
    } else {
        bt_ops->pb_ops = (esp_bt_audio_pb_ops_t) {0};
    }
    return ESP_OK;
}

esp_err_t bt_audio_host_ext_adv_configure(uint8_t handle, const bt_audio_ext_adv_params_t *params)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(ext_adv_configure);
    return bt_ops->host_ops.ext_adv_configure(handle, params);
}

esp_err_t bt_audio_host_ext_adv_set_data(uint8_t handle, const uint8_t *data, size_t len)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(ext_adv_set_data);
    return bt_ops->host_ops.ext_adv_set_data(handle, data, len);
}

esp_err_t bt_audio_host_ext_adv_start(uint8_t handle, int duration, int max_events)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(ext_adv_start);
    return bt_ops->host_ops.ext_adv_start(handle, duration, max_events);
}

esp_err_t bt_audio_host_ext_adv_stop(uint8_t handle)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(ext_adv_stop);
    return bt_ops->host_ops.ext_adv_stop(handle);
}

esp_err_t bt_audio_host_disc(uint8_t own_addr_type, const bt_audio_scan_params_t *params, uint32_t timeout_ms)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(disc);
    return bt_ops->host_ops.disc(own_addr_type, params, timeout_ms);
}

esp_err_t bt_audio_host_disc_cancel(void)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(disc_cancel);
    return bt_ops->host_ops.disc_cancel();
}

esp_err_t bt_audio_host_connect(uint8_t own_addr_type, const bt_audio_addr_t *peer,
                                const bt_audio_conn_params_t *params, uint32_t timeout_ms)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(connect);
    return bt_ops->host_ops.connect(own_addr_type, peer, params, timeout_ms);
}

esp_err_t bt_audio_host_disconnect(uint16_t conn_handle, uint8_t reason)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(disconnect);
    return bt_ops->host_ops.disconnect(conn_handle, reason);
}

esp_err_t bt_audio_host_conn_find(uint16_t conn_handle, bt_audio_conn_desc_t *desc)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(conn_find);
    if (!desc) {
        return ESP_ERR_INVALID_ARG;
    }
    return bt_ops->host_ops.conn_find(conn_handle, desc);
}

esp_err_t bt_audio_host_acl_connected(uint16_t conn_handle, const bt_audio_addr_t *peer)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(acl_connected);
    return bt_ops->host_ops.acl_connected(conn_handle, peer);
}

esp_err_t bt_audio_host_acl_disconnected(uint16_t conn_handle)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(acl_disconnected);
    return bt_ops->host_ops.acl_disconnected(conn_handle);
}

esp_err_t bt_audio_host_security_initiate(uint16_t conn_handle)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(security_initiate);
    return bt_ops->host_ops.security_initiate(conn_handle);
}

esp_err_t bt_audio_host_periodic_adv_configure(uint8_t adv_handle, const bt_audio_periodic_adv_params_t *params)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(periodic_adv_configure);
    return bt_ops->host_ops.periodic_adv_configure(adv_handle, params);
}

esp_err_t bt_audio_host_periodic_adv_set_data(uint8_t adv_handle, const uint8_t *data, size_t len)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(periodic_adv_set_data);
    return bt_ops->host_ops.periodic_adv_set_data(adv_handle, data, len);
}

esp_err_t bt_audio_host_periodic_adv_start(uint8_t adv_handle)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(periodic_adv_start);
    return bt_ops->host_ops.periodic_adv_start(adv_handle);
}

esp_err_t bt_audio_host_periodic_adv_stop(uint8_t adv_handle)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(periodic_adv_stop);
    return bt_ops->host_ops.periodic_adv_stop(adv_handle);
}

esp_err_t bt_audio_host_pa_sync_create(const bt_audio_addr_t *addr, uint8_t sid,
                                       const bt_audio_periodic_sync_params_t *params)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(pa_sync_create);
    return bt_ops->host_ops.pa_sync_create(addr, sid, params);
}

esp_err_t bt_audio_host_pa_sync_terminate(uint16_t sync_handle)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(pa_sync_terminate);
    return bt_ops->host_ops.pa_sync_terminate(sync_handle);
}

esp_err_t bt_audio_host_pa_sync_create_cancel(void)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(pa_sync_create_cancel);
    return bt_ops->host_ops.pa_sync_create_cancel();
}

esp_err_t bt_audio_host_pa_sync_receive(uint16_t conn_handle, const bt_audio_periodic_sync_params_t *params)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(pa_sync_receive);
    return bt_ops->host_ops.pa_sync_receive(conn_handle, params);
}

esp_err_t bt_audio_host_id_infer_auto(int privacy, uint8_t *out_addr_type)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(id_infer_auto);
    return bt_ops->host_ops.id_infer_auto(privacy, out_addr_type);
}

const char *bt_audio_host_svc_gap_device_name(void)
{
    if (!bt_ops || !bt_ops->host_ops.svc_gap_device_name) {
        return NULL;
    }
    return bt_ops->host_ops.svc_gap_device_name();
}

uint16_t bt_audio_host_iso_free_buf_num_get(uint16_t conn_handle)
{
    if (!bt_ops || !bt_ops->host_ops.iso_free_buf_num_get) {
        return 0;
    }
    return bt_ops->host_ops.iso_free_buf_num_get(conn_handle);
}

esp_err_t bt_audio_host_hci_iso_tx(uint16_t conn_handle, const uint8_t *sdu, uint16_t sdu_len,
                                   bool ts_flag, uint32_t time_stamp, uint16_t pkt_seq_num)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(hci_iso_tx);
    return bt_ops->host_ops.hci_iso_tx(conn_handle, sdu, sdu_len, ts_flag, time_stamp, pkt_seq_num);
}

esp_err_t bt_audio_host_register_event_cb(void)
{
    BT_AUDIO_HOST_RETURN_ON_NO_OP(register_event_cb);
    return bt_ops->host_ops.register_event_cb();
}

void bt_audio_host_post_gap_event(uint8_t gap_event_type, void *event)
{
    if (!bt_ops || !bt_ops->host_ops.post_gap_event) {
        return;
    }
    bt_ops->host_ops.post_gap_event(gap_event_type, event);
}

esp_err_t esp_bt_audio_playback_play(void)
{
    BT_AUDIO_RETURN_ON_NO_OP(playback_ops, play);
    return bt_ops->playback_ops.play();
}

esp_err_t esp_bt_audio_playback_pause(void)
{
    BT_AUDIO_RETURN_ON_NO_OP(playback_ops, pause);
    return bt_ops->playback_ops.pause();
}

esp_err_t esp_bt_audio_playback_stop(void)
{
    BT_AUDIO_RETURN_ON_NO_OP(playback_ops, stop);
    return bt_ops->playback_ops.stop();
}

esp_err_t esp_bt_audio_playback_next(void)
{
    BT_AUDIO_RETURN_ON_NO_OP(playback_ops, next);
    return bt_ops->playback_ops.next();
}

esp_err_t esp_bt_audio_playback_prev(void)
{
    BT_AUDIO_RETURN_ON_NO_OP(playback_ops, prev);
    return bt_ops->playback_ops.prev();
}

esp_err_t esp_bt_audio_playback_request_metadata(uint32_t mask)
{
    BT_AUDIO_RETURN_ON_NO_OP(playback_ops, request_metadata);
    return bt_ops->playback_ops.request_metadata(mask);
}

esp_err_t esp_bt_audio_playback_reg_notifications(uint32_t mask)
{
    BT_AUDIO_RETURN_ON_NO_OP(playback_ops, reg_notifications);
    return bt_ops->playback_ops.reg_notifications(mask);
}

esp_err_t esp_bt_audio_media_start(uint32_t role, void *config)
{
    if (role != ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC) {
        return ESP_ERR_INVALID_STATE;
    }
    BT_AUDIO_RETURN_ON_NO_OP(media_ops, a2d_media_start);
    return bt_ops->media_ops.a2d_media_start(config);
}

esp_err_t esp_bt_audio_media_stop(uint32_t role)
{
    if (role != ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC) {
        return ESP_ERR_INVALID_STATE;
    }
    BT_AUDIO_RETURN_ON_NO_OP(media_ops, a2d_media_stop);
    return bt_ops->media_ops.a2d_media_stop();
}

esp_err_t esp_bt_audio_vol_notify(uint32_t vol)
{
    BT_AUDIO_RETURN_ON_NO_OP(vol_ops, notify);
    return bt_ops->vol_ops.notify(vol);
}

esp_err_t esp_bt_audio_vol_set_absolute(uint32_t vol)
{
    BT_AUDIO_RETURN_ON_NO_OP(vol_ops, set_absolute);
    return bt_ops->vol_ops.set_absolute(vol);
}

esp_err_t esp_bt_audio_vol_set_relative(bool up_down)
{
    BT_AUDIO_RETURN_ON_NO_OP(vol_ops, set_relative);
    return bt_ops->vol_ops.set_relative(up_down);
}

esp_err_t esp_bt_audio_pb_fetch(esp_bt_audio_pb_fetch_target_t target, uint16_t start_idx, uint16_t count)
{
    BT_AUDIO_RETURN_ON_NO_OP(pb_ops, fetch);
    return bt_ops->pb_ops.fetch(target, start_idx, count);
}

esp_err_t esp_bt_audio_classic_discovery_start(void)
{
    BT_AUDIO_RETURN_ON_NO_OP(classic_ops, start_discovery);
    return bt_ops->classic_ops.start_discovery();
}

esp_err_t esp_bt_audio_classic_discovery_stop(void)
{
    BT_AUDIO_RETURN_ON_NO_OP(classic_ops, stop_discovery);
    return bt_ops->classic_ops.stop_discovery();
}

esp_err_t esp_bt_audio_classic_connect(uint32_t role, uint8_t *bt_dev_addr)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (role == ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC) {
        BT_AUDIO_RETURN_ON_NO_OP(classic_ops, a2d_src_connect);
        return bt_ops->classic_ops.a2d_src_connect(bt_dev_addr);
    } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SNK) {
        BT_AUDIO_RETURN_ON_NO_OP(classic_ops, a2d_sink_connect);
        return bt_ops->classic_ops.a2d_sink_connect(bt_dev_addr);
    } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_HFP_HF) {
        BT_AUDIO_RETURN_ON_NO_OP(classic_ops, hfp_hf_connect);
        return bt_ops->classic_ops.hfp_hf_connect(bt_dev_addr);
    } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_HFP_AG) {
        BT_AUDIO_RETURN_ON_NO_OP(classic_ops, hfp_ag_connect);
        return bt_ops->classic_ops.hfp_ag_connect(bt_dev_addr);
    } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_PBAP_PCE) {
        BT_AUDIO_RETURN_ON_NO_OP(classic_ops, pbac_connect);
        return bt_ops->classic_ops.pbac_connect(bt_dev_addr);
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t esp_bt_audio_classic_disconnect(uint32_t role, uint8_t *bt_dev_addr)
{
    if (!bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    if (role == ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC) {
        BT_AUDIO_RETURN_ON_NO_OP(classic_ops, a2d_src_disconnect);
        return bt_ops->classic_ops.a2d_src_disconnect(bt_dev_addr);
    } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SNK) {
        BT_AUDIO_RETURN_ON_NO_OP(classic_ops, a2d_sink_disconnect);
        return bt_ops->classic_ops.a2d_sink_disconnect(bt_dev_addr);
    } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_HFP_HF) {
        BT_AUDIO_RETURN_ON_NO_OP(classic_ops, hfp_hf_disconnect);
        return bt_ops->classic_ops.hfp_hf_disconnect(bt_dev_addr);
    } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_HFP_AG) {
        BT_AUDIO_RETURN_ON_NO_OP(classic_ops, hfp_ag_disconnect);
        return bt_ops->classic_ops.hfp_ag_disconnect(bt_dev_addr);
    } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_PBAP_PCE) {
        BT_AUDIO_RETURN_ON_NO_OP(classic_ops, pbac_disconnect);
        return bt_ops->classic_ops.pbac_disconnect(bt_dev_addr);
    }
    return ESP_ERR_INVALID_ARG;
}

esp_err_t esp_bt_audio_classic_set_scan_mode(bool connectable, bool discoverable)
{
    BT_AUDIO_RETURN_ON_NO_OP(classic_ops, set_scan_mode);
    return bt_ops->classic_ops.set_scan_mode(connectable, discoverable);
}

#if CONFIG_BT_AUDIO && CONFIG_BT_ISO
esp_err_t esp_bt_audio_le_scan_start(uint32_t timeout_ms)
{
    return esp_bt_audio_le_scan_start_ext(NULL, timeout_ms);
}

esp_err_t esp_bt_audio_le_scan_start_ext(const uint8_t *target, uint32_t timeout_ms)
{
    BT_AUDIO_RETURN_ON_NO_OP(le_ops, start_scan);
    return bt_ops->le_ops.start_scan(target, timeout_ms);
}

esp_err_t esp_bt_audio_le_scan_stop(void)
{
    BT_AUDIO_RETURN_ON_NO_OP(le_ops, stop_scan);
    return bt_ops->le_ops.stop_scan();
}

esp_err_t esp_bt_audio_le_connect(uint8_t addr_type, const uint8_t *bt_dev_addr, uint32_t timeout_ms)
{
    BT_AUDIO_RETURN_ON_NO_OP(le_ops, connect);
    return bt_ops->le_ops.connect(addr_type, bt_dev_addr, timeout_ms);
}

esp_err_t esp_bt_audio_le_disconnect(void)
{
    return esp_bt_audio_le_disconnect_peer(NULL);
}

esp_err_t esp_bt_audio_le_disconnect_peer(const uint8_t *bt_dev_addr)
{
    BT_AUDIO_RETURN_ON_NO_OP(le_ops, disconnect);
    return bt_ops->le_ops.disconnect(bt_dev_addr);
}

esp_err_t esp_bt_audio_le_broadcast_source_start(void)
{
    BT_AUDIO_RETURN_ON_NO_OP(le_ops, broadcast_source_start);
    return bt_ops->le_ops.broadcast_source_start();
}

esp_err_t esp_bt_audio_le_broadcast_source_stop(void)
{
    BT_AUDIO_RETURN_ON_NO_OP(le_ops, broadcast_source_stop);
    return bt_ops->le_ops.broadcast_source_stop();
}

esp_err_t esp_bt_audio_le_broadcast_sync(const uint8_t *broadcast_name, const uint8_t *broadcast_code,
                                         uint32_t bit_field, uint32_t timeout_ms)
{
    BT_AUDIO_RETURN_ON_NO_OP(le_ops, broadcast_sync);
    return bt_ops->le_ops.broadcast_sync(broadcast_name, broadcast_code, bit_field, timeout_ms);
}

esp_err_t esp_bt_audio_le_pa_sync_terminate(void)
{
    BT_AUDIO_RETURN_ON_NO_OP(le_ops, pa_sync_terminate);
    return bt_ops->le_ops.pa_sync_terminate();
}
#endif  /* CONFIG_BT_AUDIO && CONFIG_BT_ISO */

esp_err_t esp_bt_audio_call_answer(uint8_t idx)
{
    BT_AUDIO_RETURN_ON_NO_OP(call_ops, answer_call);
    return bt_ops->call_ops.answer_call(idx);
}

esp_err_t esp_bt_audio_call_reject(uint8_t idx)
{
    BT_AUDIO_RETURN_ON_NO_OP(call_ops, reject_call);
    return bt_ops->call_ops.reject_call(idx);
}

esp_err_t esp_bt_audio_call_dial(const char *number)
{
    BT_AUDIO_RETURN_ON_NO_OP(call_ops, dial);
    return bt_ops->call_ops.dial(number);
}
