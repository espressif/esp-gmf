/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#include "esp_audio_render.h"
#include "esp_gmf_element.h"
#include "player_sync.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_PLAYER_AUDIO_RENDER_CONFIG()  {  \
    .sample_info = {                             \
        .sample_rate     = 48000,                \
        .bits_per_sample = 16,                   \
        .channel         = 2                     \
    },                                           \
    .stream_handle = NULL,                       \
    .sync_handle   = NULL                        \
}

typedef struct {
    esp_audio_render_sample_info_t    sample_info;
    esp_audio_render_stream_handle_t  stream_handle;
    player_sync_handle_t              sync_handle;
} player_audio_render_config_t;

esp_gmf_err_t player_audio_render_init(player_audio_render_config_t *config, esp_gmf_element_handle_t *handle);
esp_gmf_err_t player_audio_render_set_frame_duration(esp_gmf_element_handle_t handle, uint32_t duration_ms);
esp_gmf_err_t player_audio_render_set_speed(esp_gmf_element_handle_t handle, float speed);
esp_gmf_err_t player_audio_render_get_latency(esp_gmf_element_handle_t handle, uint32_t *latency);
esp_gmf_err_t player_audio_render_flush_enable(esp_gmf_element_handle_t handle, bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
