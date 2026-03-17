/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "audio_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Multi-source audio player init configuration
 */
typedef struct {
    void *playback_handle;  /*!< Audio codec device handle for playback */
} audio_ms_player_config_t;

/**
 * @brief  Initialize the multi-source audio player
 *
 * @param[in]  config  Init configuration, see audio_ms_player_config_t
 * @return
 *       - AUDIO_MS_PLAYER_OK                 Success
 *       - AUDIO_MS_PLAYER_ERR_INVALID_ARG    config is NULL
 *       - AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL  Pool/pipeline/task init failed
 */
audio_ms_player_err_t audio_multi_source_player_init(const audio_ms_player_config_t *config);

/**
 * @brief  Deinitialize the multi-source audio player
 *
 * @return
 *       - AUDIO_MS_PLAYER_OK                   Success
 *       - AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED  Player not initialized
 */
audio_ms_player_err_t audio_multi_source_player_deinit(void);

/**
 * @brief  Start playback from specified audio source
 *
 * @param[in]  source  Audio source type
 * @return
 *       - AUDIO_MS_PLAYER_OK                   Success
 *       - AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED  Player not initialized
 *       - AUDIO_MS_PLAYER_ERR_ALREADY_PLAYING  Same source already playing
 *       - AUDIO_MS_PLAYER_ERR_UNKNOWN_SOURCE   Invalid source type
 *       - AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL    Pipeline/IO failed
 */
audio_ms_player_err_t audio_multi_source_player_play(audio_source_t source);

/**
 * @brief  Pause current playback
 *
 * @return
 *       - AUDIO_MS_PLAYER_OK                   Success
 *       - AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED  Player not initialized
 *       - AUDIO_MS_PLAYER_ERR_NOT_PLAYING      Not in playing state
 *       - AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL    Pipeline pause failed
 */
audio_ms_player_err_t audio_multi_source_player_pause(void);

/**
 * @brief  Resume paused playback
 *
 * @return
 *       - AUDIO_MS_PLAYER_OK                   Success
 *       - AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED  Player not initialized
 *       - AUDIO_MS_PLAYER_ERR_NOT_PLAYING      Not in paused state
 *       - AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL    Pipeline resume failed
 */
audio_ms_player_err_t audio_multi_source_player_resume(void);

/**
 * @brief  Stop current playback
 *
 * @return
 *       - AUDIO_MS_PLAYER_OK                   Success
 *       - AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED  Player not initialized
 *       - AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL    Pipeline stop failed
 */
audio_ms_player_err_t audio_multi_source_player_stop(void);

/**
 * @brief  Switch to different audio source
 *
 * @param[in]  source  New audio source type
 * @return
 *       - AUDIO_MS_PLAYER_OK                   Success
 *       - AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED  Player not initialized
 *       - AUDIO_MS_PLAYER_ERR_UNKNOWN_SOURCE   Invalid source type
 *       - AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL    Pipeline/IO failed
 */
audio_ms_player_err_t audio_multi_source_player_switch_source(audio_source_t source);

/**
 * @brief  Play flash tone (interrupt current playback)
 *
 * @return
 *       - AUDIO_MS_PLAYER_OK                   Success (or flash already playing)
 *       - AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED  Player not initialized
 *       - AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL    Flash pipeline create/run failed
 */
audio_ms_player_err_t audio_multi_source_player_play_flash_tone(void);

/**
 * @brief  Get current audio source
 *
 * @param[out]  source  Current audio source type, see audio_source_t
 * @return
 *       - AUDIO_MS_PLAYER_OK                   Success
 *       - AUDIO_MS_PLAYER_ERR_INVALID_ARG      source is NULL
 *       - AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED  Player not initialized
 */
audio_ms_player_err_t audio_multi_source_player_get_current_source(audio_source_t *source);

/**
 * @brief  Get current audio state
 *
 * @param[out]  state  Current playback state, see audio_state_t
 * @return
 *       - AUDIO_MS_PLAYER_OK                   Success
 *       - AUDIO_MS_PLAYER_ERR_INVALID_ARG      state is NULL
 *       - AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED  Player not initialized
 */
audio_ms_player_err_t audio_multi_source_player_get_current_state(audio_state_t *state);

/**
 * @brief  Check if flash tone is currently playing
 *
 * @return  true if flash tone is playing, false otherwise
 */
bool audio_multi_source_player_is_flash_playing(void);

/**
 * @brief  Get volume level
 *
 * @param[out]  volume  Current volume level (0-100)
 * @return
 *       - AUDIO_MS_PLAYER_OK                   Success
 *       - AUDIO_MS_PLAYER_ERR_INVALID_ARG      volume is NULL
 *       - AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED  Player not initialized
 *       - AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL    Codec get volume failed
 */
audio_ms_player_err_t audio_multi_source_player_get_volume(int *volume);

/**
 * @brief  Set volume level
 *
 * @param[in]  volume  Volume level (0-100)
 * @return
 *       - AUDIO_MS_PLAYER_OK                   Success
 *       - AUDIO_MS_PLAYER_ERR_INVALID_ARG      volume out of range [0, 100]
 *       - AUDIO_MS_PLAYER_ERR_NOT_INITIALIZED  Player not initialized
 *       - AUDIO_MS_PLAYER_ERR_RESOURCE_FAIL    Codec set volume failed
 */
audio_ms_player_err_t audio_multi_source_player_set_volume(int volume);

/**
 * @brief  Get audio source name string
 *
 * @param[in]  source  Audio source type
 * @return  Source name string, or "Unknown" if source is invalid
 */
const char *audio_multi_source_player_get_source_name(audio_source_t source);

/**
 * @brief  Get audio state name string
 *
 * @param[in]  state  Audio state
 * @return  State name string, or "Unknown" if state is invalid
 */
const char *audio_multi_source_player_get_state_name(audio_state_t state);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
