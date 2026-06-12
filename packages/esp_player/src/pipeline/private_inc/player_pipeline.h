/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_pipeline.h"
#include "esp_gmf_port.h"
#include "esp_gmf_task.h"
#include "esp_extractor.h"

#include "esp_player_types.h"
#include "player_stream.h"
#include "player_events.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/* -------- Public pipeline API (player_pipeline.c) -------- */

esp_player_err_t player_create_extractor_pipeline(esp_player_stream_t *stream);
esp_player_err_t player_create_decoder_pipeline(esp_player_stream_t *stream, bool is_audio);
esp_player_err_t player_create_render_pipeline(esp_player_stream_t *stream, bool is_audio);
esp_player_err_t queues_init(esp_player_stream_t *stream, bool is_audio);

/* -------- Component-internal: player_pipeline.c <-> audio/video pipeline .c -------- */

esp_player_err_t player_pl_task_create(esp_gmf_task_cfg_t *cfg, esp_gmf_pipeline_handle_t pipe,
                                       esp_gmf_task_handle_t *task_hd);

esp_player_err_t player_config_decoder_pipeline(esp_player_stream_t *stream, bool is_audio,
                                                esp_gmf_pipeline_handle_t *decoder_pipe,
                                                esp_gmf_port_handle_t decoder_inport,
                                                esp_gmf_port_handle_t decoder_outport,
                                                player_pipe_event_handler_t event_cb,
                                                esp_gmf_task_cfg_t *cfg,
                                                esp_gmf_task_handle_t *task_hd);

esp_player_err_t player_pl_try_custom_decoder_audio(esp_player_stream_t *stream,
                                                    esp_gmf_element_handle_t *out_el, bool *is_custom);
esp_player_err_t player_pl_try_custom_decoder_video(esp_player_stream_t *stream,
                                                    esp_gmf_element_handle_t *out_el, bool *is_custom);

esp_player_err_t player_pl_install_builtin_audio_decoder(esp_player_stream_t *stream,
                                                         esp_gmf_element_handle_t *decoder_el);
esp_player_err_t player_pl_install_builtin_video_decoder(esp_player_stream_t *stream,
                                                         esp_gmf_element_handle_t *decoder_el);

esp_player_err_t player_pl_queues_init_audio(esp_player_stream_t *stream, uint32_t queue_size);
esp_player_err_t player_pl_queues_init_video(esp_player_stream_t *stream, uint32_t queue_size);

esp_player_err_t player_pl_run_create_audio_decoder(esp_player_stream_t *stream);
esp_player_err_t player_pl_run_create_video_decoder(esp_player_stream_t *stream);

esp_player_err_t player_pl_create_audio_render(esp_player_stream_t *stream);
esp_player_err_t player_pl_create_video_render(esp_player_stream_t *stream);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
