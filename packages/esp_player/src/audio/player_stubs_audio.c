/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/**
 * @note  Link stubs when CONFIG_ESP_PLAYER_ENABLE_AUDIO is disabled.
 *        Provides symbols declared in player_pipeline.h / player_state.h.
 */

#include "player_state_path_ops.h"
#include "player_pipeline.h"

esp_gmf_db_handle_t player_audio_db(esp_player_stream_t *stream)
{
    (void)stream;
    return NULL;
}

int8_t player_audio_track_idx(esp_player_stream_t *stream)
{
    (void)stream;
    return -1;
}

esp_player_err_t player_pl_try_custom_decoder_audio(esp_player_stream_t *stream,
                                                    esp_gmf_element_handle_t *out_el, bool *is_custom)
{
    (void)stream;
    (void)out_el;
    (void)is_custom;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_pl_install_builtin_audio_decoder(esp_player_stream_t *stream,
                                                         esp_gmf_element_handle_t *decoder_el)
{
    (void)stream;
    (void)decoder_el;
    return ESP_PLAYER_ERR_FAIL;
}

esp_player_err_t player_pl_queues_init_audio(esp_player_stream_t *stream, uint32_t queue_size)
{
    (void)stream;
    (void)queue_size;
    return ESP_PLAYER_ERR_FAIL;
}

esp_player_err_t player_pl_run_create_audio_decoder(esp_player_stream_t *stream)
{
    (void)stream;
    return ESP_PLAYER_ERR_FAIL;
}

esp_player_err_t player_pl_create_audio_render(esp_player_stream_t *stream)
{
    (void)stream;
    return ESP_PLAYER_ERR_FAIL;
}

static void st_stop_path(esp_player_stream_t *stream)
{
    (void)stream;
}

static void st_init_params_format(esp_player_stream_t *stream)
{
    (void)stream;
}

static void st_seek_handles(esp_player_stream_t *stream, esp_gmf_task_handle_t *dec_tsk, QueueHandle_t *q)
{
    (void)stream;
    *dec_tsk = NULL;
    *q = NULL;
}

static void st_seek_pause_decoder(esp_player_stream_t *stream, esp_gmf_db_handle_t db,
                                  esp_gmf_task_handle_t dec_tsk, QueueHandle_t q,
                                  esp_gmf_event_state_t *state, esp_gmf_err_t *ret)
{
    (void)stream;
    (void)db;
    (void)dec_tsk;
    (void)q;
    (void)state;
    (void)ret;
}

static void st_seek_stop_decoder_if_running(esp_player_stream_t *stream, QueueHandle_t q,
                                            esp_gmf_db_handle_t db, esp_gmf_err_t *ret)
{
    (void)stream;
    (void)q;
    (void)db;
    (void)ret;
}

static bool st_finished_try_seek(esp_player_stream_t *stream)
{
    (void)stream;
    return false;
}

static void st_start_decoder_mask(esp_player_stream_t *stream)
{
    (void)stream;
}

static void st_seek_flush_render(esp_player_stream_t *stream, esp_gmf_element_handle_t *render_el)
{
    (void)stream;
    if (render_el) {
        *render_el = NULL;
    }
}

static void st_seek_resume_or_create_decoder(esp_player_stream_t *stream)
{
    (void)stream;
}

static void st_seek_create_render_if_expected(esp_player_stream_t *stream)
{
    (void)stream;
}

static bool st_seek_wait_decoder_done(esp_player_stream_t *stream, uint32_t seek_done_bits)
{
    (void)stream;
    (void)seek_done_bits;
    return false;
}

static bool st_decoder_expected_running(esp_player_stream_t *stream)
{
    (void)stream;
    return false;
}

static void st_seek_paused_disable_flush(esp_player_stream_t *stream, esp_gmf_element_handle_t render_el)
{
    (void)stream;
    (void)render_el;
}

static void st_seek_playing_disable_flush(esp_player_stream_t *stream, esp_gmf_element_handle_t render_el)
{
    (void)stream;
    (void)render_el;
}

static uint32_t st_seek_done_bit(esp_player_stream_t *stream)
{
    (void)stream;
    return 0;
}

const player_st_path_ops_t *player_st_get_audio_ops(void)
{
    static const player_st_path_ops_t ops = {
        .stop_path = st_stop_path,
        .init_params_format = st_init_params_format,
        .seek_handles = st_seek_handles,
        .seek_pause_decoder = st_seek_pause_decoder,
        .seek_stop_decoder_if_running = st_seek_stop_decoder_if_running,
        .finished_try_seek = st_finished_try_seek,
        .start_decoder_mask = st_start_decoder_mask,
        .seek_flush_render = st_seek_flush_render,
        .seek_resume_or_create_decoder = st_seek_resume_or_create_decoder,
        .seek_create_render_if_expected = st_seek_create_render_if_expected,
        .seek_done_bit = st_seek_done_bit,
        .seek_wait_decoder_done = st_seek_wait_decoder_done,
        .decoder_expected_running = st_decoder_expected_running,
        .seek_paused_disable_flush = st_seek_paused_disable_flush,
        .seek_playing_disable_flush = st_seek_playing_disable_flush,
    };
    return &ops;
}
