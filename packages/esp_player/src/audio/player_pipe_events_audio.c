/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>

#include "esp_gmf_info.h"

#include "player_pipe_events.h"
#include "player_events.h"
#include "player_extractor.h"
#include "player_state.h"

static const char *TAG = "ESP_PLAYER_PIPE_EVT";

#define AUDIO_INFO_TO_PLAYER_INFO(player_info, gmf_info)  do {             \
    (player_info)->audio_info.format          = (gmf_info)->format_id;     \
    (player_info)->audio_info.sample_rate     = (gmf_info)->sample_rates;  \
    (player_info)->audio_info.channels        = (gmf_info)->channels;      \
    (player_info)->audio_info.bits_per_sample = (gmf_info)->bits;          \
    if ((gmf_info)->bitrate) {                                             \
        (player_info)->audio_info.bitrate = (gmf_info)->bitrate;           \
    }                                                                      \
} while (0)

esp_gmf_err_t _audio_decoder_pipe_event_handler(esp_gmf_event_pkt_t *event, void *ctx)
{
    esp_player_stream_t *stream = (esp_player_stream_t *)ctx;
    esp_player_cmd_msg_t cmd = {
        .cmd_type = ESP_PLAYER_CMD_FINISHED,
        .data = stream->audio_side->decoder,
        .data_len = sizeof(stream->audio_side->decoder)};
    if (event->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        switch (event->sub) {
            case ESP_GMF_EVENT_STATE_RUNNING:
                stream->task_status |= TASK_STATUS_AUDIO_DECODER_RUNNING;
                stream->runned_status |= TASK_STATUS_AUDIO_DECODER_RUNNING;
                if (stream->extractor == NULL) {
                    player_set_events(stream, _CTRL_PLAYER_RUN);
                }
                break;
            case ESP_GMF_EVENT_STATE_STOPPED: {
                ESP_LOGD(TAG, "Audio decoder stopped, task_status: %" PRIu8 " runned_status: %" PRIu8 " expected_tasks: %" PRIu8,
                         stream->task_status, stream->runned_status, stream->expected_tasks);
                stream->task_status &= ~TASK_STATUS_AUDIO_DECODER_RUNNING;
                esp_gmf_db_handle_t aud_db = player_audio_db(stream);
                if (aud_db) {
                    esp_gmf_db_done_write(aud_db);
                }
                if (stream->is_seeking) {
                    ESP_LOGD(TAG, "Audio decoder stopped seeking");
                    player_set_events(stream, _CTRL_PLAYER_DECODER_AUDIO_SEEK_DONE);
                }
                break;
            }
            case ESP_GMF_EVENT_STATE_FINISHED: {
                ESP_LOGD(TAG, "Audio decoder finished, task_status: %" PRIu8 " runned_status: %" PRIu8 " expected_tasks: %" PRIu8,
                         stream->task_status, stream->runned_status, stream->expected_tasks);
                stream->task_status &= ~TASK_STATUS_AUDIO_DECODER_RUNNING;
                if (!player_should_wait_for_audio_render(stream)) {
                    uint8_t exp_cmp = stream->expected_tasks & TASK_STATUS_MASK_NO_RENDER;
                    uint8_t run_cmp = stream->runned_status & TASK_STATUS_MASK_NO_RENDER;
                    if (run_cmp == exp_cmp && stream->task_status == 0) {
                        player_send_cmd(stream, &cmd);
                    }
                }
                esp_gmf_db_handle_t aud_db = player_audio_db(stream);
                if (aud_db) {
                    esp_gmf_db_done_write(aud_db);
                }
                if (stream->is_seeking) {
                    ESP_LOGD(TAG, "Audio decoder finished seeking");
                    player_set_events(stream, _CTRL_PLAYER_DECODER_AUDIO_SEEK_DONE);
                }
                break;
            }
            case ESP_GMF_EVENT_STATE_ERROR: {
                ESP_LOGE(TAG, "Audio decoder error");
                stream->task_status &= ~TASK_STATUS_AUDIO_DECODER_RUNNING;
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER, "runtime STATE_ERROR");
                int8_t aud_idx = player_audio_track_idx(stream);
                if (aud_idx >= 0) {
                    player_extractor_enable_stream(player_extractor_el(stream), ESP_EXTRACTOR_STREAM_TYPE_AUDIO, aud_idx, false);
                }
                cmd.cmd_type = ESP_PLAYER_CMD_ERROR;
                player_send_cmd(stream, &cmd);
                break;
            }
        }
    }
    if (event->type == ESP_GMF_EVT_TYPE_REPORT_INFO) {
        if (event->sub == ESP_GMF_INFO_SOUND) {
            esp_gmf_info_sound_t *sound_info = (esp_gmf_info_sound_t *)event->payload;
            stream->audio_side->track_info.track_type = ESP_PLAYER_TRACK_TYPE_AUDIO;
            AUDIO_INFO_TO_PLAYER_INFO(&stream->audio_side->track_info, sound_info);
            ESP_LOGD(TAG, "sample_rate %d- %" PRIu32, sound_info->sample_rates, stream->audio_side->track_info.audio_info.sample_rate);
            ESP_LOGD(TAG, "channels %d- %d", sound_info->channels, (int)stream->audio_side->track_info.audio_info.channels);
            ESP_LOGD(TAG, "bits_per_sample %d- %u", sound_info->bits, stream->audio_side->track_info.audio_info.bits_per_sample);
            cmd.cmd_type = ESP_PLAYER_CMD_REPORT_AUDIO_INFO;
            cmd.data = NULL;
            cmd.data_len = 0;
            player_send_cmd(stream, &cmd);
        }
    }

    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t _audio_render_pipe_event_handler(esp_gmf_event_pkt_t *event, void *ctx)
{
    esp_player_stream_t *stream = (esp_player_stream_t *)ctx;
    esp_player_cmd_msg_t cmd = {
        .cmd_type = ESP_PLAYER_CMD_FINISHED,
        .data = stream->audio_side->render,
        .data_len = sizeof(stream->audio_side->render)};

    if (event->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        switch (event->sub) {
            case ESP_GMF_EVENT_STATE_RUNNING:
                stream->task_status |= TASK_STATUS_AUDIO_RENDER_RUNNING;
                stream->runned_status |= TASK_STATUS_AUDIO_RENDER_RUNNING;
                if (stream->main_state == PLAYER_STATE_PREPARING) {
                    cmd.cmd_type = ESP_PLAYER_CMD_PLAYING;
                    player_send_cmd(stream, &cmd);
                }
                break;
            case ESP_GMF_EVENT_STATE_STOPPED:
                ESP_LOGD(TAG, "Audio render stopped, task_status: %" PRIu8 " runned_status: %" PRIu8 " expected_tasks: %" PRIu8,
                         stream->task_status, stream->runned_status, stream->expected_tasks);
                stream->task_status &= ~TASK_STATUS_AUDIO_RENDER_RUNNING;
                break;
            case ESP_GMF_EVENT_STATE_FINISHED:
                ESP_LOGD(TAG, "Audio render finished, task_status: %" PRIu8 " runned_status: %" PRIu8 " expected_tasks: %" PRIu8,
                         stream->task_status, stream->runned_status, stream->expected_tasks);
                stream->task_status &= ~TASK_STATUS_AUDIO_RENDER_RUNNING;
                if (stream->sync_handle != NULL && stream->video_side && stream->video_side->track_info.video_info.fps > 0) {
                    player_sync_set_video_fps(stream->sync_handle, stream->video_side->track_info.video_info.fps);
                    player_sync_enable_video_fps_sync(stream->sync_handle, true);
                }
                uint8_t exp_cmp = stream->expected_tasks & TASK_STATUS_MASK_NO_RENDER;
                uint8_t run_cmp = stream->runned_status & TASK_STATUS_MASK_NO_RENDER;
                if (run_cmp == exp_cmp && stream->task_status == 0) {
                    player_send_cmd(stream, &cmd);
                }
                break;
            case ESP_GMF_EVENT_STATE_ERROR: {
                ESP_LOGE(TAG, "Audio render error");
                stream->task_status &= ~TASK_STATUS_AUDIO_RENDER_RUNNING;
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER, "runtime STATE_ERROR");
                if (stream->sync_handle != NULL && stream->video_side && stream->video_side->track_info.video_info.fps > 0) {
                    player_sync_set_video_fps(stream->sync_handle, stream->video_side->track_info.video_info.fps);
                    player_sync_enable_video_fps_sync(stream->sync_handle, true);
                }
                int8_t aud_idx = player_audio_track_idx(stream);
                if (aud_idx >= 0) {
                    player_extractor_enable_stream(player_extractor_el(stream), ESP_EXTRACTOR_STREAM_TYPE_AUDIO, aud_idx, false);
                }
                cmd.cmd_type = ESP_PLAYER_CMD_ERROR;
                player_send_cmd(stream, &cmd);
                break;
            }
        }
    }
    return ESP_GMF_ERR_OK;
}
