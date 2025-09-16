/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_check.h"

#include "esp_heap_caps.h"
#include "esp_sbc_dec.h"
#include "esp_sbc_enc.h"
#include "esp_sbc_def.h"

#include "esp_hf_client_api.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_event.h"
#include "esp_bt_audio_tel.h"
#include "esp_bt_audio_stream.h"
#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_classic_stream.h"
#include "bt_audio_hfp.h"
#include "bt_audio_ops.h"

/**
 * @brief  HFP Hands-Free profile context
 */
typedef struct {
    esp_hf_sync_conn_hdl_t           hf_conn;                           /*!< HFP sync connection handle (SCO/eSCO) */
    bt_audio_classic_stream_t       *snk_stream;                        /*!< Sink stream for downlink voice (AG -> HF) */
    bt_audio_classic_stream_t       *src_stream;                        /*!< Source stream for uplink voice (HF -> AG) */
    uint8_t                          call_state;                        /*!< CIND call: 0 = no call, 1 = call in progress */
    uint8_t                          call_setup;                        /*!< CIND callsetup: 0 = none, 1 = incoming, 2 = outgoing dialing, 3 = outgoing alerting */
    uint8_t                          call_held;                         /*!< CIND callheld: 0 = none, 1 = held and active, 2 = held */
    uint8_t                          call_count;                        /*!< Number of entries in calls[] (from last CLCC response) */
    esp_bt_audio_event_call_state_t  calls[ESP_BT_AUDIO_CALL_MAX_NUM];  /*!< Current call list; used to send INACTIVE per idx on NO_CALLS */
} hfp_hf_ctx_t;

static const char *TAG = "BT_AUD_HFP_HF";
static hfp_hf_ctx_t *hfp_hf_ctx = NULL;

const char *c_connection_state_str[] = {
    "DISCONNECTED",
    "CONNECTING",
    "CONNECTED",
    "SLC_CONNECTED",
    "DISCONNECTING",
};

const char *c_audio_state_str[] = {
    "DISCONNECTED",
    "CONNECTING",
    "CONNECTED",
    "CONNECTED_MSBC",
};

const char *c_vr_state_str[] = {
    "DISABLED",
    "ENABLED",
};

// esp_hf_service_availability_status_t
const char *c_service_availability_status_str[] = {
    "UNAVAILABLE",
    "AVAILABLE",
};

// esp_hf_roaming_status_t
const char *c_roaming_status_str[] = {
    "INACTIVE",
    "ACTIVE",
};

// esp_hf_client_call_state_t
const char *c_call_str[] = {
    "NO CALL IN PROGRESS",
    "CALL IN PROGRESS",
};

// esp_hf_client_callsetup_t
const char *c_call_setup_str[] = {
    "NONE",
    "INCOMING",
    "OUTGOING_DIALING",
    "OUTGOING_ALERTING",
};

// esp_hf_client_callheld_t
const char *c_call_held_str[] = {
    "NONE HELD",
    "HELD AND ACTIVE",
    "HELD",
};

// esp_hf_response_and_hold_status_t
const char *c_resp_and_hold_str[] = {
    "HELD",
    "HELD ACCEPTED",
    "HELD REJECTED",
};

// esp_hf_client_call_direction_t
const char *c_call_dir_str[] = {
    "OUTGOING",
    "INCOMING",
};

// esp_hf_client_call_state_t
const char *c_call_state_str[] = {
    "ACTIVE",
    "HELD",
    "DIALING",
    "ALERTING",
    "INCOMING",
    "WAITING",
    "HELD_BY_RESP_HOLD",
};

// esp_hf_current_call_mpty_type_t
const char *c_call_mpty_type_str[] = {
    "SINGLE",
    "MULTI",
};

// esp_hf_volume_control_target_t
const char *c_volume_control_target_str[] = {
    "SPEAKER",
    "MICROPHONE",
};

// esp_hf_at_response_code_t
const char *c_at_response_code_str[] = {
    "OK",
    "ERROR",
    "ERR_NO_CARRIER",
    "ERR_BUSY",
    "ERR_NO_ANSWER",
    "ERR_DELAYED",
    "ERR_BLACKLILSTED",
    "ERR_CME",
};

// esp_hf_subscriber_service_type_t
const char *c_subscriber_service_type_str[] = {
    "UNKNOWN",
    "VOICE",
    "FAX",
};

// esp_hf_client_in_band_ring_state_t
const char *c_inband_ring_state_str[] = {
    "NOT PROVIDED",
    "PROVIDED",
};

#if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI
static void bt_hf_client_msbc_stream_start()
{
    if (!hfp_hf_ctx->snk_stream) {
        esp_err_t ret = bt_audio_classic_stream_create(&hfp_hf_ctx->snk_stream);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create HFP sink stream: %s", esp_err_to_name(ret));
            return;
        }
        hfp_hf_ctx->snk_stream->conn_handle = hfp_hf_ctx->hf_conn;
        hfp_hf_ctx->snk_stream->base.context = ESP_BT_AUDIO_STREAM_CONTEXT_CONVERSATIONAL;
    }
    esp_sbc_dec_cfg_t *sbc_dec_cfg = heap_caps_calloc_prefer(1, sizeof(esp_sbc_dec_cfg_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (sbc_dec_cfg) {
        sbc_dec_cfg->sbc_mode = ESP_SBC_MODE_MSBC;
        sbc_dec_cfg->ch_num = 1;
        sbc_dec_cfg->enable_plc = true;
    } else {
        ESP_LOGE(TAG, "Failed to allocate sbc_dec_cfg");
        return;
    }
    hfp_hf_ctx->snk_stream->base.direction = ESP_BT_AUDIO_STREAM_DIR_SINK;
    hfp_hf_ctx->snk_stream->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_HFP;
    hfp_hf_ctx->snk_stream->base.codec_info.bits = 16;
    hfp_hf_ctx->snk_stream->base.codec_info.channels = ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT;
    hfp_hf_ctx->snk_stream->base.codec_info.sample_rate = 16000;
    hfp_hf_ctx->snk_stream->base.codec_info.frame_size = 240;
    hfp_hf_ctx->snk_stream->base.codec_info.codec_type = ESP_BT_AUDIO_STREAM_CODEC_SBC;
    hfp_hf_ctx->snk_stream->base.codec_info.codec_cfg = sbc_dec_cfg;
    hfp_hf_ctx->snk_stream->base.codec_info.cfg_size = sizeof(esp_sbc_dec_cfg_t);
    esp_bt_audio_event_stream_st_t event_data = {0};
    event_data.stream_handle = hfp_hf_ctx->snk_stream;
    event_data.state = ESP_BT_AUDIO_STREAM_STATE_ALLOCATED;
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
    event_data.state = ESP_BT_AUDIO_STREAM_STATE_STARTED;
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);

    if (!hfp_hf_ctx->src_stream) {
        esp_err_t ret = bt_audio_classic_stream_create(&hfp_hf_ctx->src_stream);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create HFP source stream: %s", esp_err_to_name(ret));
            return;
        }
        hfp_hf_ctx->src_stream->conn_handle = hfp_hf_ctx->hf_conn;
        hfp_hf_ctx->src_stream->base.context = ESP_BT_AUDIO_STREAM_CONTEXT_CONVERSATIONAL;
    }

    esp_sbc_enc_config_t *sbc_enc_cfg = heap_caps_calloc_prefer(1, sizeof(esp_sbc_enc_config_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (sbc_enc_cfg) {
        sbc_enc_cfg->sample_rate = 16000;
        sbc_enc_cfg->ch_mode = ESP_SBC_CH_MODE_MONO;
        sbc_enc_cfg->block_length = 15;
        sbc_enc_cfg->sub_bands_num = 8;
        sbc_enc_cfg->allocation_method = ESP_SBC_ALLOC_LOUDNESS;
        sbc_enc_cfg->bitpool = 26;
        sbc_enc_cfg->sbc_mode = ESP_SBC_MODE_MSBC;
    } else {
        ESP_LOGE(TAG, "Failed to allocate sbc_enc_cfg");
        return;
    }
    hfp_hf_ctx->src_stream->base.direction = ESP_BT_AUDIO_STREAM_DIR_SOURCE;
    hfp_hf_ctx->src_stream->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_HFP;
    hfp_hf_ctx->src_stream->base.codec_info.bits = 16;
    hfp_hf_ctx->src_stream->base.codec_info.channels = ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT;
    hfp_hf_ctx->src_stream->base.codec_info.sample_rate = 16000;
    hfp_hf_ctx->src_stream->base.codec_info.codec_type = ESP_BT_AUDIO_STREAM_CODEC_SBC;
    hfp_hf_ctx->src_stream->base.codec_info.codec_cfg = sbc_enc_cfg;
    hfp_hf_ctx->src_stream->base.codec_info.cfg_size = sizeof(esp_sbc_enc_config_t);
    event_data.stream_handle = hfp_hf_ctx->src_stream;
    event_data.state = ESP_BT_AUDIO_STREAM_STATE_ALLOCATED;
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
    event_data.state = ESP_BT_AUDIO_STREAM_STATE_STARTED;
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
}
#endif  /* CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI */

static inline uint8_t bt_audio_hfp_hf_battery_to_percent(int value)
{
    if (value <= 0) {
        return 0;
    }
    if (value <= 5) {
        return (uint8_t)(value * 20);
    }
    if (value >= 100) {
        return 100;
    }
    return (uint8_t)value;
}

static esp_bt_audio_call_state_t bt_audio_hfp_hf_clcc_state_to_call_state(esp_hf_current_call_status_t status)
{
    switch (status) {
        case ESP_HF_CURRENT_CALL_STATUS_ACTIVE:
            return ESP_BT_AUDIO_CALL_STATE_ACTIVE;
        case ESP_HF_CURRENT_CALL_STATUS_HELD:
        case ESP_HF_CURRENT_CALL_STATUS_HELD_BY_RESP_HOLD:
            return ESP_BT_AUDIO_CALL_STATE_LOCALLY_HELD;
        case ESP_HF_CURRENT_CALL_STATUS_DIALING:
            return ESP_BT_AUDIO_CALL_STATE_DIALING;
        case ESP_HF_CURRENT_CALL_STATUS_ALERTING:
            return ESP_BT_AUDIO_CALL_STATE_ALERTING;
        case ESP_HF_CURRENT_CALL_STATUS_INCOMING:
            return ESP_BT_AUDIO_CALL_STATE_INCOMING;
        case ESP_HF_CURRENT_CALL_STATUS_WAITING:
            return ESP_BT_AUDIO_CALL_STATE_INCOMING;
        default:
            return ESP_BT_AUDIO_CALL_STATE_INACTIVE;
    }
}

static void bt_audio_hfp_hf_dispatch_tel_status(esp_bt_audio_tel_event_t type, esp_bt_audio_tel_status_event_t *status_data)
{
    esp_bt_audio_event_tel_status_chg_t event_data = {0};
    event_data.tech = ESP_BT_AUDIO_TECH_CLASSIC;
    event_data.type = type;
    if (status_data) {
        event_data.data = *status_data;
    }
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_TEL_STATUS_CHG, &event_data);
}

static void bt_audio_hfp_hf_dispatch_clcc_call(const esp_hf_client_cb_param_t *param)
{
    esp_bt_audio_event_call_state_t event_data = {0};

    event_data.tech = ESP_BT_AUDIO_TECH_CLASSIC;
    event_data.idx = (uint8_t)param->clcc.idx;
    event_data.dir = (param->clcc.dir == 1) ? ESP_BT_AUDIO_CALL_DIR_INCOMING : ESP_BT_AUDIO_CALL_DIR_OUTGOING;
    event_data.state = bt_audio_hfp_hf_clcc_state_to_call_state(param->clcc.status);
    if (param->clcc.number) {
        strncpy(event_data.uri, param->clcc.number, sizeof(event_data.uri) - 1);
        event_data.uri[sizeof(event_data.uri) - 1] = '\0';
    }

    if (hfp_hf_ctx) {
        uint8_t i;
        for (i = 0; i < hfp_hf_ctx->call_count; i++) {
            if (hfp_hf_ctx->calls[i].idx == event_data.idx) {
                hfp_hf_ctx->calls[i] = event_data;
                break;
            }
        }
        if (i >= hfp_hf_ctx->call_count && hfp_hf_ctx->call_count < ESP_BT_AUDIO_CALL_MAX_NUM) {
            hfp_hf_ctx->calls[hfp_hf_ctx->call_count] = event_data;
            hfp_hf_ctx->call_count++;
        }
    }
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CALL_STATE_CHG, &event_data);
}

static bool bt_audio_hfp_hf_has_call(void)
{
    if (!hfp_hf_ctx) {
        return false;
    }
    return (hfp_hf_ctx->call_state != 0) ||
           (hfp_hf_ctx->call_setup != 0) ||
           (hfp_hf_ctx->call_held != 0);
}

static void bt_audio_hfp_hf_dispatch_call_ended(void)
{
    if (!hfp_hf_ctx) {
        return;
    }
    for (uint8_t i = 0; i < hfp_hf_ctx->call_count; i++) {
        esp_bt_audio_event_call_state_t event_data = {0};
        event_data.tech = ESP_BT_AUDIO_TECH_CLASSIC;
        event_data.idx = hfp_hf_ctx->calls[i].idx;
        event_data.dir = hfp_hf_ctx->calls[i].dir;
        event_data.state = ESP_BT_AUDIO_CALL_STATE_INACTIVE;
        event_data.uri[0] = '\0';
        bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CALL_STATE_CHG, &event_data);
    }
    hfp_hf_ctx->call_count = 0;
}

static void bt_audio_hfp_hf_sync_call_state(void)
{
    if (!bt_audio_hfp_hf_has_call()) {
        bt_audio_hfp_hf_dispatch_call_ended();
    } else {
        esp_hf_client_query_current_calls();
    }
}

static void bt_audio_hfp_hf_client_cb(esp_hf_client_cb_event_t event, esp_hf_client_cb_param_t *param)
{
    switch (event) {
        case ESP_HF_CLIENT_CONNECTION_STATE_EVT: {
            ESP_LOGI(TAG, "--connection state %s, peer feats 0x%" PRIx32 ", chld_feats 0x%" PRIx32,
                     c_connection_state_str[param->conn_stat.state],
                     param->conn_stat.peer_feat,
                     param->conn_stat.chld_feat);
            break;
        }

        case ESP_HF_CLIENT_AUDIO_STATE_EVT: {
            ESP_LOGI(TAG, "--audio state %s, preferred_frame_size: %d",
                     c_audio_state_str[param->audio_stat.state], param->audio_stat.preferred_frame_size);
#if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI
            if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED_MSBC) {
                hfp_hf_ctx->hf_conn = param->audio_stat.sync_conn_handle;
                bt_hf_client_msbc_stream_start();
            } else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_DISCONNECTED) {
                hfp_hf_ctx->hf_conn = 0;
                if (!hfp_hf_ctx->src_stream) {
                    ESP_LOGE(TAG, "no source stream found");
                } else {
                    esp_bt_audio_event_stream_st_t event_data = {0};
                    event_data.stream_handle = hfp_hf_ctx->src_stream;
                    event_data.state = ESP_BT_AUDIO_STREAM_STATE_STOPPED;
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                    event_data.state = ESP_BT_AUDIO_STREAM_STATE_RELEASED;
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                    bt_audio_classic_stream_destroy(hfp_hf_ctx->src_stream);
                    hfp_hf_ctx->src_stream = NULL;
                }
                if (!hfp_hf_ctx->snk_stream) {
                    ESP_LOGE(TAG, "disconnect no sink stream found");
                } else {
                    esp_bt_audio_stream_packet_t msg = {0};
                    if (uxQueueSpacesAvailable(hfp_hf_ctx->snk_stream->base.data_q) == 0) {
                        if (xQueueReceive(hfp_hf_ctx->snk_stream->base.data_q, &msg, 0) == pdTRUE) {
                            if (msg.data_owner) {
                                esp_hf_client_audio_buff_free((esp_hf_audio_buff_t *)msg.data_owner);
                            }
                        }
                    }
                    msg.data = NULL;
                    msg.size = 0;
                    msg.bad_frame = false;
                    msg.data_owner = NULL;
                    msg.is_done = true;
                    if (xQueueSend(hfp_hf_ctx->snk_stream->base.data_q, &msg, 0) != pdTRUE) {
                        ESP_LOGE(TAG, "HFP handsfree failed to send done packet to queue");
                    }
                    esp_bt_audio_event_stream_st_t event_data = {0};
                    event_data.stream_handle = hfp_hf_ctx->snk_stream;
                    event_data.state = ESP_BT_AUDIO_STREAM_STATE_STOPPED;
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                    event_data.state = ESP_BT_AUDIO_STREAM_STATE_RELEASED;
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                    bt_audio_classic_stream_destroy(hfp_hf_ctx->snk_stream);
                    hfp_hf_ctx->snk_stream = NULL;
                }
            } else if (param->audio_stat.state == ESP_HF_CLIENT_AUDIO_STATE_CONNECTED) {
                ESP_LOGW(TAG, "CVSD is not supported now");
            }
#endif  /* #if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI */
            break;
        }

        case ESP_HF_CLIENT_BVRA_EVT: {
            ESP_LOGI(TAG, "--VR state %s",
                     c_vr_state_str[param->bvra.value]);
            break;
        }

        case ESP_HF_CLIENT_CIND_SERVICE_AVAILABILITY_EVT: {
            ESP_LOGI(TAG, "--NETWORK STATE %s",
                     c_service_availability_status_str[param->service_availability.status]);
            esp_bt_audio_tel_status_event_t tel_status = {0};
            tel_status.network.available = (param->service_availability.status != 0);
            bt_audio_hfp_hf_dispatch_tel_status(ESP_BT_AUDIO_TEL_STATUS_NETWORK, &tel_status);
            break;
        }

        case ESP_HF_CLIENT_CIND_ROAMING_STATUS_EVT: {
            ESP_LOGD(TAG, "--ROAMING: %s",
                     c_roaming_status_str[param->roaming.status]);
            esp_bt_audio_tel_status_event_t tel_status = {0};
            tel_status.roaming.active = (param->roaming.status != 0);
            bt_audio_hfp_hf_dispatch_tel_status(ESP_BT_AUDIO_TEL_STATUS_ROAMING, &tel_status);
            break;
        }

        case ESP_HF_CLIENT_CIND_SIGNAL_STRENGTH_EVT: {
            ESP_LOGI(TAG, "-- signal strength: %d",
                     param->signal_strength.value);
            esp_bt_audio_tel_status_event_t tel_status = {0};
            tel_status.signal_strength.value = param->signal_strength.value;
            bt_audio_hfp_hf_dispatch_tel_status(ESP_BT_AUDIO_TEL_STATUS_SIGNAL_STRENGTH, &tel_status);
            break;
        }

        case ESP_HF_CLIENT_CIND_BATTERY_LEVEL_EVT: {
            ESP_LOGI(TAG, "--battery level %d",
                     param->battery_level.value);
            esp_bt_audio_tel_status_event_t tel_status = {0};
            tel_status.battery.level = bt_audio_hfp_hf_battery_to_percent(param->battery_level.value);
            tel_status.battery.instance_id = 0;
            bt_audio_hfp_hf_dispatch_tel_status(ESP_BT_AUDIO_TEL_STATUS_BATTERY, &tel_status);
            break;
        }

        case ESP_HF_CLIENT_COPS_CURRENT_OPERATOR_EVT: {
            ESP_LOGI(TAG, "--operator name: %s",
                     param->cops.name);
            esp_bt_audio_tel_status_event_t tel_status = {0};
            if (param->cops.name) {
                strncpy(tel_status.operator_name.name, param->cops.name, sizeof(tel_status.operator_name.name) - 1);
                tel_status.operator_name.name[sizeof(tel_status.operator_name.name) - 1] = '\0';
            }
            bt_audio_hfp_hf_dispatch_tel_status(ESP_BT_AUDIO_TEL_STATUS_OPERATOR, &tel_status);
            break;
        }

        case ESP_HF_CLIENT_CIND_CALL_EVT: {
            ESP_LOGI(TAG, "--Call indicator %s",
                     c_call_str[param->call.status]);
            hfp_hf_ctx->call_state = param->call.status;
            bt_audio_hfp_hf_sync_call_state();
            break;
        }

        case ESP_HF_CLIENT_CIND_CALL_SETUP_EVT: {
            ESP_LOGI(TAG, "--Call setup indicator %s",
                     c_call_setup_str[param->call_setup.status]);
            hfp_hf_ctx->call_setup = param->call_setup.status;
            bt_audio_hfp_hf_sync_call_state();
            break;
        }

        case ESP_HF_CLIENT_CIND_CALL_HELD_EVT: {
            ESP_LOGI(TAG, "--Call held indicator %s",
                     c_call_held_str[param->call_held.status]);
            hfp_hf_ctx->call_held = param->call_held.status;
            bt_audio_hfp_hf_sync_call_state();
            break;
        }

        case ESP_HF_CLIENT_BTRH_EVT: {
            ESP_LOGI(TAG, "--response and hold %s",
                     c_resp_and_hold_str[param->btrh.status]);
            break;
        }

        case ESP_HF_CLIENT_CLIP_EVT: {
            ESP_LOGI(TAG, "--clip number %s",
                     (param->clip.number == NULL) ? "NULL" : (param->clip.number));
            break;
        }

        case ESP_HF_CLIENT_CCWA_EVT: {
            ESP_LOGI(TAG, "--call_waiting %s",
                     (param->ccwa.number == NULL) ? "NULL" : (param->ccwa.number));
            break;
        }

        case ESP_HF_CLIENT_CLCC_EVT: {
            ESP_LOGI(TAG, "--Current call: idx %d, dir %s, state %s, mpty %s, number %s",
                     param->clcc.idx,
                     c_call_dir_str[param->clcc.dir],
                     c_call_state_str[param->clcc.status],
                     c_call_mpty_type_str[param->clcc.mpty],
                     (param->clcc.number == NULL) ? "NULL" : (param->clcc.number));
            bt_audio_hfp_hf_dispatch_clcc_call(param);
            break;
        }

        case ESP_HF_CLIENT_VOLUME_CONTROL_EVT: {
            ESP_LOGI(TAG, "--volume_target: %s, volume %d",
                     c_volume_control_target_str[param->volume_control.type],
                     param->volume_control.volume);

            uint8_t hfp_volume = param->volume_control.volume;
            uint8_t app_vol = (uint8_t)((hfp_volume * 100) / 15);
            ESP_LOGI(TAG, "HFP: Converted %s volume %d -> %d (app range 0-100)",
                     c_volume_control_target_str[param->volume_control.type],
                     hfp_volume, app_vol);
            if (param->volume_control.type == ESP_HF_VOLUME_CONTROL_TARGET_SPK) {
                esp_bt_audio_event_vol_absolute_t event_data = {0};
                event_data.context = ESP_BT_AUDIO_STREAM_CONTEXT_CONVERSATIONAL;
                event_data.vol = app_vol;
                event_data.mute = false;
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_VOL_ABSOLUTE, &event_data);
            }
            break;
        }

        case ESP_HF_CLIENT_AT_RESPONSE_EVT: {
            ESP_LOGD(TAG, "--AT response event, code %d, cme %d",
                     param->at_response.code, param->at_response.cme);
            break;
        }

        case ESP_HF_CLIENT_CNUM_EVT: {
            ESP_LOGI(TAG, "--subscriber type %s, number %s",
                     c_subscriber_service_type_str[param->cnum.type],
                     (param->cnum.number == NULL) ? "NULL" : param->cnum.number);
            break;
        }

        case ESP_HF_CLIENT_BSIR_EVT: {
            ESP_LOGI(TAG, "--inband ring state %s",
                     c_inband_ring_state_str[param->bsir.state]);
            break;
        }

        case ESP_HF_CLIENT_BINP_EVT: {
            ESP_LOGI(TAG, "--last voice tag number: %s",
                     (param->binp.number == NULL) ? "NULL" : param->binp.number);
            break;
        }
        case ESP_HF_CLIENT_RING_IND_EVT: {
            ESP_LOGD(TAG, "--ring indication");
            break;
        }
        case ESP_HF_CLIENT_PKT_STAT_NUMS_GET_EVT: {
            ESP_LOGE(TAG, "ESP_HF_CLIENT_PKT_STAT_NUMS_GET_EVT: %d", event);
            break;
        }
        case ESP_HF_CLIENT_PROF_STATE_EVT: {
            if (ESP_HF_INIT_SUCCESS == param->prof_stat.state) {
                ESP_LOGI(TAG, "HF PROF STATE: Init Complete");
            } else if (ESP_HF_DEINIT_SUCCESS == param->prof_stat.state) {
                ESP_LOGI(TAG, "HF PROF STATE: Deinit Complete");
            } else {
                ESP_LOGE(TAG, "HF PROF STATE error: %d", param->prof_stat.state);
            }
            break;
        }
        default:
            ESP_LOGE(TAG, "HF_CLIENT EVT: %d", event);
            break;
    }
}

static void bt_hf_client_audio_data_cb(esp_hf_sync_conn_hdl_t sync_conn_hdl, esp_hf_audio_buff_t *audio_buf, bool is_bad_frame)
{
    bt_audio_classic_stream_t *stream = hfp_hf_ctx->snk_stream;
    if (!stream) {
        ESP_LOGE(TAG, "No hf stream found");
        esp_hf_client_audio_buff_free(audio_buf);
        return;
    }
    esp_bt_audio_stream_packet_t msg = {0};
    if (uxQueueSpacesAvailable(stream->base.data_q) == 0) {
        if (xQueueReceive(stream->base.data_q, &msg, 0) == pdTRUE) {
            if (msg.data_owner) {
                esp_hf_client_audio_buff_free((esp_hf_audio_buff_t *)msg.data_owner);
            }
        }
    }
    msg.data = audio_buf->data;
    msg.size = 57;
    msg.bad_frame = is_bad_frame;
    msg.data_owner = audio_buf;
    msg.is_done = false;
    if (xQueueSend(stream->base.data_q, &msg, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send audio buffer to queue");
        esp_hf_client_audio_buff_free(audio_buf);
    }
}

static esp_err_t hfp_hf_connect(uint8_t *bda)
{
    return esp_hf_client_connect(bda);
}

static esp_err_t hfp_hf_disconnect(uint8_t *bda)
{
    return esp_hf_client_disconnect(bda);
}

static esp_err_t hfp_hf_answer_call(uint8_t idx)
{
    (void)idx;
    return esp_hf_client_answer_call();
}

static esp_err_t hfp_hf_reject_call(uint8_t idx)
{
    (void)idx;
    return esp_hf_client_reject_call();
}

static esp_err_t hfp_hf_dial(const char *number)
{
    return esp_hf_client_dial(number);
}

esp_err_t bt_audio_hfp_hf_init()
{
    if (hfp_hf_ctx) {
        ESP_LOGI(TAG, "HF client already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    hfp_hf_ctx = heap_caps_calloc_prefer(1, sizeof(hfp_hf_ctx_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(hfp_hf_ctx, ESP_ERR_NO_MEM, TAG, "Failed to malloc space for hfp_hf_ctx");
    ESP_ERROR_CHECK(esp_hf_client_init());
    ESP_ERROR_CHECK(esp_hf_client_register_callback(bt_audio_hfp_hf_client_cb));
    ESP_ERROR_CHECK(esp_hf_client_register_audio_data_callback(bt_hf_client_audio_data_cb));
    esp_bt_audio_classic_ops_t hfp_hf_ops = {0};
    bt_audio_ops_get_classic(&hfp_hf_ops);
    hfp_hf_ops.hfp_hf_connect = hfp_hf_connect;
    hfp_hf_ops.hfp_hf_disconnect = hfp_hf_disconnect;
    ESP_ERROR_CHECK(bt_audio_ops_set_classic(&hfp_hf_ops));

    esp_bt_audio_call_ops_t call_ops = {0};
    bt_audio_ops_get_call(&call_ops);
    call_ops.answer_call = hfp_hf_answer_call;
    call_ops.reject_call = hfp_hf_reject_call;
    call_ops.dial = hfp_hf_dial;
    ESP_ERROR_CHECK(bt_audio_ops_set_call(&call_ops));
    ESP_LOGI(TAG, "HF client init success");
    return ESP_OK;
}

esp_err_t bt_audio_hfp_hf_deinit()
{
    if (!hfp_hf_ctx) {
        ESP_LOGI(TAG, "HF client not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_bt_audio_call_ops_t call_ops = {0};
    bt_audio_ops_get_call(&call_ops);
    call_ops.answer_call = NULL;
    call_ops.reject_call = NULL;
    call_ops.dial = NULL;
    ESP_ERROR_CHECK(bt_audio_ops_set_call(&call_ops));

    if (hfp_hf_ctx->snk_stream) {
        bt_audio_classic_stream_destroy(hfp_hf_ctx->snk_stream);
        hfp_hf_ctx->snk_stream = NULL;
    }
    if (hfp_hf_ctx->src_stream) {
        bt_audio_classic_stream_destroy(hfp_hf_ctx->src_stream);
        hfp_hf_ctx->src_stream = NULL;
    }

    free(hfp_hf_ctx);
    hfp_hf_ctx = NULL;
    esp_hf_client_deinit();
    ESP_LOGI(TAG, "HF client deinit success");
    return ESP_OK;
}
