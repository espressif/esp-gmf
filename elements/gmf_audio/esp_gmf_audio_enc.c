/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_gmf_node.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_mutex.h"
#include "esp_gmf_audio_enc.h"
#include "esp_audio_enc_default.h"
#include "esp_gmf_cache.h"
#include "esp_gmf_cap.h"
#include "esp_gmf_caps_def.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_audio_element.h"
#include "gmf_audio_common.h"

#define AUD_ENC_DEFAULT_INPUT_TIME_MS (20)
#define SET_ENC_BASIC_INFO(cfg, info) do {          \
    (cfg)->sample_rate     = (info)->sample_rates;  \
    (cfg)->channel         = (info)->channels;      \
    (cfg)->bits_per_sample = (info)->bits;          \
} while (0)

/**
 * @brief Audio encoder context in GMF
 */
typedef struct {
    esp_gmf_audio_element_t parent;          /*!< The GMF audio encoder handle */
    esp_audio_enc_handle_t  audio_enc_hd;    /*!< The audio encoder handle */
    esp_gmf_cache_t        *cached_payload;  /*!< A Cached payload for data concatenation */
    uint32_t                bitrate;         /*!< The bitrate of the encoded data */
    esp_gmf_payload_t      *origin_in_load;  /*!< The original input payload */
    int64_t                 cur_pts;         /*!< The audio Presentation Time Stamp(pts) */
} esp_gmf_audio_enc_t;

static const char *TAG = "ESP_GMF_AENC";

static esp_gmf_err_t __audio_enc_get_frame_size(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                                uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, buf, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_args_desc_t *enc_desc = arg_desc;
    uint32_t *in_size = (uint32_t *)(buf);
    enc_desc = enc_desc->next;
    uint32_t *out_size = (uint32_t *)(buf + enc_desc->offset);
    return esp_gmf_audio_enc_get_frame_size(handle, in_size, out_size);
}

static esp_gmf_err_t __audio_enc_set_bitrate(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                             uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, buf, return ESP_GMF_ERR_INVALID_ARG);
    uint32_t bitrate = *((uint32_t *)buf);
    return esp_gmf_audio_enc_set_bitrate(handle, bitrate);
}

static esp_gmf_err_t __audio_enc_get_bitrate(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                             uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, buf, return ESP_GMF_ERR_INVALID_ARG);
    uint32_t *bitrate = (uint32_t *)buf;
    return esp_gmf_audio_enc_get_bitrate(handle, bitrate);
}

static esp_gmf_err_t __audio_enc_reconfig(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                          uint8_t *buf, int buf_len)
{
ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
ESP_GMF_NULL_CHECK(TAG, buf, return ESP_GMF_ERR_INVALID_ARG);
esp_audio_enc_config_t *config = (esp_audio_enc_config_t *)buf;
return esp_gmf_audio_enc_reconfig(handle, config);
}

static esp_gmf_err_t __audio_enc_reconfig_by_sound_info(esp_gmf_element_handle_t handle, esp_gmf_args_desc_t *arg_desc,
                                                        uint8_t *buf, int buf_len)
{
    ESP_GMF_NULL_CHECK(TAG, arg_desc, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, buf, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_info_sound_t *snd_info = (esp_gmf_info_sound_t *)buf;
    return esp_gmf_audio_enc_reconfig_by_sound_info(handle, snd_info);
}

static inline esp_gmf_err_t dupl_esp_gmf_audio_enc_cfg(esp_audio_enc_config_t *config, esp_audio_enc_config_t **new_config)
{
    void *sub_cfg = NULL;
    *new_config = esp_gmf_oal_calloc(1, sizeof(*config));
    ESP_GMF_MEM_VERIFY(TAG, *new_config, {return ESP_GMF_ERR_MEMORY_LACK;}, "audio encoder handle configuration", sizeof(*config));
    memcpy(*new_config, config, sizeof(*config));
    if (config->cfg && (config->cfg_sz > 0)) {
        sub_cfg = esp_gmf_oal_calloc(1, config->cfg_sz);
        ESP_GMF_MEM_VERIFY(TAG, sub_cfg, {esp_gmf_oal_free(*new_config); return ESP_GMF_ERR_MEMORY_LACK;},
                           "audio encoder configuration", (int)config->cfg_sz);
        memcpy(sub_cfg, config->cfg, config->cfg_sz);
        (*new_config)->cfg = sub_cfg;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static inline void free_esp_gmf_audio_enc_cfg(esp_audio_enc_config_t *config)
{
    if (config) {
        if (config->cfg) {
            esp_gmf_oal_free(config->cfg);
            config->cfg = NULL;
            config->cfg_sz = 0;
        }
        esp_gmf_oal_free(config);
    }
}

static inline void audio_enc_change_audio_info(esp_audio_enc_config_t *enc_cfg, esp_gmf_info_sound_t *info)
{
    switch (enc_cfg->type) {
        case ESP_AUDIO_TYPE_AAC: {
            esp_aac_enc_config_t *aac_enc_cfg = (esp_aac_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(aac_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_AMRNB: {
            esp_amrnb_enc_config_t *amr_enc_cfg = (esp_amrnb_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(amr_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_G711U:
        case ESP_AUDIO_TYPE_G711A: {
            esp_g711_enc_config_t *g711_enc_cfg = (esp_g711_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(g711_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_AMRWB: {
            esp_amrnb_enc_config_t *amr_enc_cfg = (esp_amrnb_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(amr_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_ALAC: {
            esp_alac_enc_config_t *alac_enc_cfg = (esp_alac_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(alac_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_PCM: {
            esp_pcm_enc_config_t *pcm_enc_cfg = (esp_pcm_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(pcm_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_OPUS: {
            esp_opus_enc_config_t *opus_enc_cfg = (esp_opus_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(opus_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_ADPCM: {
            esp_adpcm_enc_config_t *adpcm_enc_cfg = (esp_adpcm_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(adpcm_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_LC3: {
            esp_lc3_enc_config_t *lc3_enc_cfg = (esp_lc3_enc_config_t *)enc_cfg->cfg;
            SET_ENC_BASIC_INFO(lc3_enc_cfg, info);
            break;
        }
        case ESP_AUDIO_TYPE_SBC: {
            esp_sbc_enc_config_t *sbc_enc_cfg = (esp_sbc_enc_config_t *)enc_cfg->cfg;
            sbc_enc_cfg->sample_rate = info->sample_rates;
            sbc_enc_cfg->bits_per_sample = info->bits;
            if (info->channels == 1) {
                sbc_enc_cfg->ch_mode = ESP_SBC_CH_MODE_MONO;
            } else if (info->channels == 2) {
                sbc_enc_cfg->ch_mode = ((sbc_enc_cfg->ch_mode > ESP_SBC_CH_MODE_MONO && sbc_enc_cfg->ch_mode <= ESP_SBC_CH_MODE_JOINT_STEREO) ? (sbc_enc_cfg->ch_mode) : (ESP_SBC_CH_MODE_DUAL));
            } else {
                sbc_enc_cfg->ch_mode = ESP_SBC_CH_MODE_INVALID;
            }
            break;
        }
        default:
            break;
    }
}

static esp_gmf_err_t audio_enc_set_subcfg(esp_audio_enc_config_t *enc_cfg, void *sub_cfg, int32_t sub_cfg_sz)
{
    enc_cfg->cfg = esp_gmf_oal_calloc(1, sub_cfg_sz);
    ESP_GMF_MEM_CHECK(TAG, enc_cfg->cfg, return ESP_GMF_ERR_MEMORY_LACK;);
    enc_cfg->cfg_sz = sub_cfg_sz;
    memcpy(enc_cfg->cfg, sub_cfg, sub_cfg_sz);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t audio_enc_reconfig_enc_by_sound_info(esp_gmf_element_handle_t handle, esp_gmf_info_sound_t *info)
{
    esp_audio_enc_config_t *cfg = (esp_audio_enc_config_t *)OBJ_GET_CFG(handle);
    if (cfg == NULL) {
        cfg = esp_gmf_oal_calloc(1, sizeof(esp_audio_enc_config_t));
        ESP_GMF_MEM_VERIFY(TAG, cfg, return ESP_GMF_ERR_MEMORY_LACK, "audio encoder configuration", sizeof(esp_audio_enc_config_t));
        esp_gmf_obj_set_config(handle, cfg, sizeof(esp_audio_enc_config_t));
    }
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    bool same_type = (cfg->type == info->format_id) ? true : false;
    // free sub cfg first
    if (cfg->cfg && (same_type == false)) {
        esp_gmf_oal_free(cfg->cfg);
        cfg->cfg = NULL;
        cfg->cfg_sz = 0;
    }
    cfg->type = info->format_id;
    if (same_type == true && (cfg->cfg != NULL)) {
        audio_enc_change_audio_info(cfg, info);
        return ESP_GMF_ERR_OK;
    }
    switch (info->format_id) {
        case ESP_AUDIO_TYPE_AAC: {
            esp_aac_enc_config_t aac_enc_cfg = ESP_AAC_ENC_CONFIG_DEFAULT();
            SET_ENC_BASIC_INFO(&aac_enc_cfg, info);
            aac_enc_cfg.bitrate = info->bitrate;
            ret = audio_enc_set_subcfg(cfg, &aac_enc_cfg, sizeof(esp_aac_enc_config_t));
            break;
        }
        case ESP_AUDIO_TYPE_AMRNB: {
            esp_amrnb_enc_config_t amr_enc_cfg = ESP_AMRNB_ENC_CONFIG_DEFAULT();
            SET_ENC_BASIC_INFO(&amr_enc_cfg, info);
            amr_enc_cfg.bitrate_mode = info->bitrate;
            ret = audio_enc_set_subcfg(cfg, &amr_enc_cfg, sizeof(esp_amrnb_enc_config_t));
            break;
        }
        case ESP_AUDIO_TYPE_AMRWB: {
            esp_amrnb_enc_config_t amr_enc_cfg = ESP_AMRNB_ENC_CONFIG_DEFAULT();
            SET_ENC_BASIC_INFO(&amr_enc_cfg, info);
            amr_enc_cfg.bitrate_mode = info->bitrate;
            ret = audio_enc_set_subcfg(cfg, &amr_enc_cfg, sizeof(esp_amrnb_enc_config_t));
            break;
        }
        case ESP_AUDIO_TYPE_G711A: {
            esp_g711_enc_config_t g711_enc_cfg = ESP_G711_ENC_CONFIG_DEFAULT();
            SET_ENC_BASIC_INFO(&g711_enc_cfg, info);
            ret = audio_enc_set_subcfg(cfg, &g711_enc_cfg, sizeof(esp_g711_enc_config_t));
            break;
        }
        case ESP_AUDIO_TYPE_G711U: {
            esp_g711_enc_config_t g711_enc_cfg = ESP_G711_ENC_CONFIG_DEFAULT();
            SET_ENC_BASIC_INFO(&g711_enc_cfg, info);
            ret = audio_enc_set_subcfg(cfg, &g711_enc_cfg, sizeof(esp_g711_enc_config_t));
            break;
        }
        case ESP_AUDIO_TYPE_ALAC: {
            esp_alac_enc_config_t alac_enc_cfg = ESP_ALAC_ENC_CONFIG_DEFAULT();
            SET_ENC_BASIC_INFO(&alac_enc_cfg, info);
            ret = audio_enc_set_subcfg(cfg, &alac_enc_cfg, sizeof(esp_alac_enc_config_t));
            break;
        }
        case ESP_AUDIO_TYPE_PCM: {
            esp_pcm_enc_config_t pcm_enc_cfg = ESP_PCM_ENC_CONFIG_DEFAULT();
            SET_ENC_BASIC_INFO(&pcm_enc_cfg, info);
            ret = audio_enc_set_subcfg(cfg, &pcm_enc_cfg, sizeof(esp_pcm_enc_config_t));
            break;
        }
        case ESP_AUDIO_TYPE_OPUS: {
            esp_opus_enc_config_t opus_enc_cfg = ESP_OPUS_ENC_CONFIG_DEFAULT();
            SET_ENC_BASIC_INFO(&opus_enc_cfg, info);
            opus_enc_cfg.bitrate = info->bitrate;
            ret = audio_enc_set_subcfg(cfg, &opus_enc_cfg, sizeof(esp_opus_enc_config_t));
            break;
        }
        case ESP_AUDIO_TYPE_ADPCM: {
            esp_adpcm_enc_config_t adpcm_enc_cfg = ESP_ADPCM_ENC_CONFIG_DEFAULT();
            SET_ENC_BASIC_INFO(&adpcm_enc_cfg, info);
            ret = audio_enc_set_subcfg(cfg, &adpcm_enc_cfg, sizeof(esp_adpcm_enc_config_t));
            break;
        }
        case ESP_AUDIO_TYPE_LC3: {
            esp_lc3_enc_config_t lc3_enc_cfg = ESP_LC3_ENC_CONFIG_DEFAULT();
            SET_ENC_BASIC_INFO(&lc3_enc_cfg, info);
            ret = audio_enc_set_subcfg(cfg, &lc3_enc_cfg, sizeof(esp_lc3_enc_config_t));
            break;
        }
        case ESP_AUDIO_TYPE_SBC: {
            esp_sbc_enc_config_t sbc_enc_cfg = ESP_SBC_STD_ENC_CONFIG_DEFAULT();
            sbc_enc_cfg.sample_rate = info->sample_rates;
            sbc_enc_cfg.bits_per_sample = info->bits;
            if (info->channels == 1) {
                sbc_enc_cfg.ch_mode = ESP_SBC_CH_MODE_MONO;
            } else if (info->channels == 2) {
                sbc_enc_cfg.ch_mode = ESP_SBC_CH_MODE_DUAL;
            } else {
                sbc_enc_cfg.ch_mode = ESP_SBC_CH_MODE_INVALID;
            }
            ret = audio_enc_set_subcfg(cfg, &sbc_enc_cfg, sizeof(esp_sbc_enc_config_t));
            break;
        }
        default:
            ESP_LOGE(TAG, "Not support for encoder, %ld", info->format_id);
            cfg->type = ESP_AUDIO_TYPE_UNSUPPORT;
            return ESP_GMF_ERR_NOT_SUPPORT;
    }
    return ((ret == ESP_GMF_ERR_OK) ? (ESP_GMF_ERR_OK) : (ret));
}

static esp_gmf_job_err_t gmf_audio_enc_acquire_in(esp_gmf_audio_enc_t *audio_enc, esp_gmf_port_handle_t in_port, esp_gmf_payload_t **in_load)
{
    bool needed_load = false;
    esp_gmf_job_err_t job_ret = ESP_GMF_JOB_ERR_OK;
    esp_gmf_cache_ready_for_load(audio_enc->cached_payload, &needed_load);
    if (needed_load) {
        esp_gmf_err_io_t load_ret = esp_gmf_port_acquire_in(in_port, &audio_enc->origin_in_load, ESP_GMF_ELEMENT_GET(audio_enc)->in_attr.data_size, in_port->wait_ticks);
        ESP_GMF_PORT_ACQUIRE_IN_CHECK(TAG, load_ret, job_ret, return job_ret);
        int cache_size = 0;
        esp_gmf_cache_get_cached_size(audio_enc->cached_payload, &cache_size);
        audio_enc->cur_pts = audio_enc->origin_in_load->pts - GMF_AUDIO_CALC_PTS(cache_size, audio_enc->parent.snd_info.sample_rates, audio_enc->parent.snd_info.channels, audio_enc->parent.snd_info.bits);
        esp_gmf_cache_load(audio_enc->cached_payload, audio_enc->origin_in_load);
    }
    esp_gmf_err_t ret = esp_gmf_cache_acquire(audio_enc->cached_payload, ESP_GMF_ELEMENT_GET(audio_enc)->in_attr.data_size, in_load);
    if (ret != ESP_GMF_ERR_OK) {
        job_ret = ((ret == ESP_GMF_ERR_NOT_ENOUGH) ? (ESP_GMF_JOB_ERR_CONTINUE) : (ESP_GMF_JOB_ERR_FAIL));
    }
    return job_ret;
}

static esp_gmf_err_t esp_gmf_audio_enc_new(void *cfg, esp_gmf_obj_handle_t *handle)
{
    return esp_gmf_audio_enc_init(cfg, (esp_gmf_element_handle_t *)handle);
}

static esp_gmf_job_err_t esp_gmf_audio_enc_open(esp_gmf_element_handle_t self, void *para)
{
    esp_gmf_audio_enc_t *enc = (esp_gmf_audio_enc_t *)self;
    esp_audio_enc_config_t *enc_cfg = (esp_audio_enc_config_t *)OBJ_GET_CFG(enc);
    ESP_GMF_CHECK(TAG, enc_cfg, {return ESP_GMF_JOB_ERR_FAIL;}, "There is no encoder configuration");
    esp_audio_err_t ret = esp_audio_enc_open(enc_cfg, &enc->audio_enc_hd);
    ESP_GMF_CHECK(TAG, enc->audio_enc_hd, {return ESP_GMF_JOB_ERR_FAIL;}, "Failed to create audio encoder handle");
    if (esp_audio_enc_get_frame_size(enc->audio_enc_hd, &ESP_GMF_ELEMENT_GET(enc)->in_attr.data_size, &ESP_GMF_ELEMENT_GET(enc)->out_attr.data_size) != ESP_AUDIO_ERR_OK) {
        ESP_LOGE(TAG, "Failed to obtain frame size, ret: %d", ret);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    esp_gmf_port_enable_payload_share(ESP_GMF_ELEMENT_GET(self)->in, false);
    esp_gmf_cache_new(ESP_GMF_ELEMENT_GET(enc)->in_attr.data_size, &enc->cached_payload);
    ESP_GMF_CHECK(TAG, enc->cached_payload, {return ESP_GMF_JOB_ERR_FAIL;}, "Failed to new a cached payload on open");
    esp_audio_enc_info_t enc_info = {0};
    esp_audio_enc_get_info(enc->audio_enc_hd, &enc_info);
    GMF_AUDIO_UPDATE_SND_INFO(self, enc_info.sample_rate, enc_info.bits_per_sample, enc_info.channel);
    ESP_LOGI(TAG, "Open, type:%s, acquire in frame: %d, out frame: %d", esp_audio_codec_get_name(enc_cfg->type), ESP_GMF_ELEMENT_GET(enc)->in_attr.data_size, ESP_GMF_ELEMENT_GET(enc)->out_attr.data_size);
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_job_err_t esp_gmf_audio_enc_process(esp_gmf_element_handle_t self, void *para)
{
    // Get audio encoder instance and initialize variables
    esp_gmf_audio_enc_t *audio_enc = (esp_gmf_audio_enc_t *)self;
    esp_gmf_job_err_t out_len = ESP_GMF_JOB_ERR_OK;
    esp_audio_err_t ret = ESP_AUDIO_ERR_OK;
    esp_gmf_port_handle_t in_port = ESP_GMF_ELEMENT_GET(self)->in;
    esp_gmf_port_handle_t out_port = ESP_GMF_ELEMENT_GET(self)->out;
    esp_gmf_payload_t *out_load = NULL;
    esp_audio_enc_in_frame_t enc_in_frame = {0};
    esp_audio_enc_out_frame_t enc_out_frame = {0};
    esp_gmf_err_io_t load_ret = 0;
    bool needed_load = false;
    esp_gmf_payload_t *in_load = NULL;
    out_len = gmf_audio_enc_acquire_in(audio_enc, in_port, &in_load);
    if (out_len != ESP_GMF_JOB_ERR_OK) {
        goto __audio_enc_release;
    }
    ESP_LOGD(TAG, "Acq cache, buf:%p, vld:%d, len:%d, done:%d", in_load->buf, in_load->valid_size, in_load->buf_length, in_load->is_done);
    // Acquire output buffer for encoded data and verify output buffer size
    load_ret = esp_gmf_port_acquire_out(out_port, &out_load, ESP_GMF_ELEMENT_GET(audio_enc)->out_attr.data_size, ESP_GMF_MAX_DELAY);
    ESP_GMF_PORT_ACQUIRE_OUT_CHECK(TAG, load_ret, out_len, {goto __audio_enc_release;});
    if (out_load->buf_length < (ESP_GMF_ELEMENT_GET(audio_enc)->out_attr.data_size)) {
        ESP_LOGE(TAG, "The out payload valid size(%d) is smaller than wanted size(%d)",
                 out_load->buf_length, (ESP_GMF_ELEMENT_GET(audio_enc)->out_attr.data_size));
        out_len = ESP_GMF_JOB_ERR_FAIL;
        goto __audio_enc_release;
    }
    // Check if have enough input data for processing
    if (in_load->valid_size != ESP_GMF_ELEMENT_GET(audio_enc)->in_attr.data_size) {
        if (in_load->is_done == true) {
            out_len = ESP_GMF_JOB_ERR_DONE;
            out_load->valid_size = 0;
            out_load->is_done = in_load->is_done;
            ESP_LOGD(TAG, "Return done, line:%d", __LINE__);
            goto __audio_enc_release;
        } else {
            out_len = ESP_GMF_JOB_ERR_CONTINUE;
            ESP_LOGD(TAG, "Return Continue, line:%d", __LINE__);
            goto __audio_enc_release;
        }
    }
    // Perform audio encoding with mutex protection
    enc_in_frame.buffer = in_load->buf;
    enc_in_frame.len = in_load->valid_size;
    enc_out_frame.buffer = out_load->buf;
    enc_out_frame.len = ESP_GMF_ELEMENT_GET(audio_enc)->out_attr.data_size;
    esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)self)->lock);
    ret = esp_audio_enc_process(audio_enc->audio_enc_hd, &enc_in_frame, &enc_out_frame);
    esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)self)->lock);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {out_len = ESP_GMF_JOB_ERR_FAIL; goto __audio_enc_release;}, "Audio encoder process error %d", ret);
    out_load->valid_size = enc_out_frame.encoded_bytes;
    out_load->is_done = in_load->is_done;
    out_load->pts = audio_enc->cur_pts;
    audio_enc->cur_pts += GMF_AUDIO_CALC_PTS(enc_in_frame.len, audio_enc->parent.snd_info.sample_rates, audio_enc->parent.snd_info.channels, audio_enc->parent.snd_info.bits);
    // Handle end of stream
    if (in_load->is_done) {
        ESP_LOGW(TAG, "Got done, out size: %d", out_load->valid_size);
        out_len = ESP_GMF_JOB_ERR_DONE;
    }
    // Check if need to truncate
    esp_gmf_cache_ready_for_load(audio_enc->cached_payload, &needed_load);
    if (needed_load == false) {
        out_len = ESP_GMF_JOB_ERR_TRUNCATE;
        int cached_size = 0;
        esp_gmf_cache_get_cached_size(audio_enc->cached_payload, &cached_size);
        ESP_LOGD(TAG, "Return TRUNCATE, reminder in size: %d", cached_size);
    }
__audio_enc_release:
    // Cleanup resources
    esp_gmf_cache_release(audio_enc->cached_payload, in_load);
    if (out_load != NULL) {
        load_ret = esp_gmf_port_release_out(out_port, out_load, out_port->wait_ticks);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "OUT port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
    }
    if ((audio_enc->origin_in_load != NULL) && (out_len != ESP_GMF_JOB_ERR_TRUNCATE)) {
        load_ret = esp_gmf_port_release_in(in_port, audio_enc->origin_in_load, in_port->wait_ticks);
        if ((load_ret < ESP_GMF_IO_OK) && (load_ret != ESP_GMF_IO_ABORT)) {
            ESP_LOGE(TAG, "IN port release error, ret:%d", load_ret);
            out_len = ESP_GMF_JOB_ERR_FAIL;
        }
        audio_enc->origin_in_load = NULL;
    }
    return out_len;
}

static esp_gmf_job_err_t esp_gmf_audio_enc_close(esp_gmf_element_handle_t self, void *para)
{
    ESP_LOGD(TAG, "Closed, %p", self);
    esp_gmf_audio_enc_t *enc = (esp_gmf_audio_enc_t *)self;
    if (enc->cached_payload) {
        esp_gmf_cache_delete(enc->cached_payload);
        enc->cached_payload = NULL;
    }
    if (enc->audio_enc_hd != NULL) {
        esp_audio_enc_close(enc->audio_enc_hd);
        enc->audio_enc_hd = NULL;
    }
    return ESP_GMF_JOB_ERR_OK;
}

static esp_gmf_err_t audio_enc_received_event_handler(esp_gmf_event_pkt_t *evt, void *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, ctx, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, evt, { return ESP_GMF_ERR_INVALID_ARG;});
    if ((evt->type != ESP_GMF_EVT_TYPE_REPORT_INFO)
        || (evt->sub != ESP_GMF_INFO_SOUND)
        || (evt->payload == NULL)) {
        return ESP_GMF_ERR_OK;
    }
    esp_gmf_element_handle_t self = (esp_gmf_element_handle_t)ctx;
    esp_gmf_element_handle_t el = evt->from;
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_NONE;
    esp_gmf_element_get_state(self, &state);
    if (state < ESP_GMF_EVENT_STATE_OPENING) {
        esp_audio_enc_config_t *enc_cfg = (esp_audio_enc_config_t *)OBJ_GET_CFG(self);
        ESP_GMF_NULL_CHECK(TAG, enc_cfg, return ESP_GMF_ERR_FAIL);
        esp_gmf_info_sound_t *info = (esp_gmf_info_sound_t *)evt->payload;
        audio_enc_change_audio_info(enc_cfg, info);
        ESP_LOGD(TAG, "RECV info, from: %s-%p, next: %p, self: %s-%p, type: %x, state: %s, rate: %d, ch: %d, bits: %d",
                 OBJ_GET_TAG(el), el, esp_gmf_node_for_next((esp_gmf_node_t *)el), OBJ_GET_TAG(self), self, evt->type,
                 esp_gmf_event_get_state_str(state), info->sample_rates, info->channels, info->bits);
        if (state == ESP_GMF_EVENT_STATE_NONE) {
            esp_gmf_element_set_state(self, ESP_GMF_EVENT_STATE_INITIALIZED);
        }
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t esp_gmf_audio_enc_destroy(esp_gmf_element_handle_t self)
{
    ESP_LOGD(TAG, "Destroyed, %p", self);
    esp_gmf_audio_enc_t *enc = (esp_gmf_audio_enc_t *)self;
    free_esp_gmf_audio_enc_cfg(OBJ_GET_CFG(self));
    esp_gmf_audio_el_deinit(self);
    esp_gmf_oal_free(enc);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_enc_caps_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_cap_t *caps = NULL;
    esp_gmf_cap_t dec_caps = {0};
    dec_caps.cap_eightcc = ESP_GMF_CAPS_AUDIO_ENCODER;
    dec_caps.attr_fun = NULL;
    int ret = esp_gmf_cap_append(&caps, &dec_caps);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to create capability");

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->caps = caps;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _load_enc_methods_func(esp_gmf_element_handle_t handle)
{
    esp_gmf_method_t *method = NULL;
    esp_gmf_args_desc_t *set_args = NULL;
    esp_gmf_args_desc_t *get_args = NULL;

    esp_gmf_err_t ret = esp_gmf_args_desc_append(&set_args, AMETHOD_ARG(ENCODER, SET_BITRATE, BITRATE),
                                                 ESP_GMF_ARGS_TYPE_INT32, sizeof(uint32_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append bitrate argument");
    ret = esp_gmf_method_append(&method, AMETHOD(ENCODER, SET_BITRATE), __audio_enc_set_bitrate, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register %s method", AMETHOD(ENCODER, SET_BITRATE));
    ret = esp_gmf_args_desc_copy(set_args, &get_args);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to copy argument");
    ret = esp_gmf_method_append(&method, AMETHOD(ENCODER, GET_BITRATE), __audio_enc_get_bitrate, get_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register %s method", AMETHOD(ENCODER, GET_BITRATE));

    get_args = NULL;
    set_args = NULL;
    ret = esp_gmf_args_desc_append(&get_args, AMETHOD_ARG(ENCODER, GET_FRAME_SZ, INSIZE),
                                   ESP_GMF_ARGS_TYPE_INT32, sizeof(uint32_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append insize argument");
    ret = esp_gmf_args_desc_append(&get_args, AMETHOD_ARG(ENCODER, GET_FRAME_SZ, OUTSIZE), ESP_GMF_ARGS_TYPE_INT32,
                                   sizeof(uint32_t), sizeof(uint32_t));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append outsize argument");
    ret = esp_gmf_method_append(&method, AMETHOD(ENCODER, GET_FRAME_SZ), __audio_enc_get_frame_size, get_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register %s method", AMETHOD(ENCODER, GET_FRAME_SZ));

    set_args = NULL;
    esp_gmf_args_desc_t *sndinfo_args = NULL;
    ret = esp_gmf_args_desc_append(&sndinfo_args, AMETHOD_ARG(ENCODER, RECONFIG_BY_SND_INFO, INFO_TYPE), ESP_GMF_ARGS_TYPE_UINT32,
                                   sizeof(uint32_t), offsetof(esp_gmf_info_sound_t, format_id));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append type argument");
    ret = esp_gmf_args_desc_append(&sndinfo_args, AMETHOD_ARG(ENCODER, RECONFIG_BY_SND_INFO, INFO_SAMPLERATE), ESP_GMF_ARGS_TYPE_INT32,
                                   sizeof(int32_t), offsetof(esp_gmf_info_sound_t, sample_rates));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append sample_rates argument");
    ret = esp_gmf_args_desc_append(&sndinfo_args, AMETHOD_ARG(ENCODER, RECONFIG_BY_SND_INFO, INFO_BITRATE), ESP_GMF_ARGS_TYPE_INT32,
                                   sizeof(int32_t), offsetof(esp_gmf_info_sound_t, bitrate));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append bitrate argument");
    ret = esp_gmf_args_desc_append(&sndinfo_args, AMETHOD_ARG(ENCODER, RECONFIG_BY_SND_INFO, INFO_CHANNEL), ESP_GMF_ARGS_TYPE_INT8,
                                   sizeof(int8_t), 12);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append channels argument");
    ret = esp_gmf_args_desc_append(&sndinfo_args, AMETHOD_ARG(ENCODER, RECONFIG_BY_SND_INFO, INFO_BITS), ESP_GMF_ARGS_TYPE_INT8,
                                   sizeof(int8_t), 13);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append bits argument");
    ret = esp_gmf_args_desc_append_array(&set_args, AMETHOD_ARG(ENCODER, RECONFIG_BY_SND_INFO, INFO), sndinfo_args,
                                         sizeof(esp_gmf_info_sound_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append sound info argument");
    ret = esp_gmf_method_append(&method, AMETHOD(ENCODER, RECONFIG_BY_SND_INFO), __audio_enc_reconfig_by_sound_info, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register %s method", AMETHOD(ENCODER, RECONFIG_BY_SND_INFO));

    set_args = NULL;
    esp_gmf_args_desc_t *reconfig_args = NULL;
    ret = esp_gmf_args_desc_append(&reconfig_args, AMETHOD_ARG(ENCODER, RECONFIG, CFG_TYPE), ESP_GMF_ARGS_TYPE_INT32,
                                   sizeof(int32_t), offsetof(esp_audio_enc_config_t, type));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append type argument");
    ret = esp_gmf_args_desc_append(&reconfig_args, AMETHOD_ARG(ENCODER, RECONFIG, CFG_SUBCFGPTR), ESP_GMF_ARGS_TYPE_INT32,
                                   sizeof(int32_t), offsetof(esp_audio_enc_config_t, cfg));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append cfg argument");
    ret = esp_gmf_args_desc_append(&reconfig_args, AMETHOD_ARG(ENCODER, RECONFIG, CFG_SUBCFGSZ), ESP_GMF_ARGS_TYPE_UINT32,
                                   sizeof(uint32_t), offsetof(esp_audio_enc_config_t, cfg_sz));
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append cfg_sz argument");
    ret = esp_gmf_args_desc_append_array(&set_args, AMETHOD_ARG(ENCODER, RECONFIG, CFG), reconfig_args,
                                         sizeof(esp_audio_enc_config_t), 0);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to append argument");
    ret = esp_gmf_method_append(&method, AMETHOD(ENCODER, RECONFIG), __audio_enc_reconfig, set_args);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register %s method", AMETHOD(ENCODER, RECONFIG));

    esp_gmf_element_t *el = (esp_gmf_element_t *)handle;
    el->method = method;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_audio_enc_get_frame_size(esp_gmf_element_handle_t handle, uint32_t *in_size, uint32_t *out_size)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_audio_enc_config_t *cfg = (esp_audio_enc_config_t *)OBJ_GET_CFG(handle);
    ESP_GMF_NULL_CHECK(TAG, cfg, return ESP_GMF_ERR_FAIL);
    esp_gmf_audio_enc_t *enc = (esp_gmf_audio_enc_t *)handle;
    esp_audio_err_t ret = -1;
    if (enc->audio_enc_hd == NULL) {
        esp_audio_enc_frame_info_t frame_info = {0};
        ret = esp_audio_enc_get_frame_info_by_cfg(cfg, &frame_info);
        if (ret != ESP_AUDIO_ERR_OK) {
            return ESP_GMF_ERR_FAIL;
        }
        *in_size = (uint32_t)frame_info.in_frame_size;
        *out_size = (uint32_t)frame_info.out_frame_size;
    } else {
        ret = esp_audio_enc_get_frame_size(enc->audio_enc_hd, (int *)in_size, (int *)out_size);
    }
    return ((ret == ESP_AUDIO_ERR_OK) ? (ESP_GMF_ERR_OK) : (ESP_GMF_ERR_FAIL));
}

esp_gmf_err_t esp_gmf_audio_enc_set_bitrate(esp_gmf_element_handle_t handle, uint32_t bitrate)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_audio_enc_t *enc = (esp_gmf_audio_enc_t *)handle;
    if (enc->audio_enc_hd) {
        esp_gmf_oal_mutex_lock(((esp_gmf_audio_element_t *)handle)->lock);
        esp_audio_err_t ret = esp_audio_enc_set_bitrate(enc->audio_enc_hd, (int)bitrate);
        esp_gmf_oal_mutex_unlock(((esp_gmf_audio_element_t *)handle)->lock);
        return ((ret == ESP_AUDIO_ERR_OK) ? (ESP_GMF_ERR_OK) : (ESP_GMF_ERR_FAIL));
    }
    enc->bitrate = bitrate;
    return ESP_GMF_JOB_ERR_OK;
}

esp_gmf_err_t esp_gmf_audio_enc_get_bitrate(esp_gmf_element_handle_t handle, uint32_t *bitrate)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_audio_enc_t *enc = (esp_gmf_audio_enc_t *)handle;
    if (enc->audio_enc_hd) {
        esp_audio_enc_info_t enc_info = {0};
        esp_audio_err_t ret = esp_audio_enc_get_info(enc->audio_enc_hd, &enc_info);
        *bitrate = enc_info.bitrate;
        return ((ret == ESP_AUDIO_ERR_OK) ? (ESP_GMF_ERR_OK) : (ESP_GMF_ERR_FAIL));
    }
    *bitrate = enc->bitrate;
    return ESP_GMF_JOB_ERR_OK;
}

esp_gmf_err_t esp_gmf_audio_enc_reconfig(esp_gmf_element_handle_t handle, esp_audio_enc_config_t *config)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    ESP_GMF_NULL_CHECK(TAG, config, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_NONE;
    esp_gmf_element_get_state(handle, &state);
    if (state < ESP_GMF_EVENT_STATE_OPENING) {
        esp_audio_enc_config_t *new_config = NULL;
        esp_gmf_err_t ret = dupl_esp_gmf_audio_enc_cfg(config, &new_config);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, {return ret;}, "Failed to duplicate config");
        free_esp_gmf_audio_enc_cfg(OBJ_GET_CFG(handle));
        esp_gmf_obj_set_config(handle, new_config, sizeof(esp_audio_enc_config_t));
        return ESP_GMF_ERR_OK;
    } else {
        ESP_LOGE(TAG, "Failed to reconfig encoder due to invalid state: %s", esp_gmf_event_get_state_str(state));
        return ESP_GMF_ERR_FAIL;
    }
}

esp_gmf_err_t esp_gmf_audio_enc_reconfig_by_sound_info(esp_gmf_element_handle_t handle, esp_gmf_info_sound_t *info)
{
    ESP_GMF_NULL_CHECK(TAG, handle, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_NONE;
    esp_gmf_element_get_state(handle, &state);
    if (state < ESP_GMF_EVENT_STATE_OPENING) {
        esp_gmf_err_t ret = audio_enc_reconfig_enc_by_sound_info(handle, info);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to reconfig encoder by sound information");
        return ESP_GMF_ERR_OK;
    } else {
        ESP_LOGE(TAG, "Failed to reconfig encoder due to invalid state: %s", esp_gmf_event_get_state_str(state));
        return ESP_GMF_ERR_FAIL;
    }
}

esp_gmf_err_t esp_gmf_audio_enc_init(esp_audio_enc_config_t *config, esp_gmf_element_handle_t *handle)
{
    ESP_GMF_NULL_CHECK(TAG, handle, {return ESP_GMF_ERR_INVALID_ARG;});
    *handle = NULL;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_audio_enc_t *audio_enc = esp_gmf_oal_calloc(1, sizeof(esp_gmf_audio_enc_t));
    ESP_GMF_MEM_VERIFY(TAG, audio_enc, {return ESP_GMF_ERR_MEMORY_LACK;}, "audio encoder", sizeof(esp_gmf_audio_enc_t));
    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)audio_enc;
    obj->new_obj = esp_gmf_audio_enc_new;
    obj->del_obj = esp_gmf_audio_enc_destroy;
    esp_audio_enc_config_t *cfg = NULL;
    if (config) {
        dupl_esp_gmf_audio_enc_cfg(config, &cfg);
    } else {
        esp_audio_enc_config_t dcfg = DEFAULT_ESP_GMF_AUDIO_ENC_CONFIG();
        dupl_esp_gmf_audio_enc_cfg(&dcfg, &cfg);
    }
    ESP_GMF_CHECK(TAG, cfg, ret = ESP_GMF_ERR_MEMORY_LACK; goto ES_ENC_FAIL;, "Failed to allocate audio encoder configuration");
    esp_gmf_obj_set_config(obj, cfg, sizeof(esp_audio_enc_config_t));
    ret = esp_gmf_obj_set_tag(obj, "aud_enc");
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ES_ENC_FAIL, "Failed to set obj tag");
    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, 0, 0,
        ESP_GMF_PORT_TYPE_BLOCK | ESP_GMF_PORT_TYPE_BYTE, ESP_GMF_ELEMENT_PORT_DATA_SIZE_DEFAULT);
    el_cfg.dependency = true;
    ret = esp_gmf_audio_el_init(audio_enc, &el_cfg);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto ES_ENC_FAIL, "Failed to initialize audio encoder element");
    audio_enc->parent.base.ops.open = esp_gmf_audio_enc_open;
    audio_enc->parent.base.ops.process = esp_gmf_audio_enc_process;
    audio_enc->parent.base.ops.close = esp_gmf_audio_enc_close;
    audio_enc->parent.base.ops.event_receiver = audio_enc_received_event_handler;
    audio_enc->parent.base.ops.load_caps = _load_enc_caps_func;
    audio_enc->parent.base.ops.load_methods = _load_enc_methods_func;
    *handle = obj;
    ESP_LOGD(TAG, "Initialization, %s-%p", OBJ_GET_TAG(obj), obj);
    return ret;
ES_ENC_FAIL:
    esp_gmf_audio_enc_destroy(obj);
    return ret;
}
