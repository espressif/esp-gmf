/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_player_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void *player_sync_handle_t;

typedef struct {
    int64_t                 audio_delay_threshold;
    int64_t                 video_delay_threshold;
    esp_player_sync_mode_t  sync_mode;
    uint32_t                audio_resume_bit;
    uint32_t                video_resume_bit;
    void                   *bit_ctx;
    void                   *audio_render_stream;
} player_sync_config_t;

#define PLAYER_SYNC_CONFIG_DEFAULT()  {                   \
    .audio_delay_threshold = 1000,                        \
    .video_delay_threshold = 1000,                        \
    .sync_mode             = ESP_PLAYER_SYNC_MODE_AUDIO,  \
}

esp_player_err_t player_sync_create(player_sync_config_t *config, player_sync_handle_t *handle);
esp_player_err_t player_sync_destroy(player_sync_handle_t handle);

esp_player_err_t player_sync_set_mode(player_sync_handle_t handle, esp_player_sync_mode_t sync_mode);

esp_player_err_t player_sync_set_speed(player_sync_handle_t handle, float speed);

esp_player_err_t player_sync_set_audio_delay_threshold(player_sync_handle_t handle, int64_t audio_delay_threshold);
esp_player_err_t player_sync_set_video_delay_threshold(player_sync_handle_t handle, int64_t video_delay_threshold);

esp_player_err_t player_sync_set_video_fps(player_sync_handle_t handle, float fps);
esp_player_err_t player_sync_enable_video_fps_sync(player_sync_handle_t handle, bool enable);

bool player_sync_audio_render_frame(player_sync_handle_t handle, uint64_t pts_ms);
bool player_sync_video_render_frame(player_sync_handle_t handle, uint64_t pts_ms);
bool player_sync_audio_decode_frame(player_sync_handle_t handle, uint64_t pts_ms);
bool player_sync_video_decode_frame(player_sync_handle_t handle, uint64_t pts_ms);

esp_player_err_t player_sync_video_fps_sync(player_sync_handle_t handle);
esp_player_err_t player_sync_resume(player_sync_handle_t handle);
esp_player_err_t player_sync_pause(player_sync_handle_t handle);
esp_player_err_t player_sync_reset(player_sync_handle_t handle);

uint64_t player_sync_get_audio_render_pts_ms_with_latency(player_sync_handle_t handle);
uint64_t player_sync_get_audio_render_pts_ms(player_sync_handle_t handle);
uint64_t player_sync_get_audio_decode_pts_ms(player_sync_handle_t handle);
uint64_t player_sync_get_video_render_pts_ms(player_sync_handle_t handle);

esp_player_err_t player_sync_set_render_pts(player_sync_handle_t handle, uint64_t time_ms);
esp_player_err_t player_sync_set_seek_target(player_sync_handle_t handle, uint64_t time_ms);
uint64_t player_sync_get_seek_target(player_sync_handle_t handle);
esp_player_err_t player_sync_set_seek_in_progress(player_sync_handle_t handle, bool in_progress);
bool player_sync_get_seek_in_progress(player_sync_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
