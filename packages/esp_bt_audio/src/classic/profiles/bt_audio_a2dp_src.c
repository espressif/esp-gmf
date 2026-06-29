/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/projdefs.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "hal/efuse_hal.h"

#include "esp_sbc_enc.h"
#include "esp_sbc_def.h"
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
#include "esp_aac_enc.h"
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */

#include "esp_a2dp_api.h"
#include "esp_gap_bt_api.h"

#include "esp_bt_audio_defs.h"
#include "bt_audio_classic_stream.h"
#include "bt_audio_ops.h"
#include "bt_audio_evt_dispatcher.h"

#define A2DP_SRC_EVT_TIMER_EXPIRED  (BIT0)
#define A2DP_SRC_EVT_EXITING        (BIT1)
#define A2DP_SRC_EVT_EXITED         (BIT2)

#define A2DP_SRC_BITPOOL_MONO_DEFAULT    (32)
#define A2DP_SRC_BITPOOL_STEREO_DEFAULT  (53)
#define A2DP_SRC_MTU_OVERHEAD_BYTES      (50)
#define A2DP_SRC_SEND_INTERVAL_MS        (20)
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
#define A2DP_SRC_AAC_SAMPLES_PER_FRAME   (1024)
#define A2DP_SRC_AAC_DEFAULT_BITRATE     (96000)
#define A2DP_SRC_AAC_MTU_MARGIN_BYTES    (32)
#define A2DP_SRC_AAC_MTU_SAFETY_PERCENT  (80)
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */

typedef enum {
    A2DP_MEDIA_STATE_IDLE     = 0,
    A2DP_MEDIA_STATE_STARTING = 1,
    A2DP_MEDIA_STATE_STARTED  = 2,
    A2DP_MEDIA_STATE_STOPPING = 3,
    A2DP_MEDIA_STATE_ERROR    = 4,
} a2dp_media_state_t;

typedef struct {
    esp_a2d_conn_hdl_t                conn_hdl;           /*!< A2DP connection handle */
    uint16_t                          audio_mtu;          /*!< Media MTU for audio packets */
    bt_audio_classic_stream_t        *stream;             /*!< Classic stream instance; created on STARTED */
    esp_bt_audio_stream_codec_info_t  a2d_codec;          /*!< Active/negotiated A2DP codec info */
    a2dp_media_state_t                media_state;        /*!< A2DP media state (A2DP_MEDIA_STATE_*) */
    esp_timer_handle_t                timer;              /*!< Periodic timer driving source pacing */
    uint32_t                          tick;               /*!< Timer tick counter */
    uint32_t                          sent_frames;        /*!< Total number of frames sent since start */
    float                             frames_per_tick;    /*!< Target frames to send per timer tick */
    uint32_t                          max_batch_size;     /*!< Maximum frames to send per batch */
    uint32_t                          rtp_ts;             /*!< Current RTP timestamp (in samples) */
    uint32_t                          samples_per_frame;  /*!< PCM samples represented by one SBC frame */
    esp_a2d_cie_sbc_t                 sbc;                /*!< Negotiated SBC codec capabilities/config */
    EventGroupHandle_t                events;             /*!< Event group used for task synchronization */
    TaskHandle_t                      task;               /*!< FreeRTOS task handle for send task */
    uint8_t                           core_id;            /*!< Send task core ID (0 or 1) */
    uint8_t                           prio;               /*!< Send task priority */
    uint32_t                          stack_size;         /*!< Send task stack size in bytes */
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
    esp_a2d_cie_m24_t                 m24;                /*!< Negotiated AAC codec capabilities/config */
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */
} a2dp_src_ctx_t;

static const char *TAG = "BT_AUD_A2D_SRC";
static const char *s_a2d_conn_state_str[]  = {"Disconnected", "Connecting", "Connected", "Disconnecting"};
static const char *s_a2d_audio_state_str[] = {"Suspended", "Started"};

static a2dp_src_ctx_t *a2dp_src = NULL;

static inline bool _spiram_stack_is_enabled(void)
{
#if defined(CONFIG_SPIRAM_BOOT_INIT) && (CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM)
    bool ret = true;
#if defined(CONFIG_IDF_TARGET_ESP32)
    uint32_t chip_ver = 0;
    chip_ver = efuse_hal_chip_revision();
    if (chip_ver < 3) {
        ESP_LOGW("ESP_GMF_MEM", "Can't support stack on external memory due to ESP32 chip is %d", (int)chip_ver);
        ret = false;
    }
#endif  // defined(CONFIG_IDF_TARGET_ESP32)
    return ret;
#else   // defined(CONFIG_SPIRAM_BOOT_INIT)
    return false;
#endif  /* defined(CONFIG_SPIRAM_BOOT_INIT) && (CONFIG_FREERTOS_TASK_CREATE_ALLOW_EXT_MEM) */
}

static void a2dp_src_clear_packets(void)
{
    if (!a2dp_src || !a2dp_src->stream || !a2dp_src->stream->base.data_q) {
        return;
    }
    while (uxQueueMessagesWaiting(a2dp_src->stream->base.data_q) > 0) {
        esp_bt_audio_stream_packet_t msg = {0};
        if (xQueueReceive(a2dp_src->stream->base.data_q, &msg, 0) == pdTRUE) {
            if (msg.data_owner) {
                esp_a2d_audio_buff_free(msg.data_owner);
            }
        }
    }
}

static uint8_t calc_optimal_bitpool(int sample_rate, esp_sbc_ch_mode_t ch_mode,
                                    uint8_t min_bitpool, uint8_t max_bitpool)
{
    (void)sample_rate;
    uint8_t optimal_bitpool = 0;
    if (ch_mode == ESP_SBC_CH_MODE_MONO) {
        optimal_bitpool = A2DP_SRC_BITPOOL_MONO_DEFAULT;
    } else {
        optimal_bitpool = A2DP_SRC_BITPOOL_STEREO_DEFAULT;
    }
    if (optimal_bitpool < min_bitpool) {
        optimal_bitpool = min_bitpool;
    } else if (optimal_bitpool > max_bitpool) {
        optimal_bitpool = max_bitpool;
    }

    return optimal_bitpool;
}

static size_t calc_sbc_frame_size(esp_sbc_enc_config_t *sbc_cfg)
{
    ESP_RETURN_ON_FALSE(sbc_cfg, 0, TAG, "sbc_cfg is NULL");

    int channels = (sbc_cfg->ch_mode == ESP_SBC_CH_MODE_MONO) ? 1 : 2;
    int blocks = sbc_cfg->block_length;
    int subbands = sbc_cfg->sub_bands_num;
    int bitpool = sbc_cfg->bitpool;
    size_t header_size = 4;
    size_t scale_factors_size = (4 * subbands * channels + 7) / 8;
    size_t audio_data_size;
    if (sbc_cfg->ch_mode == ESP_SBC_CH_MODE_MONO || sbc_cfg->ch_mode == ESP_SBC_CH_MODE_DUAL) {
        audio_data_size = (blocks * channels * bitpool + 7) / 8;
    } else {
        audio_data_size = (blocks * bitpool + 7) / 8;
        if (sbc_cfg->ch_mode == ESP_SBC_CH_MODE_JOINT_STEREO) {
            audio_data_size += (subbands + 7) / 8;
        }
    }
    size_t total_size = header_size + scale_factors_size + audio_data_size;

    ESP_LOGD(TAG, "SBC frame calculation: channels=%d, blocks=%d, subbands=%d, bitpool=%d",
             channels, blocks, subbands, bitpool);
    ESP_LOGD(TAG, "SBC frame sizes: header=%zu, scale_factors=%zu, audio_data=%zu, total=%zu",
             header_size, scale_factors_size, audio_data_size, total_size);

    return total_size;
}

static uint32_t calc_max_frames_per_batch(uint16_t audio_mtu, size_t frame_size)
{
    if (audio_mtu == 0 || frame_size == 0) {
        return 0;
    }

    uint16_t usable_mtu = audio_mtu > A2DP_SRC_MTU_OVERHEAD_BYTES ? audio_mtu - A2DP_SRC_MTU_OVERHEAD_BYTES : audio_mtu;
    uint32_t max_frames = usable_mtu / frame_size;

    if (max_frames < 1) {
        max_frames = 1;
    }
    return max_frames;
}

static uint32_t calc_frames_to_send(uint32_t tick,
                                    float frames_per_tick,
                                    uint32_t sent_frames)
{
    if (tick == 0 || frames_per_tick <= 0.0f) {
        return 0;
    }
    uint32_t should_send = (uint32_t)ceilf(tick * frames_per_tick);
    return (should_send > sent_frames) ? (should_send - sent_frames) : 0;
}

static int get_sample_rate_from_sbc_info(esp_a2d_cie_sbc_t *sbc_info)
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

static int get_block_len_from_sbc_info(esp_a2d_cie_sbc_t *sbc_info)
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

static int get_subbands_from_sbc_info(esp_a2d_cie_sbc_t *sbc_info)
{
    int subbands = 8;
    if (sbc_info->num_subbands & 0x02) {
        subbands = 4;
    } else if (sbc_info->num_subbands & 0x01) {
        subbands = 8;
    }
    return subbands;
}

static esp_sbc_ch_mode_t get_ch_mode_from_sbc_info(esp_a2d_cie_sbc_t *sbc_info)
{
    switch (sbc_info->ch_mode) {
        case 0x08:  // A2DP_SBC_IE_CH_MD_MONO
            return ESP_SBC_CH_MODE_MONO;
        case 0x04:  // A2DP_SBC_IE_CH_MD_DUAL
            return ESP_SBC_CH_MODE_DUAL;
        case 0x02:  // A2DP_SBC_IE_CH_MD_STEREO
            return ESP_SBC_CH_MODE_STEREO;
        case 0x01:  // A2DP_SBC_IE_CH_MD_JOINT
            return ESP_SBC_CH_MODE_JOINT_STEREO;
        default:
            ESP_LOGW(TAG, "Unknown A2DP channel mode: 0x%02x, using stereo", sbc_info->ch_mode);
            return ESP_SBC_CH_MODE_STEREO;
    }
}

static esp_sbc_allocation_method_t get_allocation_method_from_sbc_info(esp_a2d_cie_sbc_t *sbc_info)
{
    switch (sbc_info->alloc_mthd) {
        case 0x02:  // A2DP_SBC_IE_ALLOC_MD_S
            return ESP_SBC_ALLOC_SNR;
        case 0x01:  // A2DP_SBC_IE_ALLOC_MD_L
            return ESP_SBC_ALLOC_LOUDNESS;
        default:
            ESP_LOGW(TAG, "Unknown A2DP allocation method: 0x%02x, using loudness", sbc_info->alloc_mthd);
            return ESP_SBC_ALLOC_LOUDNESS;
    }
}

static bool a2dp_sbc_info_to_codec_info(esp_a2d_cie_sbc_t *sbc_info, esp_bt_audio_stream_codec_info_t *codec_info)
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

static bool a2dp_sbc_info_to_sbc_enc_cfg(esp_a2d_cie_sbc_t *sbc_info, esp_sbc_enc_config_t *sbc_enc_cfg)
{
    if (sbc_info == NULL || sbc_enc_cfg == NULL) {
        return false;
    }
    sbc_enc_cfg->sbc_mode = ESP_SBC_MODE_STD;
    sbc_enc_cfg->bits_per_sample = 16;
    sbc_enc_cfg->sample_rate = get_sample_rate_from_sbc_info(sbc_info);
    sbc_enc_cfg->ch_mode = get_ch_mode_from_sbc_info(sbc_info);
    sbc_enc_cfg->block_length = get_block_len_from_sbc_info(sbc_info);
    sbc_enc_cfg->sub_bands_num = get_subbands_from_sbc_info(sbc_info);
    sbc_enc_cfg->allocation_method = get_allocation_method_from_sbc_info(sbc_info);
    sbc_enc_cfg->bitpool = calc_optimal_bitpool(sbc_enc_cfg->sample_rate,
                                                sbc_enc_cfg->ch_mode,
                                                sbc_info->min_bitpool,
                                                sbc_info->max_bitpool);
    ESP_LOGI(TAG, "SBC encoder configuration:");
    ESP_LOGI(TAG, "  sample_rate: %d", sbc_enc_cfg->sample_rate);
    ESP_LOGI(TAG, "  ch_mode: %d", sbc_enc_cfg->ch_mode);
    ESP_LOGI(TAG, "  block_length: %d", sbc_enc_cfg->block_length);
    ESP_LOGI(TAG, "  sub_bands_num: %d", sbc_enc_cfg->sub_bands_num);
    ESP_LOGI(TAG, "  allocation_method: %d", sbc_enc_cfg->allocation_method);
    ESP_LOGI(TAG, "  bitpool: %d", sbc_enc_cfg->bitpool);
    ESP_LOGI(TAG, "  sbc_mode: %d", sbc_enc_cfg->sbc_mode);
    return true;
}

static void *a2dp_src_create_sbc_enc_cfg(size_t *encoded_frame_size, uint32_t *cfg_size)
{
    esp_sbc_enc_config_t *sbc_enc_cfg = heap_caps_calloc_prefer(1, sizeof(esp_sbc_enc_config_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (sbc_enc_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate sbc_enc_cfg");
        return NULL;
    }
    if (!a2dp_sbc_info_to_sbc_enc_cfg(&a2dp_src->sbc, sbc_enc_cfg)) {
        ESP_LOGE(TAG, "Failed to convert A2DP SBC config to SBC encoder config");
        free(sbc_enc_cfg);
        return NULL;
    }

    *encoded_frame_size = calc_sbc_frame_size(sbc_enc_cfg);
    *cfg_size = sizeof(esp_sbc_enc_config_t);
    return sbc_enc_cfg;
}

static esp_err_t a2dp_src_prepare_sbc_stream_cfg(void **codec_cfg, uint32_t *cfg_size, int *samples_per_frame)
{
    size_t sbc_frame_size = 0;
    *codec_cfg = a2dp_src_create_sbc_enc_cfg(&sbc_frame_size, cfg_size);
    if (*codec_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    a2dp_src->max_batch_size = calc_max_frames_per_batch(a2dp_src->audio_mtu, sbc_frame_size);
    *samples_per_frame = a2dp_src->a2d_codec.frame_size / (__builtin_popcount(a2dp_src->a2d_codec.channels) * (a2dp_src->a2d_codec.bits / 8));
    ESP_LOGI(TAG, "SBC frame size: %zu bytes", sbc_frame_size);
    ESP_LOGI(TAG, "MTU %d: max %u packets per batch", a2dp_src->audio_mtu, a2dp_src->max_batch_size);
    return ESP_OK;
}

static void a2dp_src_handle_sbc_cfg(esp_a2d_cie_sbc_t *sbc_info)
{
    ESP_LOGI(TAG, "Configure audio player:");
    ESP_LOGI(TAG, "  sample rate : %d", sbc_info->samp_freq);
    ESP_LOGI(TAG, "  chan mode   : %d", sbc_info->ch_mode);
    ESP_LOGI(TAG, "  block len   : %d", sbc_info->block_len);
    ESP_LOGI(TAG, "  subbands    : %d", sbc_info->num_subbands);
    ESP_LOGI(TAG, "  alloc method: %d", sbc_info->alloc_mthd);
    ESP_LOGI(TAG, "  min bitpool : %d", sbc_info->min_bitpool);
    ESP_LOGI(TAG, "  max bitpool : %d", sbc_info->max_bitpool);

    if (!a2dp_sbc_info_to_codec_info(sbc_info, &a2dp_src->a2d_codec)) {
        ESP_LOGE(TAG, "Failed to convert A2DP SBC config to codec info");
    }
    memcpy(&a2dp_src->sbc, sbc_info, sizeof(esp_a2d_cie_sbc_t));
}

static esp_err_t a2dp_src_register_sbc_sep(uint8_t seid)
{
    esp_a2d_mcc_t mcc_sbc = {0};
    mcc_sbc.type = ESP_A2D_MCT_SBC;
    mcc_sbc.cie.sbc_info.samp_freq = ESP_A2D_SBC_CIE_SF_16K |
                                     ESP_A2D_SBC_CIE_SF_32K |
                                     ESP_A2D_SBC_CIE_SF_44K |
                                     ESP_A2D_SBC_CIE_SF_48K;
    mcc_sbc.cie.sbc_info.ch_mode = ESP_A2D_SBC_CIE_CH_MODE_STEREO;
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
    return esp_a2d_source_register_stream_endpoint(seid, &mcc_sbc);
}

#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
static int get_sample_rate_from_m24_info(esp_a2d_cie_m24_t *m24_info)
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

static int get_channel_count_from_m24_info(esp_a2d_cie_m24_t *m24_info)
{
    return (m24_info->ch & ESP_A2D_M24_CIE_CH_1) ? 1 : 2;
}

static uint32_t get_bitrate_from_m24_info(esp_a2d_cie_m24_t *m24_info)
{
    uint32_t bitrate = ((uint32_t)(m24_info->br1 & ESP_A2D_M24_CIE_BR1_MSK) << 16) | ((uint32_t)(m24_info->br2 & ESP_A2D_M24_CIE_BR2_MSK) << 8) | (uint32_t)(m24_info->br3 & ESP_A2D_M24_CIE_BR3_MSK);
    bitrate = bitrate ? bitrate : A2DP_SRC_AAC_DEFAULT_BITRATE;
    return bitrate > A2DP_SRC_AAC_DEFAULT_BITRATE ? A2DP_SRC_AAC_DEFAULT_BITRATE : bitrate;
}

static uint32_t get_mtu_limited_aac_bitrate(int sample_rate, uint16_t audio_mtu)
{
    if (sample_rate <= 0 || audio_mtu <= A2DP_SRC_AAC_MTU_MARGIN_BYTES) {
        return A2DP_SRC_AAC_DEFAULT_BITRATE;
    }

    uint32_t payload_bytes = audio_mtu - A2DP_SRC_AAC_MTU_MARGIN_BYTES;
    uint64_t bitrate = (uint64_t)payload_bytes * 8 * (uint64_t)sample_rate;
    bitrate /= A2DP_SRC_AAC_SAMPLES_PER_FRAME;
    bitrate = (bitrate * A2DP_SRC_AAC_MTU_SAFETY_PERCENT) / 100;
    return (uint32_t)bitrate;
}

static uint32_t clamp_aac_bitrate_to_mtu(uint32_t bitrate, int sample_rate)
{
    if (!a2dp_src || a2dp_src->audio_mtu == 0) {
        return bitrate;
    }

    uint32_t mtu_limited_bitrate = get_mtu_limited_aac_bitrate(sample_rate, a2dp_src->audio_mtu);
    if (bitrate > mtu_limited_bitrate) {
        ESP_LOGI(TAG, "Clamp AAC bitrate from %" PRIu32 " to %" PRIu32 " for MTU %d",
                 bitrate, mtu_limited_bitrate, a2dp_src->audio_mtu);
        return mtu_limited_bitrate;
    }
    return bitrate;
}

static bool a2dp_m24_info_to_codec_info(esp_a2d_cie_m24_t *m24_info, esp_bt_audio_stream_codec_info_t *codec_info)
{
    if (m24_info == NULL || codec_info == NULL) {
        return false;
    }

    uint32_t channels = get_channel_count_from_m24_info(m24_info) == 1 ? ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT : (ESP_BT_AUDIO_AUDIO_LOC_FRONT_LEFT | ESP_BT_AUDIO_AUDIO_LOC_FRONT_RIGHT);
    codec_info->codec_type = ESP_BT_AUDIO_STREAM_CODEC_AAC;
    codec_info->bits = 16;
    codec_info->channels = channels;
    codec_info->sample_rate = get_sample_rate_from_m24_info(m24_info);
    codec_info->frame_size = A2DP_SRC_AAC_SAMPLES_PER_FRAME * __builtin_popcount(channels) * (codec_info->bits / 8);

    ESP_LOGD(TAG, "AAC codec info parameters:");
    ESP_LOGD(TAG, "  sample_rate: %d", codec_info->sample_rate);
    ESP_LOGD(TAG, "  channels: 0x%x (%d ch)", codec_info->channels, __builtin_popcount(codec_info->channels));
    ESP_LOGD(TAG, "  frame_size: %d", codec_info->frame_size);

    return true;
}

static bool a2dp_m24_info_to_aac_enc_cfg(esp_a2d_cie_m24_t *m24_info, esp_aac_enc_config_t *aac_enc_cfg)
{
    if (m24_info == NULL || aac_enc_cfg == NULL) {
        return false;
    }

    esp_aac_enc_config_t default_aac_enc_cfg = ESP_AAC_ENC_CONFIG_DEFAULT();
    *aac_enc_cfg = default_aac_enc_cfg;
    aac_enc_cfg->sample_rate = get_sample_rate_from_m24_info(m24_info);
    aac_enc_cfg->channel = get_channel_count_from_m24_info(m24_info) == 1 ? ESP_AUDIO_MONO : ESP_AUDIO_DUAL;
    aac_enc_cfg->bits_per_sample = ESP_AUDIO_BIT16;
    aac_enc_cfg->bitrate = clamp_aac_bitrate_to_mtu(get_bitrate_from_m24_info(m24_info), aac_enc_cfg->sample_rate);
    aac_enc_cfg->adts_used = false;

    ESP_LOGD(TAG, "AAC encoder configuration:");
    ESP_LOGD(TAG, "  sample_rate: %d", aac_enc_cfg->sample_rate);
    ESP_LOGD(TAG, "  channel: %d", aac_enc_cfg->channel);
    ESP_LOGD(TAG, "  bits_per_sample: %d", aac_enc_cfg->bits_per_sample);
    ESP_LOGD(TAG, "  bitrate: %d", aac_enc_cfg->bitrate);
    ESP_LOGD(TAG, "  adts_used: %d", aac_enc_cfg->adts_used);
    return true;
}

static void *a2dp_src_create_aac_enc_cfg(uint32_t *cfg_size)
{
    esp_aac_enc_config_t *aac_enc_cfg = heap_caps_calloc_prefer(1, sizeof(esp_aac_enc_config_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (aac_enc_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to allocate aac_enc_cfg");
        return NULL;
    }
    if (!a2dp_m24_info_to_aac_enc_cfg(&a2dp_src->m24, aac_enc_cfg)) {
        ESP_LOGE(TAG, "Failed to convert A2DP AAC config to AAC encoder config");
        free(aac_enc_cfg);
        return NULL;
    }

    *cfg_size = sizeof(esp_aac_enc_config_t);
    return aac_enc_cfg;
}

static esp_err_t a2dp_src_prepare_aac_stream_cfg(void **codec_cfg, uint32_t *cfg_size, int *samples_per_frame)
{
    *codec_cfg = a2dp_src_create_aac_enc_cfg(cfg_size);
    if (*codec_cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    a2dp_src->max_batch_size = 1;
    *samples_per_frame = A2DP_SRC_AAC_SAMPLES_PER_FRAME;
    ESP_LOGI(TAG, "AAC MTU %d: send one frame per packet", a2dp_src->audio_mtu);
    return ESP_OK;
}

static void a2dp_src_handle_aac_cfg(esp_a2d_cie_m24_t *m24_info)
{
    ESP_LOGD(TAG, "Configure AAC audio source:");
    ESP_LOGD(TAG, "  drc        : 0x%x", m24_info->drc);
    ESP_LOGD(TAG, "  obj_type   : 0x%x", m24_info->obj_type);
    ESP_LOGD(TAG, "  samp_freq1 : 0x%x", m24_info->samp_freq1);
    ESP_LOGD(TAG, "  samp_freq2 : 0x%x", m24_info->samp_freq2);
    ESP_LOGD(TAG, "  channels   : 0x%x", m24_info->ch);
    ESP_LOGD(TAG, "  vbr        : 0x%x", m24_info->vbr);
    ESP_LOGD(TAG, "  bitrate    : 0x%02x%02x%02x", m24_info->br1, m24_info->br2, m24_info->br3);

    if (!a2dp_m24_info_to_codec_info(m24_info, &a2dp_src->a2d_codec)) {
        ESP_LOGE(TAG, "Failed to convert A2DP AAC config to codec info");
    }
    memcpy(&a2dp_src->m24, m24_info, sizeof(esp_a2d_cie_m24_t));
}

static esp_err_t a2dp_src_register_aac_sep(uint8_t seid)
{
    esp_a2d_mcc_t mcc_aac = {0};
    mcc_aac.type = ESP_A2D_MCT_M24;
    mcc_aac.cie.m24_info.drc = ESP_A2D_M24_CIE_DRC_NS;
    mcc_aac.cie.m24_info.obj_type = ESP_A2D_M24_CIE_OBJ_TYPE_2_AAC_LC |
                                    ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LC;
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
    /* AAC bitrate is encoded as a 23-bit value in br1/br2/br3: 0x017700 = 96000 bps. */
    mcc_aac.cie.m24_info.br1 = 0x01 & ESP_A2D_M24_CIE_BR1_MSK;
    mcc_aac.cie.m24_info.br2 = 0x77 & ESP_A2D_M24_CIE_BR2_MSK;
    mcc_aac.cie.m24_info.br3 = 0x00 & ESP_A2D_M24_CIE_BR3_MSK;
    return esp_a2d_source_register_stream_endpoint(seid, &mcc_aac);
}
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */

static esp_err_t a2dp_src_prepare_stream_codec_cfg(void **codec_cfg, uint32_t *cfg_size, int *samples_per_frame)
{
    switch (a2dp_src->a2d_codec.codec_type) {
        case ESP_BT_AUDIO_STREAM_CODEC_SBC:
            return a2dp_src_prepare_sbc_stream_cfg(codec_cfg, cfg_size, samples_per_frame);
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
        case ESP_BT_AUDIO_STREAM_CODEC_AAC:
            return a2dp_src_prepare_aac_stream_cfg(codec_cfg, cfg_size, samples_per_frame);
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */
        default:
            ESP_LOGE(TAG, "Unsupported A2DP source codec type: %d", a2dp_src->a2d_codec.codec_type);
            return ESP_ERR_NOT_SUPPORTED;
    }
}

static void a2dp_src_handle_audio_cfg(esp_a2d_mcc_t *mcc)
{
    ESP_LOGI(TAG, "A2DP audio stream configuration, codec type: %d", mcc->type);
    switch (mcc->type) {
        case ESP_A2D_MCT_SBC:
            a2dp_src_handle_sbc_cfg(&mcc->cie.sbc_info);
            break;
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
        case ESP_A2D_MCT_M24:
            a2dp_src_handle_aac_cfg(&mcc->cie.m24_info);
            break;
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */
        default:
            ESP_LOGE(TAG, "Unsupported codec type: %d", mcc->type);
            break;
    }
}

static int a2dp_src_collect_frames(bt_audio_classic_stream_t *stream,
                                   uint32_t max_collect,
                                   esp_a2d_audio_buff_t **out_frames,
                                   size_t *out_size)
{
    if (!stream || !stream->base.data_q || !out_frames || !out_size) {
        return 0;
    }

    int collected = 0;
    size_t total_size = 0;
    for (uint32_t i = 0; i < max_collect; i++) {
        esp_bt_audio_stream_packet_t msg = {0};
        if (xQueueReceive(stream->base.data_q, &msg, 0) != pdTRUE) {
            break;
        }
        if (!msg.data_owner) {
            continue;
        }
        esp_a2d_audio_buff_t *buf = (esp_a2d_audio_buff_t *)msg.data_owner;
        out_frames[collected++] = buf;
        total_size += buf->data_len;
    }

    *out_size = total_size;
    return collected;
}

static esp_err_t a2dp_src_send_batch(esp_a2d_audio_buff_t **audio_buffers,
                                     int collected_count,
                                     size_t total_size)
{
    if (!audio_buffers || collected_count <= 0 || total_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!a2dp_src) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_a2d_audio_buff_t *combined_buf = esp_a2d_audio_buff_alloc(total_size);
    if (!combined_buf) {
        return ESP_ERR_NO_MEM;
    }

    size_t offset = 0;
    for (int i = 0; i < collected_count; i++) {
        memcpy(combined_buf->data + offset, audio_buffers[i]->data, audio_buffers[i]->data_len);
        offset += audio_buffers[i]->data_len;
    }
    combined_buf->data_len = total_size;
    combined_buf->number_frame = collected_count;
    combined_buf->timestamp = a2dp_src->rtp_ts;

    esp_err_t ret = esp_a2d_source_audio_data_send(a2dp_src->conn_hdl, combined_buf);
    if (ret != ESP_OK) {
        esp_a2d_audio_buff_free(combined_buf);
        return ret;
    }

    a2dp_src->rtp_ts += a2dp_src->samples_per_frame * (uint32_t)collected_count;
    return ESP_OK;
}

static bool a2dp_src_payload_fits_mtu(size_t data_size)
{
    return a2dp_src && a2dp_src->audio_mtu > 0 && data_size <= a2dp_src->audio_mtu;
}

static void a2dp_src_send_data(bt_audio_classic_stream_t *stream)
{
    if (!a2dp_src || !stream || !stream->base.data_q ||
        a2dp_src->media_state != A2DP_MEDIA_STATE_STARTED) {
        a2dp_src_clear_packets();
        return;
    }

    if (uxQueueMessagesWaiting((QueueHandle_t)stream->base.data_q) < a2dp_src->max_batch_size) {
        return;
    }

    if ((++a2dp_src->tick) >= (UINT32_MAX * 4) / 5) {
        ESP_LOGI(TAG, "Resetting counters to prevent overflow");
        a2dp_src->tick = 1;
        a2dp_src->sent_frames = 0;
    }

    uint32_t remaining = calc_frames_to_send(a2dp_src->tick, a2dp_src->frames_per_tick, a2dp_src->sent_frames);
    uint32_t cur_sent = 0;

    while (remaining > 0) {
        uint32_t cur_batch_size = (remaining > a2dp_src->max_batch_size) ? a2dp_src->max_batch_size : remaining;
        esp_a2d_audio_buff_t *frames[cur_batch_size];
        size_t data_size = 0;
        int collected = a2dp_src_collect_frames(stream, cur_batch_size, frames, &data_size);
        if (collected <= 0) {
            break;
        }
        if (data_size == 0) {
            for (int i = 0; i < collected; i++) {
                esp_a2d_audio_buff_free(frames[i]);
            }
            break;
        }

        esp_err_t ret = ESP_OK;
        bool dropped = false;
        if (!a2dp_src_payload_fits_mtu(data_size)) {
            cur_sent += collected;
            a2dp_src->rtp_ts += a2dp_src->samples_per_frame * (uint32_t)collected;
            dropped = true;
        } else {
            ret = a2dp_src_send_batch(frames, collected, data_size);
        }

        if (dropped) {
            ESP_LOGW(TAG, "Drop oversized frame batch: %zu bytes exceeds MTU %d", data_size, a2dp_src->audio_mtu);
        } else if (ret == ESP_OK) {
            cur_sent += collected;
            ESP_LOGD(TAG, "Sent batch: %d frames, %zu bytes (MTU: %d)", collected, data_size, a2dp_src->audio_mtu);
        } else if (ret == ESP_ERR_INVALID_SIZE) {
            ESP_LOGW(TAG, "Drop invalid-size frame batch: %zu bytes, MTU: %d", data_size, a2dp_src->audio_mtu);
            cur_sent += collected;
            a2dp_src->rtp_ts += a2dp_src->samples_per_frame * (uint32_t)collected;
            ret = ESP_OK;
        } else {
            ESP_LOGW(TAG, "Failed to send frame batch: %s, data_len: %zu, MTU: %d", esp_err_to_name(ret), data_size, a2dp_src->audio_mtu);
            a2dp_src->media_state = A2DP_MEDIA_STATE_ERROR;
            esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
        }

        for (int i = 0; i < collected; i++) {
            esp_a2d_audio_buff_free(frames[i]);
        }

        remaining -= collected;
        if (ret != ESP_OK) {
            break;
        }
    }

    if (cur_sent > 0) {
        a2dp_src->sent_frames += cur_sent;
    }
}

static void a2dp_src_timer_cb(void *arg)
{
    (void)arg;
    if (!a2dp_src || !a2dp_src->events) {
        return;
    }
    xEventGroupSetBits(a2dp_src->events, A2DP_SRC_EVT_TIMER_EXPIRED);
}

static void a2dp_src_send_task(void *arg)
{
    bt_audio_classic_stream_t *stream = (bt_audio_classic_stream_t *)arg;
    if (!a2dp_src || !a2dp_src->events) {
        vTaskDelete(NULL);
        return;
    }

    while (true) {
        EventBits_t bits = xEventGroupWaitBits(a2dp_src->events,
                                               A2DP_SRC_EVT_TIMER_EXPIRED | A2DP_SRC_EVT_EXITING,
                                               pdTRUE,
                                               pdFALSE,
                                               portMAX_DELAY);
        if (bits & A2DP_SRC_EVT_EXITING) {
            break;
        }
        if (bits & A2DP_SRC_EVT_TIMER_EXPIRED) {
            a2dp_src_send_data(stream);
        }
    }

    xEventGroupSetBits(a2dp_src->events, A2DP_SRC_EVT_EXITED);
    a2dp_src->task = NULL;
    vTaskDelete(NULL);
}

static void a2dp_src_stop_send()
{
    if (!a2dp_src) {
        ESP_LOGE(TAG, "A2DP source not initialized or not in source mode");
        return;
    }
    if (a2dp_src->timer) {
        ESP_LOGI(TAG, "A2DP source stop timer");
        esp_timer_stop(a2dp_src->timer);
        esp_timer_delete(a2dp_src->timer);
        a2dp_src->timer = NULL;
        a2dp_src->tick = 0;
        a2dp_src->sent_frames = 0;
        a2dp_src->frames_per_tick = 0;
        a2dp_src->max_batch_size = 0;
        a2dp_src->rtp_ts = 0;
        a2dp_src->samples_per_frame = 0;
    }

    if (a2dp_src->events) {
        xEventGroupSetBits(a2dp_src->events, A2DP_SRC_EVT_EXITING);
        (void)xEventGroupWaitBits(a2dp_src->events, A2DP_SRC_EVT_EXITED,
                                  pdTRUE, pdTRUE, pdMS_TO_TICKS(2000));
        if (a2dp_src->task) {
            vTaskDelete(a2dp_src->task);
            a2dp_src->task = NULL;
        }
        vEventGroupDelete(a2dp_src->events);
        a2dp_src->events = NULL;
    }
}

static esp_err_t a2dp_src_start_send(void)
{
    esp_err_t ret = ESP_OK;
    if (!a2dp_src) {
        ESP_LOGE(TAG, "A2DP source not initialized or not in source mode");
        return ESP_ERR_INVALID_STATE;
    }
    if (!a2dp_src->events) {
        a2dp_src->events = xEventGroupCreate();
        if (!a2dp_src->events) {
            ESP_LOGE(TAG, "Failed to create A2DP src event group");
            return ESP_ERR_NO_MEM;
        }
    }
    if (!a2dp_src->task) {
        BaseType_t ret = pdFAIL;
        uint8_t core_id = a2dp_src->core_id;
        uint8_t prio = a2dp_src->prio;
        uint32_t stack_size = a2dp_src->stack_size;
        if (_spiram_stack_is_enabled()) {
            ret = xTaskCreatePinnedToCoreWithCaps(a2dp_src_send_task, "a2dp_src_send",
                                                  stack_size, a2dp_src->stream,
                                                  prio, &a2dp_src->task,
                                                  core_id, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            ESP_LOGI(TAG, "Created A2DP src send task in SPIRAM");
        } else {
            ret = xTaskCreatePinnedToCore(a2dp_src_send_task, "a2dp_src_send",
                                          stack_size, a2dp_src->stream,
                                          prio, &a2dp_src->task,
                                          core_id);
            ESP_LOGI(TAG, "Created A2DP src send task in RAM");
        }
        if (ret != pdPASS) {
            ESP_LOGE(TAG, "Failed to create A2DP src send task");
            vEventGroupDelete(a2dp_src->events);
            a2dp_src->events = NULL;
            return ESP_FAIL;
        }
    }
    xEventGroupClearBits(a2dp_src->events, A2DP_SRC_EVT_TIMER_EXPIRED | A2DP_SRC_EVT_EXITING | A2DP_SRC_EVT_EXITED);

    void *codec_cfg = NULL;
    uint32_t cfg_size = 0;
    int samples_per_frame = 0;

    ret = a2dp_src_prepare_stream_codec_cfg(&codec_cfg, &cfg_size, &samples_per_frame);
    if (ret != ESP_OK) {
        goto error;
    }

    memcpy(&a2dp_src->stream->base.codec_info, &a2dp_src->a2d_codec, sizeof(esp_bt_audio_stream_codec_info_t));
    a2dp_src->stream->base.codec_info.codec_cfg = codec_cfg;
    a2dp_src->stream->base.codec_info.cfg_size = cfg_size;

    int samples_per_interval = (A2DP_SRC_SEND_INTERVAL_MS * a2dp_src->stream->base.codec_info.sample_rate) / 1000;

    a2dp_src->tick = 0;
    a2dp_src->sent_frames = 0;
    a2dp_src->frames_per_tick = (float)samples_per_interval / (float)samples_per_frame;
    a2dp_src->samples_per_frame = (uint32_t)samples_per_frame;
    a2dp_src->rtp_ts = esp_random();

    ESP_LOGI(TAG, "A2DP Source: %dHz, %.2f frames/%dms, max batch %u",
             a2dp_src->stream->base.codec_info.sample_rate, a2dp_src->frames_per_tick, A2DP_SRC_SEND_INTERVAL_MS, a2dp_src->max_batch_size);

    if (a2dp_src->timer) {
        ESP_LOGI(TAG, "A2DP source stop existing timer");
        esp_timer_stop(a2dp_src->timer);
        esp_timer_delete(a2dp_src->timer);
        a2dp_src->timer = NULL;
    }

    esp_timer_create_args_t timer_args = {
        .callback = a2dp_src_timer_cb,
        .arg = a2dp_src->stream,
        .name = "a2dp_send_timer"};
    ret = esp_timer_create(&timer_args, &a2dp_src->timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(ret));
        goto error;
    }
    ret = esp_timer_start_periodic(a2dp_src->timer, A2DP_SRC_SEND_INTERVAL_MS * 1000);  /* esp_timer uses microseconds */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(ret));
        goto error;
    }
    return ESP_OK;

error:
    a2dp_src_stop_send();
    if (a2dp_src->stream && a2dp_src->stream->base.codec_info.codec_cfg == codec_cfg) {
        free(codec_cfg);
        a2dp_src->stream->base.codec_info.codec_cfg = NULL;
        a2dp_src->stream->base.codec_info.cfg_size = 0;
    } else {
        free(codec_cfg);
    }
    return ret;
}

static esp_err_t a2dp_src_media_start(void *config)
{
    (void)config;
    if (!a2dp_src || a2dp_src->media_state != A2DP_MEDIA_STATE_IDLE) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "A2DP source media start");
    a2dp_src->media_state = A2DP_MEDIA_STATE_STARTING;
    return esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_START);
}

static esp_err_t a2dp_src_media_stop()
{
    if (!a2dp_src || a2dp_src->media_state != A2DP_MEDIA_STATE_STARTED) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "A2DP source media stop");
    a2dp_src->media_state = A2DP_MEDIA_STATE_STOPPING;
    a2dp_src_clear_packets();
    return esp_a2d_media_ctrl(ESP_A2D_MEDIA_CTRL_SUSPEND);
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
                    a2dp_src->conn_hdl = a2d->conn_stat.conn_hdl;
                    a2dp_src->audio_mtu = a2d->conn_stat.audio_mtu;
                    ESP_LOGI(TAG, "A2DP connection handle saved: %d, audio_mtu: %d", a2dp_src->conn_hdl, a2dp_src->audio_mtu);

                    esp_bt_audio_media_ops_t a2dp_src_media_ops = {
                        .a2d_media_start = a2dp_src_media_start,
                        .a2d_media_stop = a2dp_src_media_stop,
                    };
                    bt_audio_ops_set_media(&a2dp_src_media_ops);
                    event_data.connected = true;
                    memcpy(&event_data.addr, bda, ESP_BD_ADDR_LEN);
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_CONNECTION_STATE_CHG, &event_data);
                    break;
                case ESP_A2D_CONNECTION_STATE_DISCONNECTED:
                    a2dp_src->conn_hdl = 0;
                    a2dp_src->audio_mtu = 0;
                    a2dp_src->media_state = A2DP_MEDIA_STATE_IDLE;
                    memset(&a2dp_src->a2d_codec, 0, sizeof(esp_bt_audio_stream_codec_info_t));
                    memset(&a2dp_src->sbc, 0, sizeof(esp_a2d_cie_sbc_t));
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
                    memset(&a2dp_src->m24, 0, sizeof(esp_a2d_cie_m24_t));
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */
                    ESP_LOGI(TAG, "A2DP connection handle cleared");

                    bt_audio_ops_set_media((esp_bt_audio_media_ops_t *)NULL);
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
            ESP_LOGI(TAG, "A2DP audio state: %s (source)", s_a2d_audio_state_str[a2d->audio_stat.state]);
            if (!a2dp_src->stream && ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state) {
                esp_err_t ret = bt_audio_classic_stream_create(&a2dp_src->stream);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to create A2DP stream: %s", esp_err_to_name(ret));
                    break;
                }
                a2dp_src->stream->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP;
                a2dp_src->stream->base.context = ESP_BT_AUDIO_STREAM_CONTEXT_MEDIA;
                a2dp_src->stream->conn_handle = a2dp_src->conn_hdl;
            }
            esp_bt_audio_event_stream_st_t event_data = {0};
            event_data.stream_handle = a2dp_src->stream;
            if (ESP_A2D_AUDIO_STATE_STARTED == a2d->audio_stat.state) {
                a2dp_src->stream->base.direction = ESP_BT_AUDIO_STREAM_DIR_SOURCE;
                if (a2dp_src->a2d_codec.codec_type == ESP_BT_AUDIO_STREAM_CODEC_UNKNOWN) {
                    ESP_LOGE(TAG, "No negotiated A2DP codec configuration available");
                    break;
                }
                esp_err_t ret = a2dp_src_start_send();
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to start send: %s", esp_err_to_name(ret));
                    break;
                }
                event_data.state = ESP_BT_AUDIO_STREAM_STATE_ALLOCATED;
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                event_data.state = ESP_BT_AUDIO_STREAM_STATE_STARTED;
                bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
            } else if (ESP_A2D_AUDIO_STATE_SUSPEND == a2d->audio_stat.state) {
                if (a2dp_src->stream) {
                    event_data.state = ESP_BT_AUDIO_STREAM_STATE_STOPPED;
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                    a2dp_src_stop_send();
                    event_data.state = ESP_BT_AUDIO_STREAM_STATE_RELEASED;
                    bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                    bt_audio_classic_stream_destroy(a2dp_src->stream);
                    a2dp_src->stream = NULL;
                }
                a2dp_src->media_state = A2DP_MEDIA_STATE_IDLE;
            }
            break;
        }
        case ESP_A2D_AUDIO_CFG_EVT: {
            a2d = (esp_a2d_cb_param_t *)(p_param);
            a2dp_src_handle_audio_cfg(&a2d->audio_cfg.mcc);
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
        case ESP_A2D_MEDIA_CTRL_ACK_EVT: {
            a2d = (esp_a2d_cb_param_t *)(p_param);
            if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_START) {
                if (a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                    a2dp_src->media_state = A2DP_MEDIA_STATE_STARTED;
                    ESP_LOGI(TAG, "A2DP media start successfully.");
                } else {
                    ESP_LOGI(TAG, "A2DP media start failed.");
                }
            } else if (a2d->media_ctrl_stat.cmd == ESP_A2D_MEDIA_CTRL_SUSPEND) {
                a2dp_src->media_state = A2DP_MEDIA_STATE_IDLE;
                if (a2d->media_ctrl_stat.status == ESP_A2D_MEDIA_CTRL_ACK_SUCCESS) {
                    ESP_LOGI(TAG, "A2DP media suspend successfully.");
                } else {
                    ESP_LOGI(TAG, "A2DP media suspend failed.");
                    if (a2dp_src->stream) {
                        esp_bt_audio_event_stream_st_t event_data = {0};
                        event_data.stream_handle = a2dp_src->stream;
                        event_data.state = ESP_BT_AUDIO_STREAM_STATE_STOPPED;
                        bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                        a2dp_src_stop_send();
                        event_data.state = ESP_BT_AUDIO_STREAM_STATE_RELEASED;
                        bt_audio_evt_dispatch(ESP_BT_AUDIO_EVT_DST_USR, ESP_BT_AUDIO_EVENT_STREAM_STATE_CHG, &event_data);
                        bt_audio_classic_stream_destroy(a2dp_src->stream);
                        a2dp_src->stream = NULL;
                    }
                }
            }
            break;
        }
        default:
            ESP_LOGI(TAG, "%s unhandled event: %d", __func__, event);
            break;
    }
}

static esp_err_t a2dp_src_connect(uint8_t *bda)
{
    if (!bda) {
        ESP_LOGE(TAG, "Invalid BDA for connection");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "A2DP Src: Connecting to peer: %02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    esp_err_t ret = esp_a2d_source_connect(bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect to A2DP sink: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t a2dp_src_disconnect(uint8_t *bda)
{
    if (!bda) {
        ESP_LOGE(TAG, "Invalid BDA for disconnection");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "A2DP Src: Disconnecting from peer: %02x:%02x:%02x:%02x:%02x:%02x",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);

    esp_err_t ret = esp_a2d_source_disconnect(bda);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disconnect from A2DP sink: %s", esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t bt_audio_a2dp_src_init(const esp_bt_audio_classic_cfg_t *classic_cfg)
{
    if (a2dp_src) {
        ESP_LOGW(TAG, "A2DP source already initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (!classic_cfg) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_FALSE(classic_cfg->a2dp_src_send_task_core_id < 2, ESP_ERR_INVALID_ARG, TAG, "Invalid A2DP source send task core ID: %d", classic_cfg->a2dp_src_send_task_core_id);
    ESP_RETURN_ON_FALSE(classic_cfg->a2dp_src_send_task_prio < 24, ESP_ERR_INVALID_ARG, TAG, "Invalid A2DP source send task priority: %d", classic_cfg->a2dp_src_send_task_prio);
    ESP_RETURN_ON_FALSE(classic_cfg->a2dp_src_send_task_stack_size > 0, ESP_ERR_INVALID_ARG, TAG, "Invalid A2DP source send task stack size: %d", classic_cfg->a2dp_src_send_task_stack_size);

    a2dp_src = heap_caps_calloc_prefer(1, sizeof(a2dp_src_ctx_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    ESP_RETURN_ON_FALSE(a2dp_src, ESP_ERR_NO_MEM, TAG, "Failed to malloc space for a2dp_src");
    a2dp_src->media_state = A2DP_MEDIA_STATE_IDLE;
    a2dp_src->core_id = classic_cfg->a2dp_src_send_task_core_id;
    a2dp_src->prio = classic_cfg->a2dp_src_send_task_prio;
    a2dp_src->stack_size = classic_cfg->a2dp_src_send_task_stack_size;

    ESP_ERROR_CHECK(esp_a2d_source_init());
    ESP_ERROR_CHECK(esp_a2d_register_callback(&bt_a2d_event_cb));
    uint8_t seid = 0;
#if CONFIG_BT_A2DP_CODEC_AAC_ENABLED
    ESP_ERROR_CHECK(a2dp_src_register_aac_sep(seid++));
#else
    ESP_LOGW(TAG, "A2DP AAC codec is disabled. Enable CONFIG_BT_A2DP_CODEC_AAC_ENABLED to advertise AAC");
#endif  /* CONFIG_BT_A2DP_CODEC_AAC_ENABLED */

    ESP_ERROR_CHECK(a2dp_src_register_sbc_sep(seid));

    esp_bt_audio_classic_ops_t a2dp_src_ops = {0};
    bt_audio_ops_get_classic(&a2dp_src_ops);
    a2dp_src_ops.a2d_src_connect = a2dp_src_connect;
    a2dp_src_ops.a2d_src_disconnect = a2dp_src_disconnect;
    ESP_LOGI(TAG, "Setting A2DP source operations");
    ESP_ERROR_CHECK(bt_audio_ops_set_classic(&a2dp_src_ops));
    ESP_LOGI(TAG, "A2DP source initialized");
    return ESP_OK;
}

esp_err_t bt_audio_a2dp_src_deinit()
{
    if (!a2dp_src) {
        return ESP_ERR_INVALID_STATE;
    }

    a2dp_src_stop_send();
    if (a2dp_src->stream) {
        bt_audio_classic_stream_destroy(a2dp_src->stream);
        a2dp_src->stream = NULL;
    }

    esp_a2d_source_deinit();
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_NON_DISCOVERABLE);

    free(a2dp_src);
    a2dp_src = NULL;

    ESP_LOGI(TAG, "A2DP source deinitialized");
    return ESP_OK;
}
