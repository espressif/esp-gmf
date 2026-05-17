/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_audio_dec_default.h"
#include "esp_player_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Default AAC decoder configuration
 *
 * @note  This configuration is suitable for AAC with ADTS header
 */
#define ESP_PLAYER_AAC_DEC_CFG_DEFAULT()  {  \
    .sample_rate     = 48000,                \
    .channel         = 2,                    \
    .bits_per_sample = 16,                   \
    .no_adts_header  = false,                \
    .aac_plus_enable = false,                \
}

/**
 * @brief  Default OPUS decoder configuration
 *
 * @note  This configuration uses 20ms frame duration and stereo channel
 */
#define ESP_PLAYER_OPUS_DEC_CFG_DEFAULT()  {              \
    .sample_rate    = 48000,                              \
    .channel        = 2,                                  \
    .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_20_MS,  \
    .self_delimited = false,                              \
}

/**
 * @brief  Default LC3 decoder configuration
 *
 * @note  This configuration is suitable for high quality audio
 */
#define ESP_PLAYER_LC3_DEC_CFG_DEFAULT()  {  \
    .sample_rate     = 48000,                \
    .bits_per_sample = 16,                   \
    .channel         = 2,                    \
    .frame_dms       = 100,                  \
    .nbyte           = 120,                  \
    .is_cbr          = true,                 \
    .len_prefixed    = false,                \
    .enable_plc      = true,                 \
}

/**
 * @brief  Default SBC decoder configuration
 *
 * @note  This configuration uses standard SBC mode with stereo output
 */
#define ESP_PLAYER_SBC_DEC_CFG_DEFAULT()  {  \
    .sbc_mode   = ESP_SBC_MODE_STD,          \
    .ch_num     = 2,                         \
    .enable_plc = true,                      \
}

/**
 * @brief  Default PCM decoder configuration
 *
 * @note  This configuration is suitable for 16-bit stereo PCM
 */
#define ESP_PLAYER_PCM_DEC_CFG_DEFAULT()  {  \
    .sample_rate     = 48000,                \
    .channel         = 2,                    \
    .bits_per_sample = 16,                   \
}

/**
 * @brief  Default G711 decoder configuration
 *
 * @note  This configuration is suitable for mono G711 audio
 */
#define ESP_PLAYER_G711_DEC_CFG_DEFAULT()  {  \
    .channel = 1,                             \
}

/**
 * @brief  Default ADPCM decoder configuration
 *
 * @note  This configuration is suitable for IMA-ADPCM with 4-bit samples
 */
#define ESP_PLAYER_ADPCM_DEC_CFG_DEFAULT()  {  \
    .sample_rate     = 8000,                   \
    .channel         = 1,                      \
    .bits_per_sample = 4,                      \
}

/**
 * @brief  Default VORBIS decoder configuration
 *
 * @note  This configuration requires headers to be provided externally
 */
#define ESP_PLAYER_VORBIS_DEC_CFG_DEFAULT()  {  \
    .info_header  = NULL,                       \
    .info_size    = 0,                          \
    .setup_header = NULL,                       \
    .setup_size   = 0,                          \
}

/**
 * @brief  Default ALAC decoder configuration
 *
 * @note  This configuration requires codec specific info to be provided externally
 */
#define ESP_PLAYER_ALAC_DEC_CFG_DEFAULT()  {  \
    .codec_spec_info = NULL,                  \
    .spec_info_len   = 0,                     \
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
