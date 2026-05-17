/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include <stdint.h>

#include "esp_extractor.h"

#include "player_state_path_ops.h"

#include "player_pipeline.h"
#include "player_audio_render.h"

static const char *TAG = "ESP_PLAYER_STATE";

static void st_stop_path(esp_player_stream_t *stream)
{
    stream->runned_status &= ~TASK_STATUS_AUDIO_DECODER_RUNNING;
    stream->runned_status &= ~TASK_STATUS_AUDIO_RENDER_RUNNING;
    stream->expected_tasks &= ~TASK_STATUS_AUDIO_DECODER_RUNNING;
    stream->expected_tasks &= ~TASK_STATUS_AUDIO_RENDER_RUNNING;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_db_handle_t aud_db = player_audio_db(stream);
    do {
        ret = ESP_GMF_ERR_OK;
        bool video_running = (stream->task_status & (TASK_STATUS_VIDEO_DECODER_RUNNING | TASK_STATUS_VIDEO_RENDER_RUNNING)) != 0;
        if (!video_running) {
            if (stream->task_status & TASK_STATUS_EXTRACTOR_RUNNING) {
                ret |= player_stop_extractor(stream);
            }
        } else if (stream->audio_side) {
            player_drop_single_queue(stream, stream->audio_side->extractor_queue);
        }
        if (stream->task_status & TASK_STATUS_AUDIO_DECODER_RUNNING) {
            ret |= player_stop_decoder(stream, stream->audio_side->extractor_queue, stream->task_status, TASK_STATUS_AUDIO_DECODER_RUNNING,
                                       stream->audio_side->decoder, aud_db);
        }
        if (stream->task_status & TASK_STATUS_AUDIO_RENDER_RUNNING) {
            ret |= player_stop_render(stream, stream->task_status, TASK_STATUS_AUDIO_RENDER_RUNNING, aud_db, stream->audio_side->render);
        }
    } while (0);
}

static void st_init_params_format(esp_player_stream_t *stream)
{
    if (stream->av_mask == ESP_PLAYER_MASK_AUDIO) {
        esp_player_format_t url_fmt = player_current_format(stream);
        /* player_apply_raw_pcm_query() may have already filled sr/ch/bits and PCM dec format;
         * do not replace the decoder format with the RAW container FourCC. */
        if (url_fmt == (esp_player_format_t)ESP_EXTRACTOR_TYPE_RAW
            && stream->audio_side->track_info.audio_info.sample_rate != 0) {
            return;
        }
        if (stream->dec_cfg.dec_type == ESP_AUDIO_SIMPLE_DEC_TYPE_NONE) {
            stream->audio_side->track_info.audio_info.format = player_current_format(stream);
        } else {
            stream->audio_side->track_info.audio_info.format = stream->dec_cfg.dec_type;
        }
    }
}

static void st_seek_handles(esp_player_stream_t *stream, esp_gmf_task_handle_t *dec_tsk, QueueHandle_t *q)
{
    *dec_tsk = stream->audio_side ? player_pipeline_task(stream->audio_side->decoder) : NULL;
    *q = stream->audio_side ? stream->audio_side->extractor_queue : NULL;
}

static void st_seek_pause_decoder(esp_player_stream_t *stream, esp_gmf_db_handle_t aud_db,
                                  esp_gmf_task_handle_t aud_dec_tsk, QueueHandle_t aud_q,
                                  esp_gmf_event_state_t *state, esp_gmf_err_t *ret)
{
    if (aud_dec_tsk) {
        *ret = ESP_GMF_ERR_FAIL;
        *state = ESP_GMF_EVENT_STATE_INITIALIZED;
        esp_gmf_task_get_state(aud_dec_tsk, state);
        player_pause_decoder_task(stream, aud_dec_tsk, aud_db, aud_q, state, TASK_STATUS_AUDIO_DECODER_RUNNING, ret);
    }
}

static void st_seek_stop_decoder_if_running(esp_player_stream_t *stream, QueueHandle_t aud_q,
                                            esp_gmf_db_handle_t aud_db, esp_gmf_err_t *ret)
{
    if ((stream->task_status & TASK_STATUS_AUDIO_DECODER_RUNNING) && stream->audio_side) {
        *ret |= player_stop_decoder(stream, aud_q, stream->task_status, TASK_STATUS_AUDIO_DECODER_RUNNING, stream->audio_side->decoder, aud_db);
    }
}

static bool st_finished_try_seek(esp_player_stream_t *stream)
{
    if (stream->expected_tasks & TASK_STATUS_AUDIO_DECODER_RUNNING) {
        esp_gmf_pipeline_seek(stream->audio_side->decoder, 0);
        return true;
    }
    return false;
}

static void st_start_decoder_mask(esp_player_stream_t *stream)
{
    ESP_LOGD(TAG, "Starting audio decoder");
    if (player_create_decoder_pipeline(stream, true) == ESP_PLAYER_ERR_OK) {
        stream->expected_tasks |= TASK_STATUS_AUDIO_DECODER_RUNNING | TASK_STATUS_AUDIO_RENDER_RUNNING;
        ESP_LOGI(TAG, "Audio decoder started, waiting for ready event");
    }
}

static void st_seek_flush_render(esp_player_stream_t *stream, esp_gmf_element_handle_t *audio_render_el)
{
    if (audio_render_el) {
        *audio_render_el = NULL;
    }
    if (stream->audio_side && stream->audio_side->render) {
        if (esp_gmf_pipeline_get_el_by_name(stream->audio_side->render, AUDIO_RENDER_TAG, audio_render_el) == ESP_GMF_ERR_OK &&
            *audio_render_el) {
            player_audio_render_flush_enable(*audio_render_el, true);
        }
    }
}

static void st_seek_resume_or_create_decoder(esp_player_stream_t *stream)
{
    if (stream->task_status & TASK_STATUS_AUDIO_DECODER_RUNNING) {
        esp_gmf_pipeline_resume(stream->audio_side->decoder);
    } else {
        player_create_pipeline_if_expected(stream, player_create_decoder_pipeline, TASK_STATUS_AUDIO_DECODER_RUNNING, true);
    }
}

static void st_seek_create_render_if_expected(esp_player_stream_t *stream)
{
    player_create_pipeline_if_expected(stream, player_create_render_pipeline, TASK_STATUS_AUDIO_RENDER_RUNNING, true);
}

static bool st_seek_wait_decoder_done(esp_player_stream_t *stream, uint32_t seek_done_bits)
{
    if (seek_done_bits & _CTRL_PLAYER_DECODER_AUDIO_SEEK_DONE) {
        if (player_wait_events(stream, _CTRL_PLAYER_DECODER_AUDIO_SEEK_DONE, portMAX_DELAY) != ESP_PLAYER_ERR_OK) {
            player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER, "wait audio seek done failed");
            return true;
        }
    }
    return false;
}

static bool st_decoder_expected_running(esp_player_stream_t *stream)
{
    return (stream->expected_tasks & TASK_STATUS_AUDIO_DECODER_RUNNING) != 0;
}

static void st_seek_paused_disable_flush(esp_player_stream_t *stream, esp_gmf_element_handle_t audio_render_el)
{
    (void)stream;
    if (audio_render_el) {
        player_audio_render_flush_enable(audio_render_el, false);
    }
}

static void st_seek_playing_disable_flush(esp_player_stream_t *stream, esp_gmf_element_handle_t audio_render_el)
{
    (void)stream;
    if (audio_render_el) {
        player_audio_render_flush_enable(audio_render_el, false);
    }
}

static uint32_t st_seek_done_bit(esp_player_stream_t *stream)
{
    if (stream->expected_tasks & TASK_STATUS_AUDIO_DECODER_RUNNING) {
        return _CTRL_PLAYER_DECODER_AUDIO_SEEK_DONE;
    }
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
