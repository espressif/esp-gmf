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
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_sbc_dec.h"
#include "esp_sbc_enc.h"
#include "esp_sbc_def.h"
#include "esp_hf_ag_api.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_event.h"
#include "esp_bt_audio_tel.h"
#include "esp_bt_audio_stream.h"
#include "bt_audio_evt_dispatcher.h"
#include "bt_audio_classic_stream.h"
#include "bt_audio_hfp.h"
#include "bt_audio_ops.h"

typedef struct {
    esp_hf_sync_conn_hdl_t     ag_conn;
    esp_bd_addr_t              peer_bda;
    bool                       peer_connected;
    bt_audio_classic_stream_t *src_stream;
    bt_audio_classic_stream_t *snk_stream;
    uint8_t                    call_count;
    esp_bt_audio_event_call_state_t calls[ESP_BT_AUDIO_CALL_MAX_NUM];
} hfp_ag_ctx_t;

static const char *TAG = "BT_AUD_HFP_AG";
static hfp_ag_ctx_t *hfp_ag_ctx = NULL;
static char c_unknown_call_number[] = "";

static const char *c_connection_state_str[] = {
    "DISCONNECTED",
    "CONNECTING",
    "CONNECTED",
    "SLC_CONNECTED",
    "DISCONNECTING",
};

static const char *c_audio_state_str[] = {
    "DISCONNECTED",
    "CONNECTING",
    "CONNECTED",
    "CONNECTED_MSBC",
};

static void bt_audio_hfp_ag_dispatch_call_state(uint8_t idx, esp_bt_audio_call_dir_t dir,
                                                esp_bt_audio_call_state_t state, const char *uri)
{
    esp_bt_audio_event_call_state_t event_data = {0};
    event_data.tech = ESP_BT_AUDIO_TECH_CLASSIC;
    event_data.idx = idx;
    event_data.dir = dir;
    event_data.state = state;
    if (uri) {
        strncpy(event_data.uri, uri, sizeof(event_data.uri) - 1);
        event_data.uri[sizeof(event_data.uri) - 1] = '\0';
    }
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CALL_STATE_CHG, &event_data);
}

#if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI
static void bt_audio_hfp_ag_release_stream(bt_audio_classic_stream_t **stream)
{
    if (!stream || !*stream) {
        return;
    }

    esp_bt_audio_event_stream_st_t event_data = {0};
    event_data.stream_handle = *stream;
    event_data.state = ESP_BT_AUDIO_STREAM_STATE_STOPPED;
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
    event_data.state = ESP_BT_AUDIO_STREAM_STATE_RELEASED;
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
    bt_audio_classic_stream_destroy(*stream);
    *stream = NULL;
}

static esp_err_t bt_audio_hfp_ag_create_src_stream(void)
{
    if (hfp_ag_ctx->src_stream) {
        return ESP_OK;
    }

    esp_err_t ret = bt_audio_classic_stream_create(&hfp_ag_ctx->src_stream);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create HFP source stream: %s", esp_err_to_name(ret));
        return ret;
    }
    hfp_ag_ctx->src_stream->conn_handle = hfp_ag_ctx->ag_conn;
    hfp_ag_ctx->src_stream->data_ops = &bt_audio_hfp_ag_stream_data_ops;
    hfp_ag_ctx->src_stream->base.context = ESP_BT_AUDIO_STREAM_CONTEXT_CONVERSATIONAL;

    esp_sbc_enc_config_t *sbc_enc_cfg = heap_caps_calloc_prefer(1, sizeof(esp_sbc_enc_config_t), 2,
                                                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(sbc_enc_cfg, ESP_ERR_NO_MEM, fail, TAG, "Failed to allocate sbc_enc_cfg");

    sbc_enc_cfg->sample_rate = 16000;
    sbc_enc_cfg->ch_mode = ESP_SBC_CH_MODE_MONO;
    sbc_enc_cfg->block_length = 15;
    sbc_enc_cfg->sub_bands_num = 8;
    sbc_enc_cfg->allocation_method = ESP_SBC_ALLOC_LOUDNESS;
    sbc_enc_cfg->bitpool = 26;
    sbc_enc_cfg->sbc_mode = ESP_SBC_MODE_MSBC;

    hfp_ag_ctx->src_stream->base.direction = ESP_BT_AUDIO_STREAM_DIR_SOURCE;
    hfp_ag_ctx->src_stream->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_HFP;
    hfp_ag_ctx->src_stream->base.codec_info.bits = 16;
    hfp_ag_ctx->src_stream->base.codec_info.channels = ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT;
    hfp_ag_ctx->src_stream->base.codec_info.sample_rate = 16000;
    hfp_ag_ctx->src_stream->base.codec_info.codec_type = ESP_BT_AUDIO_STREAM_CODEC_SBC;
    hfp_ag_ctx->src_stream->base.codec_info.codec_cfg = sbc_enc_cfg;
    hfp_ag_ctx->src_stream->base.codec_info.cfg_size = sizeof(esp_sbc_enc_config_t);

    esp_bt_audio_event_stream_st_t event_data = {0};
    event_data.stream_handle = hfp_ag_ctx->src_stream;
    event_data.state = ESP_BT_AUDIO_STREAM_STATE_ALLOCATED;
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
    event_data.state = ESP_BT_AUDIO_STREAM_STATE_STARTED;
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
    return ESP_OK;

fail:
    bt_audio_classic_stream_destroy(hfp_ag_ctx->src_stream);
    hfp_ag_ctx->src_stream = NULL;
    return ret;
}

static esp_err_t bt_audio_hfp_ag_create_snk_stream(void)
{
    if (hfp_ag_ctx->snk_stream) {
        return ESP_OK;
    }

    esp_err_t ret = bt_audio_classic_stream_create(&hfp_ag_ctx->snk_stream);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create HFP sink stream: %s", esp_err_to_name(ret));
        return ret;
    }
    hfp_ag_ctx->snk_stream->conn_handle = hfp_ag_ctx->ag_conn;
    hfp_ag_ctx->snk_stream->data_ops = &bt_audio_hfp_ag_stream_data_ops;
    hfp_ag_ctx->snk_stream->base.context = ESP_BT_AUDIO_STREAM_CONTEXT_CONVERSATIONAL;

    esp_sbc_dec_cfg_t *sbc_dec_cfg = heap_caps_calloc_prefer(1, sizeof(esp_sbc_dec_cfg_t), 2,
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(sbc_dec_cfg, ESP_ERR_NO_MEM, fail, TAG, "Failed to allocate sbc_dec_cfg");

    sbc_dec_cfg->sbc_mode = ESP_SBC_MODE_MSBC;
    sbc_dec_cfg->ch_num = 1;
    sbc_dec_cfg->enable_plc = true;

    hfp_ag_ctx->snk_stream->base.direction = ESP_BT_AUDIO_STREAM_DIR_SINK;
    hfp_ag_ctx->snk_stream->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_HFP;
    hfp_ag_ctx->snk_stream->base.codec_info.bits = 16;
    hfp_ag_ctx->snk_stream->base.codec_info.channels = ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT;
    hfp_ag_ctx->snk_stream->base.codec_info.sample_rate = 16000;
    hfp_ag_ctx->snk_stream->base.codec_info.frame_size = 240;
    hfp_ag_ctx->snk_stream->base.codec_info.codec_type = ESP_BT_AUDIO_STREAM_CODEC_SBC;
    hfp_ag_ctx->snk_stream->base.codec_info.codec_cfg = sbc_dec_cfg;
    hfp_ag_ctx->snk_stream->base.codec_info.cfg_size = sizeof(esp_sbc_dec_cfg_t);

    esp_bt_audio_event_stream_st_t event_data = {0};
    event_data.stream_handle = hfp_ag_ctx->snk_stream;
    event_data.state = ESP_BT_AUDIO_STREAM_STATE_ALLOCATED;
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
    event_data.state = ESP_BT_AUDIO_STREAM_STATE_STARTED;
    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
    return ESP_OK;

fail:
    bt_audio_classic_stream_destroy(hfp_ag_ctx->snk_stream);
    hfp_ag_ctx->snk_stream = NULL;
    return ret;
}

static void bt_audio_hfp_ag_msbc_stream_start(void)
{
    if (bt_audio_hfp_ag_create_src_stream() != ESP_OK) {
        return;
    }
    if (bt_audio_hfp_ag_create_snk_stream() != ESP_OK) {
        bt_audio_hfp_ag_release_stream(&hfp_ag_ctx->src_stream);
        bt_audio_hfp_ag_release_stream(&hfp_ag_ctx->snk_stream);
    }
}

static void bt_audio_hfp_ag_msbc_stream_stop(void)
{
    bt_audio_hfp_ag_release_stream(&hfp_ag_ctx->src_stream);

    if (hfp_ag_ctx->snk_stream) {
        esp_bt_audio_stream_packet_t msg = {0};
        if (uxQueueSpacesAvailable(hfp_ag_ctx->snk_stream->base.data_q) == 0) {
            if (xQueueReceive(hfp_ag_ctx->snk_stream->base.data_q, &msg, 0) == pdTRUE) {
                if (msg.data_owner) {
                    esp_hf_ag_audio_buff_free((esp_hf_audio_buff_t *)msg.data_owner);
                }
            }
        }
        msg.data = NULL;
        msg.size = 0;
        msg.bad_frame = false;
        msg.data_owner = NULL;
        msg.is_done = true;
        xQueueSend(hfp_ag_ctx->snk_stream->base.data_q, &msg, 0);
    }
    bt_audio_hfp_ag_release_stream(&hfp_ag_ctx->snk_stream);
}
#endif  /* CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI */

static void bt_audio_hfp_ag_cb(esp_hf_cb_event_t event, esp_hf_cb_param_t *param)
{
    if (!hfp_ag_ctx || !param) {
        return;
    }

    switch (event) {
        case ESP_HF_CONNECTION_STATE_EVT: {
            const char *state_str = (param->conn_stat.state < sizeof(c_connection_state_str) / sizeof(c_connection_state_str[0]))
                                    ? c_connection_state_str[param->conn_stat.state] : "UNKNOWN";
            ESP_LOGI(TAG, "Connection state: %s, peer %02x:%02x:%02x:%02x:%02x:%02x",
                     state_str,
                     param->conn_stat.remote_bda[0], param->conn_stat.remote_bda[1], param->conn_stat.remote_bda[2],
                     param->conn_stat.remote_bda[3], param->conn_stat.remote_bda[4], param->conn_stat.remote_bda[5]);
            if (param->conn_stat.state == ESP_HF_CONNECTION_STATE_SLC_CONNECTED) {
                memcpy(hfp_ag_ctx->peer_bda, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
                hfp_ag_ctx->peer_connected = true;
                esp_bt_audio_event_connection_st_t conn_event = {0};
                conn_event.tech = ESP_BT_AUDIO_TECH_CLASSIC;
                conn_event.connected = true;
                memcpy(conn_event.addr, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG, &conn_event);
            } else if (param->conn_stat.state == ESP_HF_CONNECTION_STATE_DISCONNECTED) {
                esp_bt_audio_event_connection_st_t conn_event = {0};
                conn_event.tech = ESP_BT_AUDIO_TECH_CLASSIC;
                conn_event.connected = false;
                memcpy(conn_event.addr, param->conn_stat.remote_bda, ESP_BD_ADDR_LEN);
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG, &conn_event);
                hfp_ag_ctx->peer_connected = false;
                memset(hfp_ag_ctx->peer_bda, 0, ESP_BD_ADDR_LEN);
            }
            break;
        }

#if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI
        case ESP_HF_AUDIO_STATE_EVT: {
            const char *audio_str = (param->audio_stat.state < sizeof(c_audio_state_str) / sizeof(c_audio_state_str[0]))
                                    ? c_audio_state_str[param->audio_stat.state] : "UNKNOWN";
            ESP_LOGI(TAG, "Audio state: %s", audio_str);
            if (param->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED_MSBC) {
                hfp_ag_ctx->ag_conn = param->audio_stat.sync_conn_handle;
                bt_audio_hfp_ag_msbc_stream_start();
            } else if (param->audio_stat.state == ESP_HF_AUDIO_STATE_DISCONNECTED) {
                hfp_ag_ctx->ag_conn = 0;
                bt_audio_hfp_ag_msbc_stream_stop();
            } else if (param->audio_stat.state == ESP_HF_AUDIO_STATE_CONNECTED) {
                ESP_LOGW(TAG, "CVSD is not supported");
            }
            break;
        }
#endif  /* CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI */

        case ESP_HF_CIND_RESPONSE_EVT:
            esp_hf_ag_cind_response(param->cind_rep.remote_addr, 0, 0, 1, 4, 0, 3, 0);
            break;

        case ESP_HF_COPS_RESPONSE_EVT:
            esp_hf_ag_cops_response(param->cops_rep.remote_addr, "ESP_HFP_AG");
            break;

        case ESP_HF_CLCC_RESPONSE_EVT: {
            esp_hf_ag_clcc_response(param->clcc_rep.remote_addr, 1, 1, ESP_HF_CURRENT_CALL_STATUS_ACTIVE,
                                    0, 0, c_unknown_call_number, ESP_HF_CALL_ADDR_TYPE_UNKNOWN);
            esp_hf_ag_clcc_response(param->clcc_rep.remote_addr, 0, 0, 0, 0, 0, NULL, 0);
            break;
        }

        case ESP_HF_CNUM_RESPONSE_EVT:
            esp_hf_ag_cnum_response(param->cnum_rep.remote_addr, c_unknown_call_number, 129,
                                    ESP_HF_SUBSCRIBER_SERVICE_TYPE_VOICE);
            break;

        case ESP_HF_ATA_RESPONSE_EVT:
            esp_hf_ag_answer_call(param->ata_rep.remote_addr, 1, 0, 1, 0, c_unknown_call_number, 0);
            bt_audio_hfp_ag_dispatch_call_state(1, ESP_BT_AUDIO_CALL_DIR_INCOMING,
                                                ESP_BT_AUDIO_CALL_STATE_ACTIVE, c_unknown_call_number);
            break;

        case ESP_HF_CHUP_RESPONSE_EVT:
            esp_hf_ag_reject_call(param->chup_rep.remote_addr, 0, 0, 0, 0, c_unknown_call_number, 0);
            bt_audio_hfp_ag_dispatch_call_state(1, ESP_BT_AUDIO_CALL_DIR_INCOMING,
                                                ESP_BT_AUDIO_CALL_STATE_INACTIVE, NULL);
            break;

        case ESP_HF_DIAL_EVT:
            if (param->out_call.num_or_loc) {
                esp_hf_ag_cmee_send(param->out_call.remote_addr, ESP_HF_AT_RESPONSE_CODE_OK, 0);
                esp_hf_ag_out_call(param->out_call.remote_addr, 1, 0, 1, 0,
                                   param->out_call.num_or_loc, 0);
                bt_audio_hfp_ag_dispatch_call_state(1, ESP_BT_AUDIO_CALL_DIR_OUTGOING,
                                                    ESP_BT_AUDIO_CALL_STATE_DIALING, param->out_call.num_or_loc);
            }
            break;

        case ESP_HF_VOLUME_CONTROL_EVT: {
            uint8_t app_vol = (uint8_t)((param->volume_control.volume * 100) / 15);
            esp_bt_audio_event_vol_absolute_t vol_event = {0};
            vol_event.context = ESP_BT_AUDIO_STREAM_CONTEXT_CONVERSATIONAL;
            vol_event.vol = app_vol;
            vol_event.mute = false;
            bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_VOL_ABSOLUTE, &vol_event);
            break;
        }

        case ESP_HF_PROF_STATE_EVT:
            ESP_LOGI(TAG, "AG profile state: %d", param->prof_stat.state);
            break;

        default:
            ESP_LOGD(TAG, "Unhandled AG event: %d", event);
            break;
    }
}

#if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI
static void bt_audio_hfp_ag_audio_data_cb(esp_hf_sync_conn_hdl_t sync_conn_hdl, esp_hf_audio_buff_t *audio_buf, bool is_bad_frame)
{
    (void)sync_conn_hdl;
    bt_audio_classic_stream_t *stream = hfp_ag_ctx ? hfp_ag_ctx->snk_stream : NULL;
    if (!stream) {
        esp_hf_ag_audio_buff_free(audio_buf);
        return;
    }

    esp_bt_audio_stream_packet_t msg = {0};
    if (uxQueueSpacesAvailable(stream->base.data_q) == 0) {
        if (xQueueReceive(stream->base.data_q, &msg, 0) == pdTRUE) {
            if (msg.data_owner) {
                esp_hf_ag_audio_buff_free((esp_hf_audio_buff_t *)msg.data_owner);
            }
        }
    }
    msg.data = audio_buf->data;
    msg.size = 57;
    msg.bad_frame = is_bad_frame;
    msg.data_owner = audio_buf;
    msg.is_done = false;
    if (xQueueSend(stream->base.data_q, &msg, 0) != pdTRUE) {
        esp_hf_ag_audio_buff_free(audio_buf);
    }
}
#endif  /* CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI */

static esp_err_t hfp_ag_connect(uint8_t *bda)
{
    return esp_hf_ag_slc_connect(bda);
}

static esp_err_t hfp_ag_disconnect(uint8_t *bda)
{
    return esp_hf_ag_slc_disconnect(bda);
}

static esp_err_t hfp_ag_answer_call(uint8_t idx)
{
    if (!hfp_ag_ctx || !hfp_ag_ctx->peer_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = esp_hf_ag_answer_call(hfp_ag_ctx->peer_bda, 1, 0, 1, 0, c_unknown_call_number, 0);
    if (ret == ESP_OK) {
        bt_audio_hfp_ag_dispatch_call_state(idx, ESP_BT_AUDIO_CALL_DIR_INCOMING,
                                            ESP_BT_AUDIO_CALL_STATE_ACTIVE, c_unknown_call_number);
    } else {
        ESP_LOGW(TAG, "Failed to answer call: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t hfp_ag_reject_call(uint8_t idx)
{
    if (!hfp_ag_ctx || !hfp_ag_ctx->peer_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t ret = esp_hf_ag_reject_call(hfp_ag_ctx->peer_bda, 0, 0, 0, 0, c_unknown_call_number, 0);
    if (ret == ESP_OK) {
        bt_audio_hfp_ag_dispatch_call_state(idx, ESP_BT_AUDIO_CALL_DIR_INCOMING,
                                            ESP_BT_AUDIO_CALL_STATE_INACTIVE, NULL);
    } else {
        ESP_LOGW(TAG, "Failed to reject call: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t hfp_ag_dial(const char *number)
{
    if (!hfp_ag_ctx || !hfp_ag_ctx->peer_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!number || number[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    const char *dial_number = number;
    esp_err_t ret = esp_hf_ag_out_call(hfp_ag_ctx->peer_bda, 1, 0, 1, 0, (char *)dial_number, 0);
    if (ret == ESP_OK) {
        bt_audio_hfp_ag_dispatch_call_state(1, ESP_BT_AUDIO_CALL_DIR_OUTGOING,
                                            ESP_BT_AUDIO_CALL_STATE_DIALING, dial_number);
    } else {
        ESP_LOGW(TAG, "Failed to dial %s: %s", dial_number, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t bt_audio_hfp_ag_init(void)
{
    if (hfp_ag_ctx) {
        return ESP_ERR_INVALID_STATE;
    }

    hfp_ag_ctx = heap_caps_calloc_prefer(1, sizeof(hfp_ag_ctx_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(hfp_ag_ctx, ESP_ERR_NO_MEM, TAG, "Failed to allocate hfp_ag_ctx");

    ESP_ERROR_CHECK(esp_hf_ag_init());
    ESP_ERROR_CHECK(esp_hf_ag_register_callback(bt_audio_hfp_ag_cb));
#if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI
    ESP_ERROR_CHECK(esp_hf_ag_register_audio_data_callback(bt_audio_hfp_ag_audio_data_cb));
#endif  /* CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI */

    esp_bt_audio_classic_ops_t classic_ops = {0};
    bt_audio_ops_get_classic(&classic_ops);
    classic_ops.hfp_ag_connect = hfp_ag_connect;
    classic_ops.hfp_ag_disconnect = hfp_ag_disconnect;
    ESP_ERROR_CHECK(bt_audio_ops_set_classic(&classic_ops));

    esp_bt_audio_call_ops_t call_ops = {0};
    bt_audio_ops_get_call(&call_ops);
    call_ops.answer_call = hfp_ag_answer_call;
    call_ops.reject_call = hfp_ag_reject_call;
    call_ops.dial = hfp_ag_dial;
    ESP_ERROR_CHECK(bt_audio_ops_set_call(&call_ops));

    ESP_LOGI(TAG, "AG init success");
    return ESP_OK;
}

esp_err_t bt_audio_hfp_ag_deinit(void)
{
    if (!hfp_ag_ctx) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_bt_audio_call_ops_t call_ops = {0};
    bt_audio_ops_get_call(&call_ops);
    call_ops.answer_call = NULL;
    call_ops.reject_call = NULL;
    call_ops.dial = NULL;
    ESP_ERROR_CHECK(bt_audio_ops_set_call(&call_ops));

    esp_bt_audio_classic_ops_t classic_ops = {0};
    bt_audio_ops_get_classic(&classic_ops);
    classic_ops.hfp_ag_connect = NULL;
    classic_ops.hfp_ag_disconnect = NULL;
    ESP_ERROR_CHECK(bt_audio_ops_set_classic(&classic_ops));

#if CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI
    bt_audio_hfp_ag_msbc_stream_stop();
#endif  /* CONFIG_BT_HFP_AUDIO_DATA_PATH_HCI */

    free(hfp_ag_ctx);
    hfp_ag_ctx = NULL;
    esp_hf_ag_deinit();
    ESP_LOGI(TAG, "AG deinit success");
    return ESP_OK;
}
