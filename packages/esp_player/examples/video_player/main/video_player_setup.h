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
 */
#define VIDEO_PLAYER_PLAY_URL       "/sdcard/test.mp4"
#define VIDEO_PLAYER_OUTPUT_VOLUME  60

/**
 * @brief  Shared audio/video render output configuration for the example.
 *
 * @note  LCD geometry comes from board manager. This struct only carries render
 *        parameters under application control.
 */
typedef struct {
    uint32_t  out_sample_rate;      /*!< Audio playback sample rate (Hz) */
    uint8_t   out_bits_per_sample;  /*!< Audio playback bit depth */
    uint8_t   out_channels;         /*!< Audio playback channel count */
    uint32_t  video_fps;            /*!< Video render frame rate */
} video_render_settings_t;

#define VIDEO_RENDER_SETTINGS_DEFAULT()  {  \
    .out_sample_rate     = 44100,           \
    .out_bits_per_sample = 16,              \
    .out_channels        = 2,               \
    .video_fps           = 30,              \
}

/**
 * @brief  Register media defaults and bring up shared A/V render (call once).
 *
 * @param[in]  render_settings  Audio/video output format; must not be NULL.
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Setup successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  `render_settings` is NULL
 *       - ESP_PLAYER_ERR_FAIL         Already initialized, or render/LCD setup failed
 */
esp_player_err_t video_player_setup(const video_render_settings_t *render_settings);

/**
 * @brief  Create one ESP Player bound to the shared A/V render.
 *
 * @note  Must be called after `video_player_setup()`. Only one player is supported.
 *
 * @param[out]  player  Receives the player handle (same usage as `esp_player_init()`).
 *
 * @return
 *       - ESP_PLAYER_ERR_OK           Created successful
 *       - ESP_PLAYER_ERR_INVALID_ARG  `player` is NULL
 *       - ESP_PLAYER_ERR_FAIL         `video_player_setup()` not called, player already exists,
 *                                     or `esp_player_init()` failed
 */
esp_player_err_t video_player_new(esp_player_handle_t *player);

/**
 * @brief  Destroy one ESP Player created by `video_player_new()`.
 *
 * @note  Only destroys the player handle. Shared render and media defaults are released
 *        by `video_player_teardown()` after the player is deleted.
 *
 * @param[in]  player  Player handle to destroy.
 */
void video_player_delete(esp_player_handle_t player);

/**
 * @brief  Tear down shared A/V render, codecs, LCD, media defaults, and setup state.
 *
 * @note  Call after the player is deleted via `video_player_delete()`. Resets internal
 *        state so `video_player_setup()` may be invoked again.
 */
void video_player_teardown(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
