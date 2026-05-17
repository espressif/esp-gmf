/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>

#include "esp_player.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Example playback configuration
 *
 * @note  `AUDIO_PLAYER_STREAM_NUM` is the number of concurrent ESP Player instances
 *        mixed into one DAC (1 .. AUDIO_PLAYER_PLAY_URL_MAX).
 *        `audio_player_play_urls[]` supplies one URL per stream index.
 *        `AUDIO_PLAYER_OUTPUT_VOLUME` is codec output volume (0-100).
 */
#define AUDIO_PLAYER_STREAM_NUM     1
#define AUDIO_PLAYER_PLAY_URL_MAX   4
#define AUDIO_PLAYER_OUTPUT_VOLUME  60

/**
 * @brief  Shared codec output format for esp_audio_render in this example.
 */
typedef struct {
    uint32_t  out_sample_rate;      /*!< Playback sample rate (Hz) */
    uint8_t   out_bits_per_sample;  /*!< Playback bit depth */
    uint8_t   out_channels;         /*!< Playback channel count */
} audio_render_settings_t;

#define AUDIO_RENDER_SETTINGS_DEFAULT()  {  \
    .out_sample_rate     = 44100,           \
    .out_bits_per_sample = 16,              \
    .out_channels        = 2,               \
}

static const char *const audio_player_play_urls[AUDIO_PLAYER_PLAY_URL_MAX] = {
    "/sdcard/test.mp3",
    "/sdcard/test.aac",
    "/sdcard/test.mp3",
    "/sdcard/test.aac",
};

#if (AUDIO_PLAYER_STREAM_NUM < 1) || (AUDIO_PLAYER_STREAM_NUM > AUDIO_PLAYER_PLAY_URL_MAX)
#error "AUDIO_PLAYER_STREAM_NUM must be in [1, AUDIO_PLAYER_PLAY_URL_MAX]"
#endif  /* (AUDIO_PLAYER_STREAM_NUM < 1) || (AUDIO_PLAYER_STREAM_NUM > AUDIO_PLAYER_PLAY_URL_MAX) */

/**
 * @brief  Register media defaults and bring up shared audio render (call once).
 *
 * @note  Creates `AUDIO_PLAYER_STREAM_NUM` render stream slot(s).
 *
 * @param[in]  render_settings  Output format; must not be NULL.
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Setup successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  `render_settings` is NULL
 *       - ESP_PLAYER_ERR_FAIL         Already initialized, or render/codec setup failed
 */
esp_player_err_t audio_player_setup(const audio_render_settings_t *render_settings);

/**
 * @brief  Create one ESP Player bound to the next free render stream.
 *
 * @note  Must be called after `audio_player_setup()`. At most
 *        `AUDIO_PLAYER_STREAM_NUM` players may exist until deleted.
 *
 * @param[out]  player  Receives the player handle (same usage as `esp_player_init()`).
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Created successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  `player` is NULL
 *       - ESP_PLAYER_ERR_FAIL         `audio_player_setup()` not called, no free stream,
 *                                     or `esp_player_init()` failed
 */
esp_player_err_t audio_player_new(esp_player_handle_t *player);

/**
 * @brief  Destroy one ESP Player created by `audio_player_new()`.
 *
 * @note  Only destroys the player handle. Shared render and media defaults are released
 *        by `audio_player_teardown()` after all players are deleted.
 *
 * @param[in]  player  Player handle to destroy.
 */
void audio_player_delete(esp_player_handle_t player);

/**
 * @brief  Tear down shared render, media defaults, and setup state.
 *
 * @note  Call after all players are deleted via `audio_player_delete()`. Resets internal
 *        state so `audio_player_setup()` may be invoked again.
 */
void audio_player_teardown(void);

/**
 * @brief  Playback URL for mixer stream index.
 *
 * @param[in]  stream_idx  Zero-based index in [0, AUDIO_PLAYER_STREAM_NUM)
 *
 * @return
 *       - URL  string, or NULL if stream_idx is out of range
 */
static inline const char *audio_player_play_url(int stream_idx)
{
    if (stream_idx < 0 || stream_idx >= AUDIO_PLAYER_STREAM_NUM) {
        return NULL;
    }
    return audio_player_play_urls[stream_idx];
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
