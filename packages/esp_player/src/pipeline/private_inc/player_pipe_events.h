/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_gmf_event.h"
#include "player_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

esp_gmf_err_t _extractor_pipe_event_handler(esp_gmf_event_pkt_t *event, void *ctx);
esp_gmf_err_t _video_decoder_pipe_event_handler(esp_gmf_event_pkt_t *event, void *ctx);
esp_gmf_err_t _audio_decoder_pipe_event_handler(esp_gmf_event_pkt_t *event, void *ctx);
esp_gmf_err_t _audio_render_pipe_event_handler(esp_gmf_event_pkt_t *event, void *ctx);
esp_gmf_err_t _video_render_pipe_event_handler(esp_gmf_event_pkt_t *event, void *ctx);

static inline bool player_should_wait_for_audio_render(const esp_player_stream_t *stream)
{
    return ((stream->expected_tasks & TASK_STATUS_AUDIO_RENDER_RUNNING) != 0)
           && stream->audio_side != NULL
           && stream->audio_side->render != NULL
           && ((stream->task_status & TASK_STATUS_AUDIO_RENDER_RUNNING) != 0);
}

static inline bool player_should_wait_for_video_render(const esp_player_stream_t *stream)
{
    return ((stream->expected_tasks & TASK_STATUS_VIDEO_RENDER_RUNNING) != 0)
           && stream->video_side != NULL
           && stream->video_side->render != NULL
           && ((stream->task_status & TASK_STATUS_VIDEO_RENDER_RUNNING) != 0);
}

#ifdef __cplusplus
}
#endif  /* __cplusplus */
