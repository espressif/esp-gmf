/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "player_ports.h"
#include "player_events.h"
#include "player_url.h"

#include "freertos/task.h"

static const char *TAG = "ESP_PLAYER_PORTS";

esp_gmf_err_io_t extractor_video_out_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    ESP_LOGD(TAG, "extractor_video_out_release");
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    (void)wanted_size;
    (void)wait_ticks;
    esp_gmf_err_io_t ret = player_ports_handle_stop_state(stream, load, "Video");
    if (ret != ESP_GMF_IO_OK) {
        return ret;
    }
    if (!stream->video_side || stream->video_side->extractor_queue == NULL) {
        player_release_payload(stream, load);
        PLAYER_PORTS_EMPTY_LOAD(load);
        return ESP_GMF_IO_OK;
    }
    esp_gmf_err_io_t push_ret =
        player_ports_push_bounded(stream, stream->video_side->extractor_queue, load, false);
    if (push_ret == ESP_GMF_IO_OK) {
        player_ports_buffer_note_extractor_frame(stream, false);
        QueueHandle_t aud_q = stream->audio_side ? stream->audio_side->extractor_queue : NULL;
        bool aud_queue_idle = aud_q && (uxQueueMessagesWaiting(aud_q) == 0);
        if ((player_audio_track_idx(stream) < 0 || aud_queue_idle) && (!stream->is_seeking)) {
            const uint16_t fps = stream->video_side->track_info.video_info.fps;
            const uint32_t frame_period_ms = (fps > 0) ? (1000 / fps) : 0;
            const uint32_t yield_ms = frame_period_ms * 70 / 100;
            vTaskDelay(pdMS_TO_TICKS(yield_ms));
        }
        return ESP_GMF_IO_OK;
    }
    player_release_payload(stream, load);
    PLAYER_PORTS_EMPTY_LOAD(load);
    return push_ret;
}

esp_gmf_err_io_t decoder_video_in_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    TickType_t recv_wait = portMAX_DELAY;
    if (stream->buffer_ctrl && stream->main_state == PLAYER_STATE_PLAYING
        && stream->buffer_ctrl->gate_state == ESP_PLAYER_BUFFER_GATE_NONE
        && _player_is_network_source_uri(stream)) {
        recv_wait = pdMS_TO_TICKS(10);
    }
_rec_dec_video_in_frame:
    if (stream->_is_stop || stream->error_source == ESP_PLAYER_ERROR_SOURCE_EXTRACTOR || stream->error_source == ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER) {
        ESP_LOGE(TAG, "Video queue receive abort, line: %d", __LINE__);
        PLAYER_PORTS_EMPTY_LOAD(load);
        esp_gmf_db_handle_t vid_db = player_video_db(stream);
        if (vid_db) {
            esp_gmf_db_abort(vid_db);
        }
        return ESP_GMF_IO_ABORT;
    }
    player_ports_buffer_gate_try_enter(stream, false);
    if (stream->buffer_ctrl && stream->buffer_ctrl->gate_state != ESP_PLAYER_BUFFER_GATE_NONE) {
        if (player_ports_buffer_gate_try_leave(stream) == false) {
            vTaskDelay(pdMS_TO_TICKS(10));
            goto _rec_dec_video_in_frame;
        }
    }
    if (xQueueReceive(stream->video_side->extractor_queue, load, recv_wait) == pdTRUE) {
        if (stream->is_seeking
            && load->pts >= player_sync_get_seek_target(stream->sync_handle)) {
            player_set_events(stream, _CTRL_PLAYER_DECODER_VIDEO_SEEK_DONE);
        }
        if (stream->_is_stop) {
            if (load->buf) {
                player_release_payload(stream, load);
            }
            PLAYER_PORTS_EMPTY_LOAD(load);
            esp_gmf_db_handle_t vid_db = player_video_db(stream);
            if (vid_db) {
                esp_gmf_db_done_write(vid_db);
            }
            return ESP_GMF_IO_ABORT;
        }
        if (load->is_done == true) {
            esp_gmf_db_handle_t vid_db = player_video_db(stream);
            if (vid_db) {
                esp_gmf_db_done_write(vid_db);
            }
        }
        if (load->valid_size == 0) {
            return ESP_GMF_IO_OK;
        }
        if (stream->sync_handle && stream->main_state == PLAYER_STATE_PLAYING && !stream->is_seeking) {
            if (player_sync_video_decode_frame(stream->sync_handle, load->pts) == false && load->is_done == false) {
                ESP_LOGD(TAG, "Drop video frame");
                if (player_release_payload(stream, load) != ESP_GMF_IO_OK) {
                    return ESP_GMF_IO_FAIL;
                }
                load->pts = 0;
                load->meta_flag = 0;
                PLAYER_PORTS_EMPTY_LOAD(load);
                goto _rec_dec_video_in_frame;
            }
        }
        return ESP_GMF_IO_OK;
    } else {
        if (recv_wait != portMAX_DELAY) {
            goto _rec_dec_video_in_frame;
        }
        ESP_LOGE(TAG, "Video queue receive abort, line: %d", __LINE__);
        esp_gmf_db_handle_t vid_db = player_video_db(stream);
        if (vid_db) {
            esp_gmf_db_abort(vid_db);
        }
        PLAYER_PORTS_EMPTY_LOAD(load);
        return ESP_GMF_IO_ABORT;
    }
}

esp_gmf_err_io_t decoder_video_in_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    (void)wanted_size;
    (void)wait_ticks;
    if (stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_UNKNOWN) {
        return ESP_GMF_IO_OK;
    }
    if (stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_FILL
        || stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_BLOCK) {
        return player_release_payload(stream, load);
    }
    esp_gmf_db_handle_t vid_db = player_video_db(stream);
    if (load->is_done == true && vid_db != NULL) {
        esp_gmf_db_done_write(vid_db);
    }
    return player_release_payload(stream, load);
}
