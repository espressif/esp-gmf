/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_video_render.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Video player view
 */
typedef struct player_view video_player_view_t;

/**
 * @brief  Video player view configuration
 */
typedef struct {
    uint16_t                          width;
    uint16_t                          height;
    esp_video_render_stream_handle_t  stream;
    const char                       *file_name;
    const char                       *font_path;
    const uint8_t                    *font_data;
    int                               font_data_size;
    int                               initial_duration_sec;
    int                               hide_timeout_ms;
    uint8_t                           status_bar_alpha;
    uint8_t                           fps_alpha;
    esp_video_render_format_t         format;
} video_player_view_cfg_t;

/**
 * @brief  Initialize video player view
 *
 * @param[in]   cfg         Video player view configuration
 * @param[out]  out_handle  Video player view handle
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_player_view_init(const video_player_view_cfg_t *cfg, video_player_view_t **out_handle);

/**
 * @brief  Set playing state
 *
 * @param[in]  pv       Video player view handle
 * @param[in]  playing  Playing state
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_player_view_set_playing(video_player_view_t *pv, bool playing);

/**
 * @brief  Set duration
 *
 * @param[in]  pv            Video player view handle
 * @param[in]  duration_sec  Duration in seconds
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_player_view_set_duration(video_player_view_t *pv, int duration_sec);

/**
 * @brief  Set position
 *
 * @param[in]  pv       Video player view handle
 * @param[in]  pos_sec  Position in seconds
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_player_view_set_position(video_player_view_t *pv, int pos_sec);

/**
 * @brief  Set mute state
 *
 * @param[in]  pv    Video player view handle
 * @param[in]  mute  Mute state
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_player_view_set_mute(video_player_view_t *pv, bool mute);

/**
 * @brief  Set volume
 *
 * @param[in]  pv   Video player view handle
 * @param[in]  vol  Volume in percentage
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_player_view_set_volume(video_player_view_t *pv, int vol);

/**
 * @brief  Set file name
 *
 * @param[in]  pv         Video player view handle
 * @param[in]  file_name  File name
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_player_view_set_file_name(video_player_view_t *pv, const char *file_name);

/**
 * @brief  Set FPS
 *
 * @param[in]  pv   Video player view handle
 * @param[in]  fps  FPS
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_player_view_set_fps(video_player_view_t *pv, float fps);

/**
 * @brief  Tick video player view
 *
 * @note  During tick update, if show over hide_timeout_ms it will be hidden automatically
 *
 * @param[in]  pv  Video player view handle
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int video_player_view_tick(video_player_view_t *pv);

/**
 * @brief  Deinitialize video player view
 *
 * @param[in]  pv  Video player view handle
 */
void video_player_view_deinit(video_player_view_t *pv);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
