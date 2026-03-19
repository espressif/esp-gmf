/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "esp_heap_caps.h"
#include "esp_err.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_tel.h"
#include "esp_bt_audio_pb.h"
#include "bt_audio_ops.h"

typedef struct {
    esp_bt_audio_playback_ops_t  playback_ops;
    esp_bt_audio_media_ops_t     media_ops;
    esp_bt_audio_vol_ops_t       vol_ops;
    esp_bt_audio_classic_ops_t   classic_ops;
    esp_bt_audio_call_ops_t      call_ops;
    esp_bt_audio_pb_ops_t        pb_ops;
} gmf_bt_ops_t;

static gmf_bt_ops_t *bt_ops;

esp_err_t bt_audio_ops_init(void)
{
    if (bt_ops) {
        return ESP_ERR_INVALID_STATE;
    }
    bt_ops = heap_caps_calloc_prefer(1, sizeof(gmf_bt_ops_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (!bt_ops) {
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

esp_err_t esp_bt_audio_playback_play(void)
{
    if (bt_ops && bt_ops->playback_ops.play) {
        return bt_ops->playback_ops.play();
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_playback_pause(void)
{
    if (bt_ops && bt_ops->playback_ops.pause) {
        return bt_ops->playback_ops.pause();
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_playback_stop(void)
{
    if (bt_ops && bt_ops->playback_ops.stop) {
        return bt_ops->playback_ops.stop();
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_playback_next(void)
{
    if (bt_ops && bt_ops->playback_ops.next) {
        return bt_ops->playback_ops.next();
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_playback_prev(void)
{
    if (bt_ops && bt_ops->playback_ops.prev) {
        return bt_ops->playback_ops.prev();
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_playback_request_metadata(uint32_t mask)
{
    if (bt_ops && bt_ops->playback_ops.request_metadata) {
        return bt_ops->playback_ops.request_metadata(mask);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_playback_reg_notifications(uint32_t mask)
{
    if (bt_ops && bt_ops->playback_ops.reg_notifications) {
        return bt_ops->playback_ops.reg_notifications(mask);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_media_start(uint32_t role, void *config)
{
    if (bt_ops && role == ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC && bt_ops->media_ops.a2d_media_start) {
        return bt_ops->media_ops.a2d_media_start(config);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_media_stop(uint32_t role)
{
    if (bt_ops && role == ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC && bt_ops->media_ops.a2d_media_stop) {
        return bt_ops->media_ops.a2d_media_stop();
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_vol_notify(uint32_t vol)
{
    if (bt_ops && bt_ops->vol_ops.notify) {
        return bt_ops->vol_ops.notify(vol);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_vol_set_absolute(uint32_t vol)
{
    if (bt_ops && bt_ops->vol_ops.set_absolute) {
        return bt_ops->vol_ops.set_absolute(vol);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_vol_set_relative(bool up_down)
{
    if (bt_ops && bt_ops->vol_ops.set_relative) {
        return bt_ops->vol_ops.set_relative(up_down);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_pb_fetch(esp_bt_audio_pb_fetch_target_t target, uint16_t start_idx, uint16_t count)
{
    if (bt_ops && bt_ops->pb_ops.fetch) {
        return bt_ops->pb_ops.fetch(target, start_idx, count);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_classic_discovery_start(void)
{
    if (bt_ops && bt_ops->classic_ops.start_discovery) {
        return bt_ops->classic_ops.start_discovery();
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_classic_discovery_stop(void)
{
    if (bt_ops && bt_ops->classic_ops.stop_discovery) {
        return bt_ops->classic_ops.stop_discovery();
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_classic_connect(uint32_t role, uint8_t *bt_dev_addr)
{
    if (bt_ops) {
        if (role == ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC && bt_ops->classic_ops.a2d_src_connect) {
            return bt_ops->classic_ops.a2d_src_connect(bt_dev_addr);
        } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SNK && bt_ops->classic_ops.a2d_sink_connect) {
            return bt_ops->classic_ops.a2d_sink_connect(bt_dev_addr);
        } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_HFP_HF && bt_ops->classic_ops.hfp_hf_connect) {
            return bt_ops->classic_ops.hfp_hf_connect(bt_dev_addr);
        } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_PBAP_PCE && bt_ops->classic_ops.pbac_connect) {
            return bt_ops->classic_ops.pbac_connect(bt_dev_addr);
        } else {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t esp_bt_audio_classic_disconnect(uint32_t role, uint8_t *bt_dev_addr)
{
    if (bt_ops) {
        if (role == ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC && bt_ops->classic_ops.a2d_src_disconnect) {
            return bt_ops->classic_ops.a2d_src_disconnect(bt_dev_addr);
        } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SNK && bt_ops->classic_ops.a2d_sink_disconnect) {
            return bt_ops->classic_ops.a2d_sink_disconnect(bt_dev_addr);
        } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_HFP_HF && bt_ops->classic_ops.hfp_hf_disconnect) {
            return bt_ops->classic_ops.hfp_hf_disconnect(bt_dev_addr);
        } else if (role == ESP_BT_AUDIO_CLASSIC_ROLE_PBAP_PCE && bt_ops->classic_ops.pbac_disconnect) {
            return bt_ops->classic_ops.pbac_disconnect(bt_dev_addr);
        } else {
            return ESP_ERR_INVALID_ARG;
        }
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t esp_bt_audio_classic_set_scan_mode(bool connectable, bool discoverable)
{
    if (bt_ops && bt_ops->classic_ops.set_scan_mode) {
        return bt_ops->classic_ops.set_scan_mode(connectable, discoverable);
    } else {
        return ESP_ERR_INVALID_STATE;
    }
}

esp_err_t esp_bt_audio_call_answer(uint8_t idx)
{
    if (bt_ops && bt_ops->call_ops.answer_call) {
        return bt_ops->call_ops.answer_call(idx);
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t esp_bt_audio_call_reject(uint8_t idx)
{
    if (bt_ops && bt_ops->call_ops.reject_call) {
        return bt_ops->call_ops.reject_call(idx);
    }
    return ESP_ERR_INVALID_STATE;
}

esp_err_t esp_bt_audio_call_dial(const char *number)
{
    if (bt_ops && bt_ops->call_ops.dial) {
        return bt_ops->call_ops.dial(number);
    }
    return ESP_ERR_INVALID_STATE;
}
