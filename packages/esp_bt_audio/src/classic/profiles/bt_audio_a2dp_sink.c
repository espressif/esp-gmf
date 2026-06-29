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
#include "freertos/projdefs.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

#include "esp_sbc_dec.h"
#include "esp_sbc_def.h"
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
#include "esp_aac_dec.h"
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */

#include "esp_a2dp_api.h"
#include "esp_gap_bt_api.h"

#include "esp_bt_audio_defs.h"
#include "bt_audio_classic_stream.h"
#include "bt_audio_ops.h"
#include "bt_audio_evt_dispatcher.h"

#define A2DP_SINK_DELAY_VALUE  (50)
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
#define A2DP_AAC_SAMPLES_PER_FRAME  (1024)
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */

static const char *TAG = "BT_AUD_A2D_SINK";
static const char *s_a2d_conn_state_str[]  = {"Disconnected", "Connecting", "Connected", "Disconnecting"};
static const char *s_a2d_audio_state_str[] = {"Suspended", "Started"};

typedef struct {
    esp_a2d_conn_hdl_t                conn_hdl;   /*!< A2DP connection handle */
    bt_audio_classic_stream_t        *stream;     /*!< Classic stream instance; created on STARTED */
    esp_bt_audio_stream_codec_info_t  a2d_codec;  /*!< Active/negotiated A2DP codec info */
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
    esp_a2d_cie_m24_t                 m24;        /*!< Negotiated AAC codec capabilities/config */
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */
} a2dp_sink_ctx_t;

static a2dp_sink_ctx_t *a2dp_sink = NULL;

static inline int get_sample_rate_from_sbc_info(esp_a2d_cie_sbc_t *sbc_info)
{
    int sample_rate = 16000;
    if (sbc_info->samp_freq & 0x04) {
        sample_rate = 32000;
    } else if (sbc_info->samp_freq & 0x02) {
        sample_rate = 44100;
    } else if (sbc_info->samp_freq & 0x01) {
        sample_rate = 48000;
    }
    return sample_rate;
}

static inline int get_block_len_from_sbc_info(esp_a2d_cie_sbc_t *sbc_info)
{
    int block_len = 16;
    if (sbc_info->block_len & 0x08) {
        block_len = 4;
    } else if (sbc_info->block_len & 0x04) {
        block_len = 8;
    } else if (sbc_info->block_len & 0x02) {
        block_len = 12;
    } else if (sbc_info->block_len & 0x01) {
        block_len = 16;
    }
    return block_len;
}

static inline int get_subbands_from_sbc_info(esp_a2d_cie_sbc_t *sbc_info)
{
    int subbands = 8;
    if (sbc_info->num_subbands & 0x02) {
        subbands = 4;
    } else if (sbc_info->num_subbands & 0x01) {
        subbands = 8;
    }
    return subbands;
}

static inline bool a2dp_sbc_info_to_codec_info(esp_a2d_cie_sbc_t *sbc_info, esp_bt_audio_stream_codec_info_t *codec_info)
{
    if (sbc_info == NULL || codec_info == NULL) {
        return false;
    }
    int block_len = get_block_len_from_sbc_info(sbc_info);
    int sub_bands_num = get_subbands_from_sbc_info(sbc_info);

    codec_info->codec_type = ESP_BT_AUDIO_STREAM_CODEC_SBC;
    codec_info->bits = 16;
    codec_info->channels = sbc_info->ch_mode == 0x08 ? ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT : ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT | ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT;
    codec_info->sample_rate = get_sample_rate_from_sbc_info(sbc_info);
    codec_info->frame_size = block_len * sub_bands_num * __builtin_popcount(codec_info->channels) * 2;

    ESP_LOGI(TAG, "Codec info parameters:");
    ESP_LOGI(TAG, "  sample_rate: %d", codec_info->sample_rate);
    ESP_LOGI(TAG, "  channels: 0x%x (%d ch)", codec_info->channels, __builtin_popcount(codec_info->channels));
    ESP_LOGI(TAG, "  frame_size: %d", codec_info->frame_size);

    return true;
}

static inline void *a2dp_sink_create_sbc_dec_cfg(const esp_bt_audio_stream_codec_info_t *codec_info, uint32_t *cfg_size)
{
    esp_sbc_dec_cfg_t *sbc_dec_cfg = heap_caps_calloc(1, sizeof(esp_sbc_dec_cfg_t), MALLOC_CAP_8BIT);
    if (sbc_dec_cfg == NULL) {
        ESP_LOGW(TAG, "Failed to allocate SBC decoder configuration");
        return NULL;
    }

    sbc_dec_cfg->sbc_mode = ESP_SBC_MODE_STD;
    sbc_dec_cfg->ch_num = __builtin_popcount(codec_info->channels);
    sbc_dec_cfg->enable_plc = true;
    *cfg_size = sizeof(esp_sbc_dec_cfg_t);

    ESP_LOGI(TAG, "SBC decoder configuration:");
    ESP_LOGI(TAG, "  sbc_mode: %d", sbc_dec_cfg->sbc_mode);
    ESP_LOGI(TAG, "  ch_num: %d", sbc_dec_cfg->ch_num);
    ESP_LOGI(TAG, "  enable_plc: %d", sbc_dec_cfg->enable_plc);
    return sbc_dec_cfg;
}

static inline void a2dp_sink_handle_sbc_cfg(esp_a2d_cie_sbc_t *sbc_info)
{
    ESP_LOGI(TAG, "Configure audio player:");
    ESP_LOGI(TAG, "  sample rate : %d", sbc_info->samp_freq);
    ESP_LOGI(TAG, "  chan mode   : %d", sbc_info->ch_mode);
    ESP_LOGI(TAG, "  block len   : %d", sbc_info->block_len);
    ESP_LOGI(TAG, "  subbands    : %d", sbc_info->num_subbands);
    ESP_LOGI(TAG, "  alloc method: %d", sbc_info->alloc_mthd);
    ESP_LOGI(TAG, "  min bitpool : %d", sbc_info->min_bitpool);
    ESP_LOGI(TAG, "  max bitpool : %d", sbc_info->max_bitpool);

    if (!a2dp_sbc_info_to_codec_info(sbc_info, &a2dp_sink->a2d_codec)) {
        ESP_LOGE(TAG, "Failed to convert A2DP SBC config to codec info");
    }
}

static inline esp_err_t a2dp_sink_register_sbc_sep(uint8_t seid)
{
    esp_a2d_mcc_t mcc_sbc = {0};
    mcc_sbc.type = ESP_A2D_MCT_SBC;
    mcc_sbc.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_16K |
                                     ESP_A2D_SBC_CIE_SF_32K |
                                     ESP_A2D_SBC_CIE_SF_44K |
                                     ESP_A2D_SBC_CIE_SF_48K;
    mcc_sbc.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_MONO |
                                   ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL |
                                   ESP_A2D_SBC_CIE_CH_MODE_STEREO |
                                   ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
    mcc_sbc.cie.sbc_info.block_len = ESP_A2D_SBC_CIE_BLOCK_LEN_4 |
                                     ESP_A2D_SBC_CIE_BLOCK_LEN_8 |
                                     ESP_A2D_SBC_CIE_BLOCK_LEN_12 |
                                     ESP_A2D_SBC_CIE_BLOCK_LEN_16;
    mcc_sbc.cie.sbc_info.num_subbands = ESP_A2D_SBC_CIE_NUM_SUBBANDS_4 |
                                        ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
    mcc_sbc.cie.sbc_info.alloc_mthd = ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR |
                                      ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
    mcc_sbc.cie.sbc_info.max_bitpool = 250;
    mcc_sbc.cie.sbc_info.min_bitpool = 2;
    return esp_a2d_sink_register_stream_endpoint(seid, &mcc_sbc);
}

#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
static inline int get_sample_rate_from_m24_info(esp_a2d_cie_m24_t *m24_info)
{
    if (m24_info->samp_freq2 & ESP_A2D_M24_CIE_SF2_96K) {
        return 96000;
    } else if (m24_info->samp_freq2 & ESP_A2D_M24_CIE_SF2_88K) {
        return 88200;
    } else if (m24_info->samp_freq2 & ESP_A2D_M24_CIE_SF2_64K) {
        return 64000;
    } else if (m24_info->samp_freq2 & ESP_A2D_M24_CIE_SF2_48K) {
        return 48000;
    } else if (m24_info->samp_freq1 & ESP_A2D_M24_CIE_SF1_44K) {
        return 44100;
    } else if (m24_info->samp_freq1 & ESP_A2D_M24_CIE_SF1_32K) {
        return 32000;
    } else if (m24_info->samp_freq1 & ESP_A2D_M24_CIE_SF1_24K) {
        return 24000;
    } else if (m24_info->samp_freq1 & ESP_A2D_M24_CIE_SF1_22K) {
        return 22050;
    } else if (m24_info->samp_freq1 & ESP_A2D_M24_CIE_SF1_16K) {
        return 16000;
    } else if (m24_info->samp_freq1 & ESP_A2D_M24_CIE_SF1_12K) {
        return 12000;
    } else if (m24_info->samp_freq1 & ESP_A2D_M24_CIE_SF1_11K) {
        return 11025;
    } else if (m24_info->samp_freq1 & ESP_A2D_M24_CIE_SF1_8K) {
        return 8000;
    }
    return 44100;
}

static inline int get_channel_count_from_m24_info(esp_a2d_cie_m24_t *m24_info)
{
    return (m24_info->ch & ESP_A2D_M24_CIE_CH_1) ? 1 : 2;
}

static inline bool a2dp_m24_info_to_codec_info(esp_a2d_cie_m24_t *m24_info, esp_bt_audio_stream_codec_info_t *codec_info)
{
    if (m24_info == NULL || codec_info == NULL) {
        return false;
    }

    uint32_t channels = get_channel_count_from_m24_info(m24_info) == 1 ? ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT : (ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT | ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT);
    codec_info->codec_type = ESP_BT_AUDIO_STREAM_CODEC_AAC;
    codec_info->bits = 16;
    codec_info->channels = channels;
    codec_info->sample_rate = get_sample_rate_from_m24_info(m24_info);
    codec_info->frame_size = A2DP_AAC_SAMPLES_PER_FRAME * __builtin_popcount(channels) * (codec_info->bits / 8);

    ESP_LOGD(TAG, "AAC codec info parameters:");
    ESP_LOGD(TAG, "  sample_rate: %d", codec_info->sample_rate);
    ESP_LOGD(TAG, "  channels: 0x%x (%d ch)", codec_info->channels, __builtin_popcount(codec_info->channels));
    ESP_LOGD(TAG, "  frame_size: %d", codec_info->frame_size);

    return true;
}

static inline void *a2dp_sink_create_aac_dec_cfg(const esp_bt_audio_stream_codec_info_t *codec_info, uint32_t *cfg_size)
{
    esp_aac_dec_cfg_t *aac_dec_cfg = heap_caps_calloc(1, sizeof(esp_aac_dec_cfg_t), MALLOC_CAP_8BIT);
    if (aac_dec_cfg == NULL) {
        ESP_LOGW(TAG, "Failed to allocate AAC decoder configuration");
        return NULL;
    }

    esp_aac_dec_cfg_t default_aac_dec_cfg = ESP_AAC_DEC_CONFIG_DEFAULT();
    *aac_dec_cfg = default_aac_dec_cfg;
    aac_dec_cfg->sample_rate = codec_info->sample_rate;
    aac_dec_cfg->channel = __builtin_popcount(codec_info->channels);
    aac_dec_cfg->bits_per_sample = codec_info->bits;
    aac_dec_cfg->no_adts_header = true;
    aac_dec_cfg->aac_plus_enable = (a2dp_sink->m24.obj_type & (ESP_A2D_M24_CIE_OBJ_TYPE_4_HE_AAC | ESP_A2D_M24_CIE_OBJ_TYPE_4_HE_AAC_V2)) != 0;
    *cfg_size = sizeof(esp_aac_dec_cfg_t);

    ESP_LOGD(TAG, "AAC decoder configuration:");
    ESP_LOGD(TAG, "  sample_rate: %d", aac_dec_cfg->sample_rate);
    ESP_LOGD(TAG, "  channel: %d", aac_dec_cfg->channel);
    ESP_LOGD(TAG, "  bits_per_sample: %d", aac_dec_cfg->bits_per_sample);
    ESP_LOGD(TAG, "  no_adts_header: %d", aac_dec_cfg->no_adts_header);
    ESP_LOGD(TAG, "  aac_plus_enable: %d", aac_dec_cfg->aac_plus_enable);
    return aac_dec_cfg;
}

static inline void a2dp_sink_handle_aac_cfg(esp_a2d_cie_m24_t *m24_info)
{
    ESP_LOGD(TAG, "Configure AAC audio player:");
    ESP_LOGD(TAG, "  drc        : 0x%x", m24_info->drc);
    ESP_LOGD(TAG, "  obj_type   : 0x%x", m24_info->obj_type);
    ESP_LOGD(TAG, "  samp_freq1 : 0x%x", m24_info->samp_freq1);
    ESP_LOGD(TAG, "  samp_freq2 : 0x%x", m24_info->samp_freq2);
    ESP_LOGD(TAG, "  channels   : 0x%x", m24_info->ch);
    ESP_LOGD(TAG, "  vbr        : 0x%x", m24_info->vbr);
    ESP_LOGD(TAG, "  bitrate    : 0x%02x%02x%02x", m24_info->br1, m24_info->br2, m24_info->br3);

    if (!a2dp_m24_info_to_codec_info(m24_info, &a2dp_sink->a2d_codec)) {
        ESP_LOGE(TAG, "Failed to convert A2DP AAC config to codec info");
    }
    memcpy(&a2dp_sink->m24, m24_info, sizeof(esp_a2d_cie_m24_t));
}

static inline esp_err_t a2dp_sink_register_aac_sep(uint8_t seid)
{
    esp_a2d_mcc_t mcc_aac = {0};
    mcc_aac.type = ESP_A2D_MCT_M24;
    mcc_aac.cie.m24_info.drc = ESP_A2D_M24_CIE_DRC_NS;
    mcc_aac.cie.m24_info.obj_type = ESP_A2D_M24_CIE_OBJ_TYPE_2_AAC_LC |
                                    ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LC |
                                    ESP_A2D_M24_CIE_OBJ_TYPE_4_HE_AAC |
                                    ESP_A2D_M24_CIE_OBJ_TYPE_4_HE_AAC_V2;
    mcc_aac.cie.m24_info.samp_freq1 = ESP_A2D_M24_CIE_SF1_8K |
                                      ESP_A2D_M24_CIE_SF1_11K |
                                      ESP_A2D_M24_CIE_SF1_12K |
                                      ESP_A2D_M24_CIE_SF1_16K |
                                      ESP_A2D_M24_CIE_SF1_22K |
                                      ESP_A2D_M24_CIE_SF1_24K |
                                      ESP_A2D_M24_CIE_SF1_32K |
                                      ESP_A2D_M24_CIE_SF1_44K;
    mcc_aac.cie.m24_info.samp_freq2 = ESP_A2D_M24_CIE_SF2_48K |
                                      ESP_A2D_M24_CIE_SF2_64K |
                                      ESP_A2D_M24_CIE_SF2_88K |
                                      ESP_A2D_M24_CIE_SF2_96K;
    mcc_aac.cie.m24_info.ch = ESP_A2D_M24_CIE_CH_1 |
                              ESP_A2D_M24_CIE_CH_2;
    mcc_aac.cie.m24_info.vbr = ESP_A2D_M24_CIE_VBR_SUPPORT;
    mcc_aac.cie.m24_info.br1 = 0x7F & ESP_A2D_M24_CIE_BR1_MSK;
    mcc_aac.cie.m24_info.br2 = 0xFF & ESP_A2D_M24_CIE_BR2_MSK;
    mcc_aac.cie.m24_info.br3 = 0xFF & ESP_A2D_M24_CIE_BR3_MSK;
    return esp_a2d_sink_register_stream_endpoint(seid, &mcc_aac);
}
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */

static bool a2dp_sink_prepare_stream_codec_cfg(bt_audio_classic_stream_t *stream)
{
    uint32_t cfg_size = 0;
    void *codec_cfg = NULL;

    switch (stream->base.codec_info.codec_type) {
        case ESP_BT_AUDIO_STREAM_CODEC_SBC:
            codec_cfg = a2dp_sink_create_sbc_dec_cfg(&stream->base.codec_info, &cfg_size);
            break;
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
        case ESP_BT_AUDIO_STREAM_CODEC_AAC:
            codec_cfg = a2dp_sink_create_aac_dec_cfg(&stream->base.codec_info, &cfg_size);
            break;
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */
        default:
            ESP_LOGE(TAG, "Unsupported sink codec type: %d", stream->base.codec_info.codec_type);
            break;
    }

    if (codec_cfg == NULL) {
        return false;
    }
    stream->base.codec_info.codec_cfg = codec_cfg;
    stream->base.codec_info.cfg_size = cfg_size;
    return true;
}

static void a2dp_sink_handle_audio_cfg(esp_a2d_mcc_t *mcc)
{
    ESP_LOGI(TAG, "A2DP audio stream configuration, codec type: %d", mcc->type);
    switch (mcc->type) {
        case ESP_A2D_MCT_SBC:
            a2dp_sink_handle_sbc_cfg(&mcc->cie.sbc_info);
            break;
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
        case ESP_A2D_MCT_M24:
            a2dp_sink_handle_aac_cfg(&mcc->cie.m24_info);
            break;
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */
        default:
            ESP_LOGE(TAG, "Unsupported codec type: %d", mcc->type);
            break;
    }
}

static void bt_a2d_sink_data_cb(esp_a2d_conn_hdl_t conn_hdl, esp_a2d_audio_buff_t *audio_buf)
{
    (void)conn_hdl;
    if (audio_buf == NULL) {
        return;
    }

    if (!a2dp_sink || !a2dp_sink->stream) {
        ESP_LOGE(TAG, "No sink stream found");
        esp_a2d_audio_buff_free(audio_buf);
        return;
    }
    ESP_LOGD(TAG, "a2dp recv %d", audio_buf->data_len);
    esp_bt_audio_stream_packet_t msg = {0};
    if (uxQueueSpacesAvailable(a2dp_sink->stream->base.data_q) == 0) {
        if (xQueueReceive(a2dp_sink->stream->base.data_q, &msg, 0) == pdTRUE) {
            if (msg.data_owner) {
                esp_a2d_audio_buff_free((esp_a2d_audio_buff_t *)msg.data_owner);
            }
        }
    }
    msg.data = audio_buf->data;
    msg.size = audio_buf->data_len;
    msg.data_owner = audio_buf;
    msg.bad_frame = false;
    msg.is_done = false;
    if (xQueueSend(a2dp_sink->stream->base.data_q, &msg, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send audio buffer to queue");
        esp_a2d_audio_buff_free(audio_buf);
    }
}

static void bt_a2d_event_cb(esp_a2d_cb_event_t event, esp_a2d_cb_param_t *p_param)
{
    ESP_LOGD(TAG, "%s event: %d", __func__, event);

    esp_a2d_cb_param_t *a2d = NULL;

    switch (event) {
        case ESP_A2D_CONNECTION_STATE_EVT: {
            a2d = (esp_a2d_cb_param_t *)(p_param);
            uint8_t *bda = a2d->conn_stat.remote_bda;
            ESP_LOGI(TAG, "A2DP connection state: %s, addr[%02x:%02x:%02x:%02x:%02x:%02x]",
                     s_a2d_conn_state_str[a2d->conn_stat.state], bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
            esp_bt_audio_event_connection_st_t event_data = {0};
            event_data.tech = ESP_BT_AUDIO_TECH_CLASSIC;
            switch (a2d->conn_stat.state) {
                case ESP_A2D_CONNECTION_STATE_CONNECTED:
                    a2dp_sink->conn_hdl = a2d->conn_stat.conn_hdl;
                    ESP_LOGI(TAG, "A2DP connection handle saved: %d", a2dp_sink->conn_hdl);
                    event_data.connected = true;
                    memcpy(&event_data.addr, bda, ESP_BD_ADDR_LEN);
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG, &event_data);
                    break;
                case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
                    a2dp_sink->conn_hdl = 0;
                    memset(&a2dp_sink->a2d_codec, 0, sizeof(esp_bt_audio_stream_codec_info_t));
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
                    memset(&a2dp_sink->m24, 0, sizeof(esp_a2d_cie_m24_t));
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */
                    ESP_LOGI(TAG, "A2DP connection handle cleared");
                    event_data.connected = false;
                    memcpy(&event_data.addr, bda, ESP_BD_ADDR_LEN);
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG, &event_data);
                    break;
                default:
                    break;
            }
            break;
        }
        case ESP_A2D_AUDIO_STATE_EVT: {
            a2d = (esp_a2d_cb_param_t *)(p_param);
            ESP_LOGI(TAG, "A2DP audio state: %s (sink)", s_a2d_audio_state_str[a2d->audio_stat.state]);
            if (!a2dp_sink->stream && ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state) {
                esp_err_t ret = bt_audio_classic_stream_create(&a2dp_sink->stream);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to create A2DP stream: %s", esp_err_to_name(ret));
                    break;
                }
                a2dp_sink->stream->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP;
                a2dp_sink->stream->base.context = ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA;
                a2dp_sink->stream->conn_handle = a2dp_sink->conn_hdl;
            }

            if (!a2dp_sink->stream) {
                break;
            }

            esp_bt_audio_event_stream_st_t event_data = {0};
            event_data.stream_handle = a2dp_sink->stream;
            a2dp_sink->stream->base.direction = ESP_BT_AUDIO_STREAM_DIR_SINK;
            if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state) {
                if (a2dp_sink->a2d_codec.codec_type == ESP_BT_AUDIO_STREAM_CODEC_UNKNOWN) {
                    ESP_LOGE(TAG, "No negotiated A2DP codec configuration available for sink");
                    break;
                }
                memcpy(&a2dp_sink->stream->base.codec_info, &a2dp_sink->a2d_codec, sizeof(esp_bt_audio_stream_codec_info_t));

                if (!a2dp_sink_prepare_stream_codec_cfg(a2dp_sink->stream)) {
                    ESP_LOGE(TAG, "Failed to prepare sink codec configuration");
                    break;
                }

                event_data.state = ESP_BT_AUDIO_STREAM_STATE_ALLOCATED;
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                event_data.state = ESP_BT_AUDIO_STREAM_STATE_STARTED;
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
            } else if (ESP_A2D_AUDIO_STATE_SUSPEND == a2d->audio_stat.state) {
                if (a2dp_sink->stream) {
                    esp_bt_audio_stream_packet_t msg = {0};
                    if (uxQueueSpacesAvailable(a2dp_sink->stream->base.data_q) == 0) {
                        if (xQueueReceive(a2dp_sink->stream->base.data_q, &msg, 0) == pdTRUE) {
                            if (msg.data_owner) {
                                esp_a2d_audio_buff_free(msg.data_owner);
                            }
                        }
                    }
                    msg.data = NULL;
                    msg.size = 0;
                    msg.bad_frame = false;
                    msg.data_owner = NULL;
                    msg.is_done = true;
                    if (xQueueSend(a2dp_sink->stream->base.data_q, &msg, 0) != pdTRUE) {
                        ESP_LOGE(TAG, "A2DP sink failed to send done packet to queue");
                    }
                    event_data.state = ESP_BT_AUDIO_STREAM_STATE_STOPPED;
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                    event_data.state = ESP_BT_AUDIO_STREAM_STATE_RELEASED;
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                    bt_audio_classic_stream_destroy(a2dp_sink->stream);
                    a2dp_sink->stream = NULL;
                }
            }
            break;
        }
        case ESP_A2D_AUDIO_CFG_EVT: {
            a2d = (esp_a2d_cb_param_t *)(p_param);
            a2dp_sink_handle_audio_cfg(&a2d->audio_cfg.mcc);
            break;
        }
        case ESP_A2D_PROF_STATE_EVT: {
            a2d = (esp_a2d_cb_param_t *)(p_param);
            if (ESP_A2D_INIT_SUCCESS == a2d->a2d_prof_stat.init_state) {
                ESP_LOGI(TAG, "A2DP PROF STATE: Init Complete");
            } else {
                ESP_LOGI(TAG, "A2DP PROF STATE: Deinit Complete");
            }
            break;
        }
        case ESP_A2D_SNK_PSC_CFG_EVT: {
            a2d = (esp_a2d_cb_param_t *)(p_param);
            ESP_LOGI(TAG, "protocol service capabilities configured: 0x%x ", a2d->a2d_psc_cfg_stat.psc_mask);
            if (a2d->a2d_psc_cfg_stat.psc_mask & ESP_A2D_PSC_DELAY_RPT) {
                ESP_LOGI(TAG, "Peer device support delay reporting");
            } else {
                ESP_LOGI(TAG, "Peer device unsupported delay reporting");
            }
            break;
        }
        case ESP_A2D_SNK_SET_DELAY_VALUE_EVT: {
            a2d = (esp_a2d_cb_param_t *)(p_param);
            if (ESP_A2D_SET_INVALID_PARAMS == a2d->a2d_set_delay_value_stat.set_state) {
                ESP_LOGI(TAG, "Set delay report value: fail");
            } else {
                ESP_LOGI(TAG, "Set delay report value: success, delay_value: %u * 1/10 ms", a2d->a2d_set_delay_value_stat.delay_value);
            }
            break;
        }
        case ESP_A2D_SNK_GET_DELAY_VALUE_EVT: {
            a2d = (esp_a2d_cb_param_t *)(p_param);
            ESP_LOGI(TAG, "Get delay report value: delay_value: %u * 1/10 ms", a2d->a2d_get_delay_value_stat.delay_value);
            esp_a2d_sink_set_delay_value(a2d->a2d_get_delay_value_stat.delay_value + A2DP_SINK_DELAY_VALUE);
            break;
        }
        default:
            ESP_LOGI(TAG, "%s unhandled event: %d", __func__, event);
            break;
    }
}

static esp_err_t a2dp_sink_connect(uint8_t *bda)
{
    if (!bda) {
        ESP_LOGE(TAG, "Invalid BDA for connection");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "A2DP sink: Connecting to peer: %02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    esp_err_t ret = esp_a2d_sink_connect(bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to A2DP sink: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t a2dp_sink_disconnect(uint8_t *bda)
{
    if (!bda) {
        ESP_LOGE(TAG, "Invalid BDA for disconnection");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "A2DP sink: Disconnecting from peer: %02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    esp_err_t ret = esp_a2d_sink_disconnect(bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect from A2DP sink: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t bt_audio_a2dp_sink_init()
{
    if (a2dp_sink) {
        ESP_LOGW(TAG, "A2DP sink already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    a2dp_sink = heap_caps_calloc_prefer(1, sizeof(a2dp_sink_ctx_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(a2dp_sink, ESP_ERR_NO_MEM, TAG, "Failed to malloc space for a2dp_sink");

    ESP_ERROR_CHECK(esp_a2d_sink_init());
    ESP_ERROR_CHECK(esp_a2d_register_callback(&bt_a2d_event_cb));
    ESP_ERROR_CHECK(esp_a2d_sink_register_audio_data_callback(bt_a2d_sink_data_cb));
    uint8_t seid = 0;
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
    ESP_ERROR_CHECK(a2dp_sink_register_aac_sep(seid++));
#else
    ESP_LOGW(TAG, "A2DP AAC codec is disabled. Enable CONFIG_BT_A2DP_CODEC_AAC_ENABLED to advertise AAC");
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */

    ESP_ERROR_CHECK(a2dp_sink_register_sbc_sep(seid));

    esp_bt_audio_classic_ops_t a2dp_sink_ops = {0};
    bt_audio_ops_get_classic(&a2dp_sink_ops);
    a2dp_sink_ops.a2d_sink_connect = a2dp_sink_connect;
    a2dp_sink_ops.a2d_sink_disconnect = a2dp_sink_disconnect;
    ESP_ERROR_CHECK(bt_audio_ops_set_classic(&a2dp_sink_ops));

    ESP_LOGI(TAG, "A2DP sink: initialized");
    return ESP_OK;
}

esp_err_t bt_audio_a2dp_sink_deinit()
{
    if (!a2dp_sink) {
        return ESP_ERR_INVALID_STATE;
    }

    if (a2dp_sink->stream) {
        bt_audio_classic_stream_destroy(a2dp_sink->stream);
        a2dp_sink->stream = NULL;
    }

    esp_a2d_sink_deinit();
    esp_bt_gap_set_scan_mode(ESP_BT_NON_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);

    free(a2dp_sink);
    a2dp_sink = NULL;

    ESP_LOGI(TAG, "A2DP sink: deinitialized");
    return ESP_OK;
}
