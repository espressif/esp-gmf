/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_gmf_pool.h"
#include "esp_gmf_err.h"

#include "esp_extractor.h"

#include "player_helper.h"
#include "player_extractor.h"

static const char *TAG = "ESP_PLAYER_HELPER";

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
esp_player_err_t player_dec_cfg_resolve(esp_player_format_t type, uint32_t *sz,
                                        esp_audio_simple_dec_type_t *dt, void *buf)
{
    if (sz == NULL || dt == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
#define ESP_PLAYER_DEC_SUBCFG_CASE(fcc, dec_t, cfg_t, def_macro)  {  \
    case (uint32_t)(fcc):                                            \
        *sz = sizeof(cfg_t);                                         \
        *dt = (dec_t);                                               \
        if (buf) {                                                   \
            cfg_t _v = def_macro;                                    \
            memcpy(buf, &_v, sizeof(_v));                            \
        }                                                            \
        return ESP_PLAYER_ERR_OK;                                    \
}
    switch ((uint32_t)type) {
        ESP_PLAYER_DEC_SUBCFG_CASE(ESP_AUDIO_SIMPLE_DEC_TYPE_AAC, ESP_AUDIO_SIMPLE_DEC_TYPE_AAC,
                                   esp_aac_dec_cfg_t, ESP_PLAYER_AAC_DEC_CFG_DEFAULT());
        ESP_PLAYER_DEC_SUBCFG_CASE(ESP_AUDIO_SIMPLE_DEC_TYPE_RAW_OPUS, ESP_AUDIO_SIMPLE_DEC_TYPE_RAW_OPUS,
                                   esp_opus_dec_cfg_t, ESP_PLAYER_OPUS_DEC_CFG_DEFAULT());
        ESP_PLAYER_DEC_SUBCFG_CASE(ESP_AUDIO_SIMPLE_DEC_TYPE_LC3, ESP_AUDIO_SIMPLE_DEC_TYPE_LC3,
                                   esp_lc3_dec_cfg_t, ESP_PLAYER_LC3_DEC_CFG_DEFAULT());
        ESP_PLAYER_DEC_SUBCFG_CASE(ESP_AUDIO_SIMPLE_DEC_TYPE_SBC, ESP_AUDIO_SIMPLE_DEC_TYPE_SBC,
                                   esp_sbc_dec_cfg_t, ESP_PLAYER_SBC_DEC_CFG_DEFAULT());
        ESP_PLAYER_DEC_SUBCFG_CASE(ESP_AUDIO_SIMPLE_DEC_TYPE_PCM, ESP_AUDIO_SIMPLE_DEC_TYPE_PCM,
                                   esp_pcm_dec_cfg_t, ESP_PLAYER_PCM_DEC_CFG_DEFAULT());
        ESP_PLAYER_DEC_SUBCFG_CASE(ESP_AUDIO_SIMPLE_DEC_TYPE_G711U, ESP_AUDIO_SIMPLE_DEC_TYPE_G711U,
                                   esp_g711_dec_cfg_t, ESP_PLAYER_G711_DEC_CFG_DEFAULT());
        ESP_PLAYER_DEC_SUBCFG_CASE(ESP_AUDIO_SIMPLE_DEC_TYPE_G711A, ESP_AUDIO_SIMPLE_DEC_TYPE_G711A,
                                   esp_g711_dec_cfg_t, ESP_PLAYER_G711_DEC_CFG_DEFAULT());
        ESP_PLAYER_DEC_SUBCFG_CASE(ESP_AUDIO_SIMPLE_DEC_TYPE_ADPCM, ESP_AUDIO_SIMPLE_DEC_TYPE_ADPCM,
                                   esp_adpcm_dec_cfg_t, ESP_PLAYER_ADPCM_DEC_CFG_DEFAULT());
        ESP_PLAYER_DEC_SUBCFG_CASE(ESP_AUDIO_SIMPLE_DEC_TYPE_VORBIS, ESP_AUDIO_SIMPLE_DEC_TYPE_VORBIS,
                                   esp_vorbis_dec_cfg_t, ESP_PLAYER_VORBIS_DEC_CFG_DEFAULT());
        ESP_PLAYER_DEC_SUBCFG_CASE(ESP_AUDIO_SIMPLE_DEC_TYPE_ALAC, ESP_AUDIO_SIMPLE_DEC_TYPE_ALAC,
                                   esp_alac_dec_cfg_t, ESP_PLAYER_ALAC_DEC_CFG_DEFAULT());
        default:
            return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
#undef ESP_PLAYER_DEC_SUBCFG_CASE
}
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
esp_player_err_t player_format_from_codec_name(const char *name, esp_player_format_t *format)
{
    if (name == NULL || name[0] == '\0' || format == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (strcasecmp(name, "aac") == 0) {
        *format = ESP_FOURCC_AAC;
    } else if (strcasecmp(name, "mp3") == 0) {
        *format = ESP_FOURCC_MP3;
    } else if (strcasecmp(name, "flac") == 0) {
        *format = ESP_FOURCC_FLAC;
    } else if (strcasecmp(name, "opus") == 0) {
        *format = ESP_AUDIO_SIMPLE_DEC_TYPE_RAW_OPUS;
    } else if (strcasecmp(name, "pcm") == 0) {
        *format = ESP_AUDIO_SIMPLE_DEC_TYPE_PCM;
    } else if (strcasecmp(name, "g711a") == 0 || strcasecmp(name, "alaw") == 0) {
        *format = ESP_AUDIO_SIMPLE_DEC_TYPE_G711A;
    } else if (strcasecmp(name, "g711u") == 0 || strcasecmp(name, "ulaw") == 0) {
        *format = ESP_AUDIO_SIMPLE_DEC_TYPE_G711U;
    } else if (strcasecmp(name, "adpcm") == 0) {
        *format = ESP_AUDIO_SIMPLE_DEC_TYPE_ADPCM;
    } else if (strcasecmp(name, "sbc") == 0) {
        *format = ESP_AUDIO_SIMPLE_DEC_TYPE_SBC;
    } else if (strcasecmp(name, "lc3") == 0) {
        *format = ESP_AUDIO_SIMPLE_DEC_TYPE_LC3;
    } else if (strcasecmp(name, "alac") == 0) {
        *format = ESP_AUDIO_SIMPLE_DEC_TYPE_ALAC;
    } else if (strcasecmp(name, "vorbis") == 0) {
        *format = ESP_AUDIO_SIMPLE_DEC_TYPE_VORBIS;
    } else if (strcasecmp(name, "amr") == 0 || strcasecmp(name, "amrnb") == 0) {
        *format = ESP_FOURCC_AMRNB;
    } else if (strcasecmp(name, "amrwb") == 0) {
        *format = ESP_FOURCC_AMRWB;
    } else if (strcasecmp(name, "wav") == 0) {
        *format = ESP_FOURCC_WAV;
    } else {
        ESP_LOGE(TAG, "Unsupported frame-mode codec \"%s\"", name);
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    return ESP_PLAYER_ERR_OK;
}
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

esp_player_err_t player_get_favor_type(const char *url, esp_player_format_t *format)
{
    if (url == NULL || format == NULL) {
        ESP_LOGE(TAG, "Invalid argument. url: %s, format: %p", url ? url : "(null)", format);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    const char *query = strchr(url, '?');
    size_t url_len = query ? (size_t)(query - url) : strlen(url);
    const char *dot = NULL;
    for (size_t i = url_len; i > 0; i--) {
        if (url[i - 1] == '.') {
            dot = url + i - 1;
            break;
        }
        if (url[i - 1] == '/') {
            break;
        }
    }
    if (dot == NULL) {
        ESP_LOGE(TAG, "Invalid argument. url: %s, format: %p", url, format);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    const char *ext = dot;
    if ((url_len - (size_t)(ext - url)) == 4 && strncasecmp(ext, ".pcm", 4) == 0) {
        *format = (esp_player_format_t)ESP_EXTRACTOR_TYPE_RAW;
        return ESP_PLAYER_ERR_OK;
    }
    if (strcasecmp(ext, ".avi") == 0) {
        *format = ESP_FOURCC_AVI;
    } else if (strcasecmp(ext, ".caf") == 0) {
        *format = ESP_FOURCC_CAF;
    } else if (strcasecmp(ext, ".flv") == 0) {
        *format = ESP_FOURCC_FLV;
    } else if (strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".mov") == 0 || strcasecmp(ext, ".m4a") == 0) {
        *format = ESP_FOURCC_MP4;
    } else if (strcasecmp(ext, ".ogg") == 0 || strcasecmp(ext, ".ogm") == 0) {
        *format = ESP_FOURCC_OGG;
    } else if (strcasecmp(ext, ".ts") == 0) {
        *format = ESP_FOURCC_TS;
    } else if (strcasecmp(ext, ".wav") == 0) {
        *format = ESP_FOURCC_WAV;
    } else if (strcasecmp(ext, ".aac") == 0) {
        *format = ESP_FOURCC_AAC;
    } else if (strcasecmp(ext, ".mp3") == 0) {
        *format = ESP_FOURCC_MP3;
    } else if (strcasecmp(ext, ".flac") == 0) {
        *format = ESP_FOURCC_FLAC;
    } else if (strcasecmp(ext, ".amr") == 0) {
        *format = ESP_FOURCC_AMRNB;
    } else {
        ESP_LOGI(TAG, "Unsupported format: %s", ext);
        *format = ESP_PLAYER_FORMAT_NONE;
    }
    return ESP_PLAYER_ERR_OK;
}
