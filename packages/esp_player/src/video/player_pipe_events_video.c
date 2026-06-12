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

#define VIDEO_INFO_TO_PLAYER_INFO(player_info, gmf_info)  do {    \
    (player_info)->video_info.format = (gmf_info)->format_id;     \
    (player_info)->video_info.width  = (gmf_info)->width;         \
    (player_info)->video_info.height = (gmf_info)->height;        \
    if ((gmf_info)->bitrate) {                                    \
        (player_info)->video_info.bitrate = (gmf_info)->bitrate;  \
    }                                                             \
} while (0)

static inline int player_video_dim_fits_with_align16(uint16_t ref, uint16_t dec)
{
    return (((uint32_t)ref + 15U) & (~0xFU)) >= dec;
}

esp_gmf_err_t _video_decoder_pipe_event_handler(esp_gmf_event_pkt_t *event, void *ctx)
{
    esp_player_stream_t *stream = (esp_player_stream_t *)ctx;
    esp_player_cmd_msg_t cmd = {
        .cmd_type = ESP_PLAYER_CMD_FINISHED,
        .data = stream->video_side->decoder,
        .data_len = sizeof(stream->video_side->decoder)};
    if (event->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        switch (event->sub) {
            case ESP_GMF_EVENT_STATE_RUNNING:
                stream->task_status |= TASK_STATUS_VIDEO_DECODER_RUNNING;
                stream->runned_status |= TASK_STATUS_VIDEO_DECODER_RUNNING;
                if (stream->extractor == NULL) {
                    player_set_events(stream, _CTRL_PLAYER_RUN);
                }
                break;
            case ESP_GMF_EVENT_STATE_STOPPED:
                ESP_LOGD(TAG, "Video decoder stopped, task_status: %" PRIu8 " runned_status: %" PRIu8 " expected_tasks: %" PRIu8,
                         stream->task_status, stream->runned_status, stream->expected_tasks);
                stream->task_status &= ~TASK_STATUS_VIDEO_DECODER_RUNNING;
                if (stream->is_seeking) {
                    ESP_LOGD(TAG, "Video decoder stopped seeking");
                    player_set_events(stream, _CTRL_PLAYER_DECODER_VIDEO_SEEK_DONE);
                }
                break;
            case ESP_GMF_EVENT_STATE_FINISHED:
                stream->task_status &= ~TASK_STATUS_VIDEO_DECODER_RUNNING;
                ESP_LOGD(TAG, "Video decoder finished, task_status: %" PRIu8 " runned_status: %" PRIu8 " expected_tasks: %" PRIu8,
                         stream->task_status, stream->runned_status, stream->expected_tasks);
                if (!player_should_wait_for_video_render(stream)) {
                    uint8_t exp_cmp = stream->expected_tasks & TASK_STATUS_MASK_NO_RENDER;
                    uint8_t run_cmp = stream->runned_status & TASK_STATUS_MASK_NO_RENDER;
                    if (run_cmp == exp_cmp && stream->task_status == 0) {
                        player_send_cmd(stream, &cmd);
                    }
                }
                if (stream->is_seeking) {
                    ESP_LOGD(TAG, "Video decoder finished seeking");
                    player_set_events(stream, _CTRL_PLAYER_DECODER_VIDEO_SEEK_DONE);
                }
                break;
            case ESP_GMF_EVENT_STATE_ERROR:
                ESP_LOGE(TAG, "Video decoder error");
                stream->task_status &= ~TASK_STATUS_VIDEO_DECODER_RUNNING;
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER, "runtime STATE_ERROR");
                int8_t vid_idx = player_video_track_idx(stream);
                if (vid_idx >= 0) {
                    player_extractor_enable_stream(player_extractor_el(stream), ESP_EXTRACTOR_STREAM_TYPE_VIDEO, vid_idx, false);
                }
                cmd.cmd_type = ESP_PLAYER_CMD_ERROR;
                player_send_cmd(stream, &cmd);
                break;
        }
    }

    if (event->type == ESP_GMF_EVT_TYPE_REPORT_INFO) {
        if (event->sub == ESP_GMF_INFO_VIDEO) {
            cmd.cmd_type = ESP_PLAYER_CMD_REPORT_VIDEO_INFO;
            cmd.data = NULL;
            cmd.data_len = 0;
            esp_gmf_info_video_t *video_info = (esp_gmf_info_video_t *)event->payload;
            stream->video_side->track_info.track_type = ESP_PLAYER_TRACK_TYPE_VIDEO;
            uint16_t ext_w = stream->video_side->track_info.video_info.width;
            uint16_t ext_h = stream->video_side->track_info.video_info.height;
            int has_ext_size = (ext_w > 0 && ext_h > 0);
            if (has_ext_size &&
                (!player_video_dim_fits_with_align16(ext_w, video_info->width) ||
                 !player_video_dim_fits_with_align16(ext_h, video_info->height))) {
                ESP_LOGD(TAG, "video info mismatch dec=%ux%u ext=%ux%u fmt=0x%lx",
                         video_info->width, video_info->height,
                         ext_w, ext_h, video_info->format_id);
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER, "video info size mismatch");
                cmd.cmd_type = ESP_PLAYER_CMD_ERROR;
                player_send_cmd(stream, &cmd);
                return ESP_GMF_ERR_FAIL;
            }
            if (has_ext_size && (video_info->width != ext_w || video_info->height != ext_h)) {
                ESP_LOGW(TAG, "Decoder frame size %ux%u differs from extractor %ux%u, keep extractor display size",
                         video_info->width, video_info->height, ext_w, ext_h);
            }
            VIDEO_INFO_TO_PLAYER_INFO(&stream->video_side->track_info, video_info);
            player_send_cmd(stream, &cmd);
        }
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t _video_render_pipe_event_handler(esp_gmf_event_pkt_t *event, void *ctx)
{
    esp_player_stream_t *stream = (esp_player_stream_t *)ctx;
    esp_player_cmd_msg_t cmd = {
        .cmd_type = ESP_PLAYER_CMD_FINISHED,
        .data = stream->video_side->render,
        .data_len = sizeof(stream->video_side->render)};
    if (event->type == ESP_GMF_EVT_TYPE_CHANGE_STATE) {
        switch (event->sub) {
            case ESP_GMF_EVENT_STATE_RUNNING:
                stream->task_status |= TASK_STATUS_VIDEO_RENDER_RUNNING;
                stream->runned_status |= TASK_STATUS_VIDEO_RENDER_RUNNING;
                if (stream->main_state == PLAYER_STATE_PREPARING) {
                    cmd.cmd_type = ESP_PLAYER_CMD_PLAYING;
                    player_send_cmd(stream, &cmd);
                }
                break;
            case ESP_GMF_EVENT_STATE_STOPPED:
                stream->task_status &= ~TASK_STATUS_VIDEO_RENDER_RUNNING;
                ESP_LOGD(TAG, "Video render stopped, task_status: %" PRIu8 " runned_status: %" PRIu8 " expected_tasks: %" PRIu8,
                         stream->task_status, stream->runned_status, stream->expected_tasks);
                break;
            case ESP_GMF_EVENT_STATE_FINISHED:
                ESP_LOGD(TAG, "Video render finished, task_status: %" PRIu8 " runned_status: %" PRIu8 " expected_tasks: %" PRIu8,
                         stream->task_status, stream->runned_status, stream->expected_tasks);
                stream->task_status &= ~TASK_STATUS_VIDEO_RENDER_RUNNING;
                uint8_t exp_cmp = stream->expected_tasks & TASK_STATUS_MASK_NO_RENDER;
                uint8_t run_cmp = stream->runned_status & TASK_STATUS_MASK_NO_RENDER;
                if (run_cmp == exp_cmp && stream->task_status == 0) {
                    player_send_cmd(stream, &cmd);
                }
                break;
            case ESP_GMF_EVENT_STATE_ERROR: {
                ESP_LOGE(TAG, "Video render error");
                stream->task_status &= ~TASK_STATUS_VIDEO_RENDER_RUNNING;
                player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER, "runtime STATE_ERROR");
                int8_t vid_idx = player_video_track_idx(stream);
                if (vid_idx >= 0) {
                    player_extractor_enable_stream(player_extractor_el(stream), ESP_EXTRACTOR_STREAM_TYPE_VIDEO, vid_idx, false);
                }
                cmd.cmd_type = ESP_PLAYER_CMD_ERROR;
                player_send_cmd(stream, &cmd);
                break;
            }
        }
    }

    return ESP_GMF_ERR_OK;
}
