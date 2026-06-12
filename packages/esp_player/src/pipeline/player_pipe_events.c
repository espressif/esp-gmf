/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <stdbool.h>

#include "esp_gmf_info.h"
#include "esp_extractor.h"

#include "player_pipe_events.h"
#include "player_pipeline.h"
#include "player_extractor.h"

static const char *TAG = "ESP_PLAYER_PIPE_EVT";

static esp_gmf_err_t extractor_handle_report_info(esp_player_stream_t *stream, esp_gmf_event_pkt_t *event,
                                                  esp_player_cmd_msg_t *cmd)
{
    if (event->sub != ESP_GMF_INFO_VIDEO && event->sub != ESP_GMF_INFO_SOUND) {
        return ESP_GMF_ERR_OK;
    }

    esp_gmf_element_handle_t ext_el = player_extractor_el(stream);
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    int8_t audio_active_idx = -1;
    if (player_extractor_track_active(ext_el, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, &audio_active_idx) == ESP_GMF_ERR_OK) {
        if (audio_active_idx >= 0) {
            if (queues_init(stream, true) != ESP_PLAYER_ERR_OK) {
                player_send_cmd(stream, cmd);
                return ESP_GMF_ERR_FAIL;
            }
            if (player_extractor_get_track_info(ext_el, ESP_PLAYER_TRACK_TYPE_AUDIO, (uint16_t)audio_active_idx, &stream->audio_side->track_info) != ESP_GMF_ERR_OK) {
                cmd->cmd_type = ESP_PLAYER_CMD_ERROR;
                player_send_cmd(stream, cmd);
                return ESP_GMF_ERR_FAIL;
            }
            ESP_LOGD(TAG, "audio_active_idx: %d, format: %x channels: %u sample_rate: %u bits_per_sample: %u\n", audio_active_idx,
                     (unsigned)stream->audio_side->track_info.audio_info.format,
                     stream->audio_side->track_info.audio_info.channels,
                     (unsigned)stream->audio_side->track_info.audio_info.sample_rate,
                     stream->audio_side->track_info.audio_info.bits_per_sample);
        }
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
#if CONFIG_ESP_PLAYER_ENABLE_VIDEO
    int8_t video_active_idx = -1;
    if (player_extractor_track_active(ext_el, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, &video_active_idx) == ESP_GMF_ERR_OK) {
        if (video_active_idx >= 0) {
            if (queues_init(stream, false) != ESP_PLAYER_ERR_OK) {
                player_send_cmd(stream, cmd);
                return ESP_GMF_ERR_FAIL;
            }
            if (player_extractor_get_track_info(ext_el, ESP_PLAYER_TRACK_TYPE_VIDEO, (uint16_t)video_active_idx, &stream->video_side->track_info) != ESP_GMF_ERR_OK) {
                cmd->cmd_type = ESP_PLAYER_CMD_ERROR;
                player_send_cmd(stream, cmd);
                return ESP_GMF_ERR_FAIL;
            }
            ESP_LOGD(TAG, "video_active_idx: %d, format: %x\n", video_active_idx, (unsigned)stream->video_side->track_info.video_info.format);
        }
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_VIDEO */
    cmd->cmd_type = ESP_PLAYER_CMD_REPORT_TRACK_INFO;
    cmd->data = NULL;
    cmd->data_len = 0;
    player_send_cmd(stream, cmd);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t _extractor_pipe_event_handler(esp_gmf_event_pkt_t *event, void *ctx)
{
    esp_player_stream_t *stream = (esp_player_stream_t *)ctx;
    esp_player_cmd_msg_t cmd = {
        .cmd_type = ESP_PLAYER_CMD_FINISHED,
        .data = stream->extractor,
        .data_len = sizeof(stream->extractor)};
    if (event->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        switch (event->sub) {
            case ESP_GMF_EVENT_STATE_RUNNING:
                stream->task_status |= TASK_STATUS_EXTRACTOR_RUNNING;
                stream->runned_status |= TASK_STATUS_EXTRACTOR_RUNNING;
                player_set_events(stream, _CTRL_PLAYER_RUN);
                break;
            case ESP_GMF_EVENT_STATE_STOPPED:
                ESP_LOGD(TAG, "Extractor stopped, task_status: %" PRIu8 " runned_status: %" PRIu8 " expected_tasks: %" PRIu8,
                         stream->task_status, stream->runned_status, stream->expected_tasks);
                player_sync_set_seek_target(stream->sync_handle, 0);
                stream->task_status &= ~TASK_STATUS_EXTRACTOR_RUNNING;
                break;
            case ESP_GMF_EVENT_STATE_FINISHED:
                ESP_LOGD(TAG, "Extractor finished, task_status: %" PRIu8 " runned_status: %" PRIu8 " expected_tasks: %" PRIu8,
                         stream->task_status, stream->runned_status, stream->expected_tasks);
                stream->task_status &= ~TASK_STATUS_EXTRACTOR_RUNNING;
                player_sync_set_seek_target(stream->sync_handle, 0);
                if (!player_should_wait_for_audio_render(stream) && !player_should_wait_for_video_render(stream)) {
                    uint8_t exp_cmp = stream->expected_tasks & TASK_STATUS_MASK_NO_RENDER;
                    uint8_t run_cmp = stream->runned_status & TASK_STATUS_MASK_NO_RENDER;
                    if (run_cmp == exp_cmp && stream->task_status == 0 && stream->expected_tasks != TASK_STATUS_EXTRACTOR_RUNNING) {
                        player_send_cmd(stream, &cmd);
                    }
                }
                if (stream->audio_side) {
                    player_send_null_queue(stream->audio_side->extractor_queue);
                }
                if (stream->video_side) {
                    player_send_null_queue(stream->video_side->extractor_queue);
                }
                break;
            case ESP_GMF_EVENT_STATE_ERROR:
                stream->task_status &= ~TASK_STATUS_EXTRACTOR_RUNNING;
                if (stream->_is_stop) {
                    ESP_LOGI(TAG, "Extractor stopped during open (stop requested)");
                    break;
                }
                ESP_LOGE(TAG, "Extractor error");
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_EXTRACTOR, "runtime STATE_ERROR");
                cmd.cmd_type = ESP_PLAYER_CMD_ERROR;
                player_send_cmd(stream, &cmd);
                break;
        }
    }

    if (event->type == ESP_GMF_EVT_TYPE_REPORT_INFO) {
        esp_gmf_err_t er = extractor_handle_report_info(stream, event, &cmd);
        if (er != ESP_GMF_ERR_OK) {
            return er;
        }
    }

    return ESP_GMF_ERR_OK;
}
