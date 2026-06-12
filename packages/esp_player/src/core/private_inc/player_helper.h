/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "sdkconfig.h"
#include "esp_gmf_io.h"
#include "esp_gmf_pool.h"

#include "esp_player_types.h"
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
#include "esp_audio_simple_dec.h"
#include "player_adec_defaults_cfg.h"
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
esp_player_err_t player_dec_cfg_resolve(esp_player_format_t type, uint32_t *sz,
                                        esp_audio_simple_dec_type_t *dt, void *buf);
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

/**
 * @brief  Get favorite type
 */
esp_player_err_t player_get_favor_type(const char *url, esp_player_format_t *format);

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
/**
 * @brief  Map a short codec name from fill/block URLs to a player format FourCC
 */
esp_player_err_t player_format_from_codec_name(const char *name, esp_player_format_t *format);
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
static inline bool is_simple_format_type(esp_player_format_t type)
{
    return ((uint32_t)type == ESP_AUDIO_SIMPLE_DEC_TYPE_MP3 || (uint32_t)type == ESP_AUDIO_SIMPLE_DEC_TYPE_AMRNB || (uint32_t)type == ESP_AUDIO_SIMPLE_DEC_TYPE_AMRWB || (uint32_t)type == ESP_AUDIO_SIMPLE_DEC_TYPE_FLAC || (uint32_t)type == ESP_AUDIO_SIMPLE_DEC_TYPE_WAV || (uint32_t)type == ESP_AUDIO_SIMPLE_DEC_TYPE_M4A || (uint32_t)type == ESP_FOURCC_MP4 || (uint32_t)type == ESP_AUDIO_SIMPLE_DEC_TYPE_TS);
}
#else
static inline bool is_simple_format_type(esp_player_format_t type)
{
    (void)type;
    return false;
}
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

#ifdef __cplusplus
}
#endif  /* __cplusplus */
