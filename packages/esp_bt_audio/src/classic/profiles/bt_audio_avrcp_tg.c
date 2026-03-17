/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>

#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

#ifdef CONFIG_BT_AVRCP_ENABLED
#include "esp_avrc_api.h"
#endif  /* CONFIG_BT_AVRCP_ENABLED */

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_stream.h"
#include "bt_audio_avrcp.h"
#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_ops.h"

typedef struct {
    bool     is_connected;
    uint8_t  volume;
    bool     volume_notify;
} avrcp_tg_ctx_t;

static const char *TAG = "BT_AUD_AVRC_TG";
static avrcp_tg_ctx_t *avrcp_tg = NULL;

static inline uint8_t vol_from_avrc(uint8_t avrcp_vol)
{
    return (uint8_t)((avrcp_vol * 100) / 127);
}

static inline uint8_t vol_to_avrc(uint8_t app_vol)
{
    return (uint8_t)((app_vol * 127) / 100);
}

static esp_err_t avrcp_tg_vol_changed(uint32_t vol)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG, "TG: Volume changed to %d (app range 0-100)", (int)vol);
    if (!avrcp_tg || !avrcp_tg->is_connected) {
        ESP_LOGW(TAG, "TG not connected");
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t avrcp_vol = vol_to_avrc(vol);
    if (avrcp_tg && avrcp_tg->volume_notify) {
        esp_avrc_rn_param_t rn_param;
        rn_param.volume = avrcp_vol;
        ret = esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_CHANGED, &rn_param);
        avrcp_tg->volume_notify = false;
        avrcp_tg->volume = avrcp_vol;
        ESP_LOGI(TAG, "TG: Converted volume %d -> %d, sent notification", (int)vol, avrcp_vol);
    }
    return ret;
}

static void bt_app_rc_tg_cb(esp_avrc_tg_cb_event_t event, esp_avrc_tg_cb_param_t *param)
{
    ESP_LOGD(TAG, "%s event: %d", __func__, event);

    esp_avrc_tg_cb_param_t *rc = (esp_avrc_tg_cb_param_t *)(param);

    switch (event) {
        case ESP_AVRC_TG_CONNECTION_STATE_EVT: {
            ESP_LOGI(TAG, "TG conn_state evt: state %d, [%02x:%02x:%02x:%02x:%02x:%02x]",
                     rc->conn_stat.connected, rc->conn_stat.remote_bda[0], rc->conn_stat.remote_bda[1], rc->conn_stat.remote_bda[2], rc->conn_stat.remote_bda[3], rc->conn_stat.remote_bda[4], rc->conn_stat.remote_bda[5]);
            if (avrcp_tg) {
                avrcp_tg->is_connected = rc->conn_stat.connected;
                esp_bt_audio_vol_ops_t vol_ops = {0};
                bt_audio_ops_get_vol(&vol_ops);
                if (rc->conn_stat.connected) {
                    vol_ops.notify = avrcp_tg_vol_changed;
                } else {
                    vol_ops.notify = NULL;
                }
                bt_audio_ops_set_vol(&vol_ops);
            }
            break;
        }
        case ESP_AVRC_TG_REMOTE_FEATURES_EVT: {
            ESP_LOGI(TAG, "TG remote features evt: remote_features %d", rc->rmt_feats.feat_mask);
            break;
        }
        case ESP_AVRC_TG_PASSTHROUGH_CMD_EVT: {
            ESP_LOGI(TAG, "TG passthrough cmd evt: key_code 0x%x, key_state %d", rc->psth_cmd.key_code, rc->psth_cmd.key_state);
            if (rc->psth_cmd.key_state != ESP_AVRC_PT_CMD_STATE_PRESSED) {
                break;
            }
            uint32_t cmd = 0;
            switch (rc->psth_cmd.key_code) {
                case ESP_AVRC_PT_CMD_PLAY:
                case ESP_AVRC_PT_CMD_PAUSE:
                case ESP_AVRC_PT_CMD_STOP:
                case ESP_AVRC_PT_CMD_FORWARD:
                case ESP_AVRC_PT_CMD_BACKWARD:
                    if (rc->psth_cmd.key_code == ESP_AVRC_PT_CMD_PLAY) {
                        cmd = ESP_BT_AUDIO_MEDIA_CTRL_CMD_PLAY;
                    } else if (rc->psth_cmd.key_code == ESP_AVRC_PT_CMD_PAUSE) {
                        cmd = ESP_BT_AUDIO_MEDIA_CTRL_CMD_PAUSE;
                    } else if (rc->psth_cmd.key_code == ESP_AVRC_PT_CMD_STOP) {
                        cmd = ESP_BT_AUDIO_MEDIA_CTRL_CMD_STOP;
                    } else if (rc->psth_cmd.key_code == ESP_AVRC_PT_CMD_FORWARD) {
                        cmd = ESP_BT_AUDIO_MEDIA_CTRL_CMD_NEXT;
                    } else if (rc->psth_cmd.key_code == ESP_AVRC_PT_CMD_BACKWARD) {
                        cmd = ESP_BT_AUDIO_MEDIA_CTRL_CMD_PREV;
                    }
                    esp_bt_audio_event_media_ctrl_t event_data = {0};
                    event_data.cmd = cmd;
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_MEDIA_CTRL_CMD, &event_data);
                    break;
                case ESP_AVRC_PT_CMD_VOL_UP:
                case ESP_AVRC_PT_CMD_VOL_DOWN: {
                    esp_bt_audio_event_vol_relative_t event_data = {0};
                    event_data.context = ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA;
                    event_data.up_down = rc->psth_cmd.key_code == ESP_AVRC_PT_CMD_VOL_UP ? true : false;
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_VOL_RELATIVE, &event_data);
                    break;
                }
            }
            break;
        }
        case ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT: {
            ESP_LOGI(TAG, "TG set absolute volume cmd evt: volume %d (AVRCP range 0-127)", rc->set_abs_vol.volume);
            esp_bt_audio_event_vol_absolute_t event_data = {0};
            event_data.context = ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA;
            event_data.vol = vol_from_avrc(rc->set_abs_vol.volume);
            event_data.mute = false;
            ESP_LOGD(TAG, "TG: Converted volume %d -> %d (app range 0-100)", rc->set_abs_vol.volume, event_data.vol);
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_VOL_ABSOLUTE, &event_data);
            if (avrcp_tg) {
                avrcp_tg->volume = rc->set_abs_vol.volume;
            }
            break;
        }
        case ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT: {
            ESP_LOGI(TAG, "TG register notification evt: event_id %d, event_parameter %d", rc->reg_ntf.event_id, rc->reg_ntf.event_parameter);
            if (rc->reg_ntf.event_id == ESP_AVRC_RN_VOLUME_CHANGE) {
                esp_avrc_rn_param_t rn_param;
                rn_param.volume = avrcp_tg->volume;
                esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE, ESP_AVRC_RN_RSP_INTERIM, &rn_param);
                if (avrcp_tg) {
                    avrcp_tg->volume_notify = true;
                }
            }
            break;
        }
        case ESP_AVRC_TG_SET_PLAYER_APP_VALUE_EVT: {
            ESP_LOGI(TAG, "TG set player app value evt: num_val %d", rc->set_app_value.num_val);
            break;
        }
        case ESP_AVRC_TG_PROF_STATE_EVT: {
            ESP_LOGI(TAG, "TG prof state evt: prof_state %d", rc->avrc_tg_init_stat.state);
            break;
        }
        default: {
            ESP_LOGI(TAG, "TG unhandled event: %d", event);
            break;
        }
    }
}

esp_err_t bt_audio_avrcp_tg_init()
{
    if (avrcp_tg) {
        return ESP_ERR_INVALID_STATE;
    }
    avrcp_tg = heap_caps_calloc_prefer(1, sizeof(avrcp_tg_ctx_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(avrcp_tg, ESP_ERR_NO_MEM, TAG, "Failed to malloc space for avrcp_tg_ctx");
    ESP_RETURN_ON_ERROR(esp_avrc_tg_init(), TAG, "esp_avrc_tg_init failed");
    ESP_RETURN_ON_ERROR(esp_avrc_tg_register_callback(bt_app_rc_tg_cb), TAG, "esp_avrc_tg_register_callback failed");
    esp_avrc_rn_evt_cap_mask_t evt_set = {0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &evt_set, ESP_AVRC_RN_VOLUME_CHANGE);
    ESP_RETURN_ON_ERROR(esp_avrc_tg_set_rn_evt_cap(&evt_set), TAG, "esp_avrc_tg_set_rn_evt_cap failed");
    esp_avrc_psth_bit_mask_t mask = {0};
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &mask, ESP_AVRC_PT_CMD_STOP);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &mask, ESP_AVRC_PT_CMD_PLAY);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &mask, ESP_AVRC_PT_CMD_PAUSE);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &mask, ESP_AVRC_PT_CMD_FORWARD);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &mask, ESP_AVRC_PT_CMD_BACKWARD);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &mask, ESP_AVRC_PT_CMD_VOL_UP);
    esp_avrc_psth_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET, &mask, ESP_AVRC_PT_CMD_VOL_DOWN);
    esp_avrc_tg_set_psth_cmd_filter(ESP_AVRC_PSTH_FILTER_SUPPORTED_CMD, &mask);
    ESP_LOGI(TAG, "TG init success");
    return ESP_OK;
}

esp_err_t bt_audio_avrcp_tg_deinit()
{
    if (!avrcp_tg) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_avrc_tg_deinit();
    free(avrcp_tg);
    avrcp_tg = NULL;
    ESP_LOGI(TAG, "TG deinit success");
    return ESP_OK;
}
