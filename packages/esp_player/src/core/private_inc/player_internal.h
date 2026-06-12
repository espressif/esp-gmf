/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_task.h"

#include "esp_player.h"
#include "player_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

esp_player_err_t player_set_task_config_impl(esp_player_stream_t *stream, const esp_player_task_config_t *config);
esp_player_err_t player_set_buffer_config_impl(esp_player_stream_t *stream, const esp_player_buffer_config_t *config);
void player_free_runtime_config(esp_player_stream_t *stream);

void player_cfg_fill_extractor_task(const esp_player_stream_t *stream, esp_gmf_task_cfg_t *out);
void player_cfg_fill_audio_decoder_task(const esp_player_stream_t *stream, esp_gmf_task_cfg_t *out);
void player_cfg_fill_audio_render_task(const esp_player_stream_t *stream, esp_gmf_task_cfg_t *out);
void player_cfg_fill_video_decoder_task(const esp_player_stream_t *stream, esp_gmf_task_cfg_t *out);
void player_cfg_fill_video_render_task(const esp_player_stream_t *stream, esp_gmf_task_cfg_t *out);

uint32_t player_cfg_extractor_pool_size(const esp_player_stream_t *stream, bool for_video);
uint32_t player_cfg_http_read_buf_size(const esp_player_stream_t *stream);

uint32_t player_cfg_prebuffer_resume_ms(const esp_player_stream_t *stream);
uint32_t player_cfg_rebuffer_enter_ms(const esp_player_stream_t *stream);
uint32_t player_cfg_rebuffer_resume_ms(const esp_player_stream_t *stream);
uint32_t player_cfg_rebuffer_grace_ms(const esp_player_stream_t *stream);

esp_player_err_t player_validate_init_config(const esp_player_config_t *config);

esp_player_err_t player_validate_sync_mode(esp_player_sync_mode_t sync_mode);

esp_player_err_t player_side_reconcile(esp_player_stream_t *stream, uint8_t new_mask);

uint8_t player_build_time_av_mask(void);

bool player_build_time_has_full_av(void);

void player_deinit_decoder_subcfg(esp_player_stream_t *stream);

void player_free_custom_elements(esp_player_stream_t *stream);

esp_player_err_t player_set_dec_cfg_impl(esp_player_stream_t *stream, esp_player_format_t type, void *cfg,
                                         uint32_t cfg_sz);

void player_set_speed_impl(esp_player_stream_t *stream, float speed, esp_player_err_t *out_ret);

static inline bool is_state_allowed_for_operation(esp_player_stream_t *stream)
{
    return (stream->main_state == PLAYER_STATE_IDLE
            || stream->main_state == PLAYER_STATE_STOPPED
            || stream->main_state == PLAYER_STATE_FINISHED);
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
