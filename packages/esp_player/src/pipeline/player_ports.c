/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "player_ports.h"
#include "player_extractor.h"
#include "player_events.h"
#include "player_submit_frame.h"
#include "player_url.h"
#include "player_internal.h"

static const char *TAG = "ESP_PLAYER_PORTS";

#define PUSH_POLL_TICK_MS    20U
#define PUSH_STALL_LIMIT_MS  2000U

#define PLAYER_BUFFER_DEFAULT_AUDIO_FRAME_MS  (24U)
#define PLAYER_BUFFER_DEFAULT_VIDEO_FRAME_MS  (33U)
#define PLAYER_BUFFER_MAX_FRAME_MS            (1000U)

static inline uint32_t player_queue_depth(QueueHandle_t q)
{
    return q ? uxQueueMessagesWaiting(q) : 0;
}

static inline bool player_audio_decoder_expected(esp_player_stream_t *stream)
{
    return (stream->expected_tasks & TASK_STATUS_AUDIO_DECODER_RUNNING) != 0;
}

static inline bool player_video_decoder_expected(esp_player_stream_t *stream)
{
    return (stream->expected_tasks & TASK_STATUS_VIDEO_DECODER_RUNNING) != 0;
}

static inline player_buffer_ctrl_t *player_buffer_ctrl(esp_player_stream_t *stream)
{
    return stream ? stream->buffer_ctrl : NULL;
}

static void player_buffer_update_avg_frame_ms(uint32_t *avg_ms, uint32_t sample_ms)
{
    if (avg_ms == NULL || sample_ms == 0 || sample_ms > PLAYER_BUFFER_MAX_FRAME_MS) {
        return;
    }
    if (*avg_ms == 0) {
        *avg_ms = sample_ms;
        return;
    }
    *avg_ms = ((*avg_ms * 7U) + sample_ms) / 8U;
}

static uint32_t player_buffer_default_frame_ms(const esp_player_stream_t *stream, bool is_audio)
{
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    if (is_audio && stream->audio_side != NULL) {
        const esp_player_audio_stream_info_t *ai = &stream->audio_side->track_info.audio_info;
        if (ai->sample_rate > 0 && ai->channels > 0 && ai->bits_per_sample > 0) {
            uint32_t byte_rate = ai->sample_rate * (uint32_t)ai->channels * ((uint32_t)ai->bits_per_sample / 8U);
            if (byte_rate > 0) {
                return (2048U * 1000U) / byte_rate;
            }
        }
        return PLAYER_BUFFER_DEFAULT_AUDIO_FRAME_MS;
    }
#else
    (void)is_audio;
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
#if CONFIG_ESP_PLAYER_ENABLE_VIDEO
    if (!is_audio && stream->video_side != NULL) {
        uint16_t fps = stream->video_side->track_info.video_info.fps;
        if (fps > 0) {
            return 1000U / fps;
        }
        return PLAYER_BUFFER_DEFAULT_VIDEO_FRAME_MS;
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_VIDEO */
    return is_audio ? PLAYER_BUFFER_DEFAULT_AUDIO_FRAME_MS : PLAYER_BUFFER_DEFAULT_VIDEO_FRAME_MS;
}

static uint32_t player_buffer_track_frame_ms(const esp_player_stream_t *stream, bool is_audio)
{
    const player_buffer_ctrl_t *ctrl = player_buffer_ctrl((esp_player_stream_t *)stream);
    if (ctrl != NULL) {
        uint32_t avg = is_audio ? ctrl->avg_audio_frame_ms : ctrl->avg_video_frame_ms;
        if (avg > 0) {
            return avg;
        }
    }
    return player_buffer_default_frame_ms(stream, is_audio);
}

static uint32_t player_buffer_track_buffered_ms(esp_player_stream_t *stream, bool is_audio)
{
    QueueHandle_t q = NULL;
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    if (is_audio && stream->audio_side != NULL) {
        q = stream->audio_side->extractor_queue;
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
#if CONFIG_ESP_PLAYER_ENABLE_VIDEO
    if (!is_audio && stream->video_side != NULL) {
        q = stream->video_side->extractor_queue;
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_VIDEO */
    if (q == NULL) {
        return UINT32_MAX;
    }
    uint32_t frame_ms = player_buffer_track_frame_ms(stream, is_audio);
    return player_queue_depth(q) * frame_ms;
}

static uint32_t player_buffer_effective_ms(esp_player_stream_t *stream)
{
    uint32_t effective = UINT32_MAX;
    if (player_audio_decoder_expected(stream)) {
        effective = player_buffer_track_buffered_ms(stream, true);
    }
    if (player_video_decoder_expected(stream)) {
        uint32_t video_ms = player_buffer_track_buffered_ms(stream, false);
        if (video_ms < effective) {
            effective = video_ms;
        }
    }
    if (effective == UINT32_MAX) {
        return 0;
    }
    return effective;
}

static bool buffer_gate_resume_ready(esp_player_stream_t *stream)
{
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    if (ctrl == NULL) {
        return false;
    }
    uint32_t threshold = (ctrl->gate_state == ESP_PLAYER_BUFFER_GATE_PRE_BUFFERING)
                             ? player_cfg_prebuffer_resume_ms(stream)
                             : player_cfg_rebuffer_resume_ms(stream);
    return player_buffer_effective_ms(stream) >= threshold;
}

esp_gmf_err_io_t player_ports_push_bounded(esp_player_stream_t *stream, QueueHandle_t q, esp_gmf_payload_t *load,
                                           bool is_audio)
{
    const TickType_t per_wait = pdMS_TO_TICKS(PUSH_POLL_TICK_MS);
    const TickType_t total_limit = pdMS_TO_TICKS(PUSH_STALL_LIMIT_MS);
    TickType_t waited = 0;
    for (;;) {
        if (stream->_is_stop
            || stream->error_source == ESP_PLAYER_ERROR_SOURCE_EXTRACTOR
            || (is_audio && stream->error_source == ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER)
            || (!is_audio && stream->error_source == ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER)) {
            return ESP_GMF_IO_ABORT;
        }
        if (xQueueSend(q, load, per_wait) == pdTRUE) {
            return ESP_GMF_IO_OK;
        }
        if (stream->main_state == PLAYER_STATE_PAUSED) {
            waited = 0;
            continue;
        }
        /* Queue may stay full while decoder waits on PRE/RE gate; not a downstream fault. */
        if (stream->buffer_ctrl != NULL
            && stream->buffer_ctrl->gate_state != ESP_PLAYER_BUFFER_GATE_NONE) {
            waited = 0;
            continue;
        }
        waited += per_wait;
        if (waited >= total_limit) {
            ESP_LOGW(TAG, "%s extractor push stalled %lums - treating as downstream fault",
                     is_audio ? "Audio" : "Video",
                     (unsigned long)pdTICKS_TO_MS(waited));
            return ESP_GMF_IO_FAIL;
        }
    }
}

esp_gmf_err_io_t player_ports_handle_stop_state(esp_player_stream_t *stream, esp_gmf_payload_t *load, const char *queue_name)
{
    if (stream->_is_stop) {
        ESP_LOGE(TAG, "%s out queue receive abort", queue_name);
        player_release_payload(stream, load);
        PLAYER_PORTS_EMPTY_LOAD(load);
        return ESP_GMF_IO_ABORT;
    }
    return ESP_GMF_IO_OK;
}

esp_gmf_err_io_t player_release_payload(esp_player_stream_t *stream, esp_gmf_payload_t *load)
{
    if (load == NULL || load->buf == NULL) {
        return ESP_GMF_IO_OK;
    }
    switch (stream->dec_frame_mode) {
        case ESP_PLAYER_DEC_FRAME_MODE_FILL: {
            frame_pool_slot_t *slot = frame_pool_find_by_buf(stream->fill_pool, load->buf);
            frame_pool_release(stream->fill_pool, slot);
            return ESP_GMF_IO_OK;
        }
        case ESP_PLAYER_DEC_FRAME_MODE_BLOCK:
            player_set_events(stream, _CTRL_DECODER_FRAME_DONE);
            return ESP_GMF_IO_OK;
        case ESP_PLAYER_DEC_FRAME_MODE_EXTRACTOR:
        default: {
            esp_extractor_frame_info_t frame_info;
            frame_info.frame_buffer = load->buf;
            frame_info.frame_size = load->valid_size;
            if (player_extractor_release_frame(player_extractor_el(stream), &frame_info) != ESP_GMF_ERR_OK) {
                ESP_LOGE(TAG, "Failed to release frame, line: %d", __LINE__);
                return ESP_GMF_IO_FAIL;
            }
            return ESP_GMF_IO_OK;
        }
    }
}

void player_ports_buffer_note_extractor_frame(esp_player_stream_t *stream, bool is_audio)
{
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    if (ctrl == NULL) {
        return;
    }
    esp_gmf_element_handle_t ext_el = player_extractor_el(stream);
    uint64_t delta_ms = 0;
    if (ext_el == NULL || player_extractor_get_delta_pts(ext_el, &delta_ms) != ESP_GMF_ERR_OK) {
        return;
    }
    if (delta_ms == 0 || delta_ms > PLAYER_BUFFER_MAX_FRAME_MS) {
        return;
    }
    if (is_audio) {
        player_buffer_update_avg_frame_ms(&ctrl->avg_audio_frame_ms, (uint32_t)delta_ms);
    } else {
        player_buffer_update_avg_frame_ms(&ctrl->avg_video_frame_ms, (uint32_t)delta_ms);
    }
}

void player_ports_buffer_gate_try_enter(esp_player_stream_t *stream, bool is_audio_path)
{
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    if (ctrl == NULL || ctrl->gate_state != ESP_PLAYER_BUFFER_GATE_NONE
        || stream->main_state != PLAYER_STATE_PLAYING || !_player_is_network_source_uri(stream)) {
        return;
    }

    bool need_track = is_audio_path ? player_audio_decoder_expected(stream)
                                    : player_video_decoder_expected(stream);
    if (!need_track) {
        return;
    }

    uint32_t effective_ms = player_buffer_effective_ms(stream);
    uint32_t enter_ms = player_cfg_rebuffer_enter_ms(stream);
    if (effective_ms > enter_ms) {
        ctrl->low_since = 0;
        return;
    }

    TickType_t grace = pdMS_TO_TICKS(player_cfg_rebuffer_grace_ms(stream));
    TickType_t now = xTaskGetTickCount();
    if (ctrl->low_since == 0) {
        ctrl->low_since = now;
        return;
    }
    if ((now - ctrl->low_since) < grace) {
        return;
    }

    bool entered = false;
    if (stream->lock_resource && xSemaphoreTake(stream->lock_resource, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (ctrl->gate_state == ESP_PLAYER_BUFFER_GATE_NONE) {
            ctrl->gate_state = ESP_PLAYER_BUFFER_GATE_RE_BUFFERING;
            ctrl->low_since = 0;
            entered = true;
        }
        xSemaphoreGive(stream->lock_resource);
    }
    if (entered) {
        esp_player_event_msg_t event_msg = {
            .event_type = ESP_PLAYER_EVENT_BUFFERING,
            .data = NULL,
            .data_len = 0,
        };
        player_send_event(stream, &event_msg);
    }
}

bool player_ports_buffer_gate_try_leave(esp_player_stream_t *stream)
{
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    if (ctrl == NULL) {
        return false;
    }
    bool left = false;
    if (stream->lock_resource && xSemaphoreTake(stream->lock_resource, pdMS_TO_TICKS(10)) == pdTRUE) {
        if (ctrl->gate_state != ESP_PLAYER_BUFFER_GATE_NONE
            && buffer_gate_resume_ready(stream)) {
            ctrl->gate_state = ESP_PLAYER_BUFFER_GATE_NONE;
            ctrl->low_since = 0;
            left = true;
        }
        xSemaphoreGive(stream->lock_resource);
    }
    if (left) {
        esp_player_event_msg_t event_msg = {
            .event_type = ESP_PLAYER_EVENT_BUFFERED,
            .data = NULL,
            .data_len = 0,
        };
        player_send_event(stream, &event_msg);
    }
    return left;
}
