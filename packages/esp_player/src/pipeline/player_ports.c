/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>

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

/* Gate poll period. Paused polls slowly: the gate cannot open until playback resumes. */
#define PLAYER_GATE_POLL_MS         (10U)
#define PLAYER_GATE_POLL_PAUSED_MS  (100U)

/* Consecutive WAITING_OUTPUT retries that mean the demux pool is full.
 * The extractor retries every 10 ms, so 50 is about 500 ms. */
#define PLAYER_BUFFER_POOL_SATURATED_RETRIES  (50U)

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

/* PTS span still between extractor and decoder (newest queued − newest consumed). */
static uint32_t player_buffer_track_buffered_ms(esp_player_stream_t *stream, bool is_audio)
{
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    esp_gmf_data_queue_t *q = NULL;
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    if (is_audio && stream->audio_side != NULL) {
        q = stream->audio_side->frame_queue;
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
#if CONFIG_ESP_PLAYER_ENABLE_VIDEO
    if (!is_audio && stream->video_side != NULL) {
        q = stream->video_side->frame_queue;
    }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_VIDEO */
    if (q == NULL || ctrl == NULL) {
        return UINT32_MAX;
    }
    const player_buffer_span_t *buf_span = is_audio ? &ctrl->audio : &ctrl->video;
    if (!buf_span->valid) {
        return 0;
    }
    if (buf_span->write_pts <= buf_span->read_pts) {
        return 0;
    }
    uint64_t span = buf_span->write_pts - buf_span->read_pts;
    /* UINT32_MAX means "this track does not constrain the gate". */
    return span >= UINT32_MAX ? (UINT32_MAX - 1U) : (uint32_t)span;
}

static uint32_t player_buffer_effective_ms(esp_player_stream_t *stream)
{
    bool want_audio = player_audio_decoder_expected(stream);
    bool want_video = player_video_decoder_expected(stream);
    if (!want_audio && !want_video) {
        return 0;
    }
    if (want_audio && !want_video) {
        return player_buffer_track_buffered_ms(stream, true);
    }
    if (!want_audio && want_video) {
        return player_buffer_track_buffered_ms(stream, false);
    }
    /* AV: follow the master clock. MIN(A, V) would close the gate when video runs ahead. */
    esp_player_sync_mode_t mode = ESP_PLAYER_SYNC_MODE_AUDIO;
    if (player_sync_get_mode(stream->sync_handle, &mode) != ESP_PLAYER_ERR_OK) {
        mode = ESP_PLAYER_SYNC_MODE_AUDIO;
    }
    if (mode == ESP_PLAYER_SYNC_MODE_VIDEO) {
        return player_buffer_track_buffered_ms(stream, false);
    }
    return player_buffer_track_buffered_ms(stream, true);
}

/* Gated decoders return nothing to the pool; waiting further cannot grow the buffer. */
static bool buffer_gate_pool_saturated(esp_player_stream_t *stream)
{
    uint8_t wait_count = 0;
    esp_gmf_element_handle_t ext_el = player_extractor_el(stream);
    if (ext_el == NULL
        || player_extractor_get_output_wait_count(ext_el, &wait_count) != ESP_GMF_ERR_OK) {
        return false;
    }
    return wait_count >= PLAYER_BUFFER_POOL_SATURATED_RETRIES;
}

static bool buffer_gate_resume_ready(esp_player_stream_t *stream)
{
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    if (ctrl == NULL) {
        return false;
    }
    /* EOS is already queued; no refill can reach the resume threshold. */
    if (ctrl->source_eos) {
        return true;
    }
    uint32_t threshold = (ctrl->gate_state == ESP_PLAYER_BUFFER_GATE_PRE_BUFFERING)
                             ? player_cfg_prebuffer_resume_ms(stream)
                             : player_cfg_rebuffer_resume_ms(stream);
    uint32_t effective_ms = player_buffer_effective_ms(stream);
    if (effective_ms >= threshold) {
        ctrl->extractor_push_blocked = false;
        return true;
    }
    /* Extractor cannot push while gated; leave so the decoder can drain. */
    if (ctrl->extractor_push_blocked) {
        ctrl->extractor_push_blocked = false;
        ESP_LOGD(TAG, "Extractor push blocked at %" PRIu32 "ms buffered (resume needs %" PRIu32 "ms), resuming",
                 effective_ms, threshold);
        return true;
    }
    /* Paused: the front stages fill the pool by design, so a full pool proves nothing. */
    if (stream->main_state == ESP_PLAYER_STATE_PAUSED || !buffer_gate_pool_saturated(stream)) {
        return false;
    }
    /* Only RE_BUFFERING may latch: `pool_limited` suppresses RE_BUFFERING alone, and
     * PRE_BUFFERING failed against a different threshold. */
    if (ctrl->gate_state == ESP_PLAYER_BUFFER_GATE_RE_BUFFERING && !ctrl->pool_limited) {
        ctrl->pool_limited = true;
        ESP_LOGW(TAG, "Demux pool saturated at %" PRIu32 "ms buffered (resume needs %" PRIu32 "ms), "
                      "resuming and disabling the re-buffering gate for this stream",
                 effective_ms, threshold);
    }
    return true;
}

static void player_ports_buffer_gate_try_enter(esp_player_stream_t *stream, bool is_audio_path)
{
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    /* After EOS the tail only drains; do not re-enter. */
    if (ctrl == NULL || ctrl->gate_state != ESP_PLAYER_BUFFER_GATE_NONE || ctrl->pool_limited
        || ctrl->source_eos || stream->main_state != ESP_PLAYER_STATE_PLAYING
        || !_player_is_network_source_uri(stream)) {
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

static bool player_ports_buffer_gate_try_leave(esp_player_stream_t *stream)
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

esp_gmf_err_io_t player_ports_push_bounded(esp_player_stream_t *stream, esp_gmf_data_queue_t *q,
                                           esp_gmf_payload_t *load,
                                           bool is_audio)
{
    /* frame_queue / esp_gmf_data_queue timeouts are milliseconds, not FreeRTOS ticks. */
    const uint32_t per_wait_ms = PUSH_POLL_TICK_MS;
    const uint32_t total_limit_ms = PUSH_STALL_LIMIT_MS;
    uint32_t waited_ms = 0;
    for (;;) {
        if (stream->_is_stop
            || stream->error_source == ESP_PLAYER_ERROR_SOURCE_EXTRACTOR
            || (is_audio && stream->error_source == ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER)
            || (!is_audio && stream->error_source == ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER)) {
            return ESP_GMF_IO_ABORT;
        }
        if (player_frame_queue_push_ref(q, load, ESP_PLAYER_DEC_FRAME_MODE_EXTRACTOR,
                                        per_wait_ms) == ESP_GMF_IO_OK) {
            return ESP_GMF_IO_OK;
        }
        if (stream->main_state == ESP_PLAYER_STATE_PAUSED) {
            waited_ms = 0;
            continue;
        }
        /* Decoder is gated: queue-full is not a downstream fault. */
        if (stream->buffer_ctrl != NULL
            && stream->buffer_ctrl->gate_state != ESP_PLAYER_BUFFER_GATE_NONE) {
            stream->buffer_ctrl->extractor_push_blocked = true;
            waited_ms = 0;
            continue;
        }
        waited_ms += per_wait_ms;
        if (waited_ms >= total_limit_ms) {
            ESP_LOGW(TAG, "%s extractor push stalled %lums - treating as downstream fault",
                     is_audio ? "Audio" : "Video",
                     (unsigned long)waited_ms);
            return ESP_GMF_IO_FAIL;
        }
    }
}

esp_gmf_err_io_t player_ports_handle_stop_state(esp_player_stream_t *stream, esp_gmf_payload_t *load, const char *queue_name)
{
    if (stream->_is_stop) {
        ESP_LOGD(TAG, "%s out push abort", queue_name);
        player_release_extractor_payload(stream, load);
        PLAYER_PORTS_EMPTY_LOAD(load);
        return ESP_GMF_IO_ABORT;
    }
    return ESP_GMF_IO_OK;
}

esp_gmf_err_io_t player_release_extractor_payload(esp_player_stream_t *stream, esp_gmf_payload_t *load)
{
    if (stream == NULL || load == NULL) {
        return ESP_GMF_IO_OK;
    }
    uint8_t *buf = __atomic_load_n(&load->buf, __ATOMIC_SEQ_CST);
    if (buf == NULL || !__atomic_compare_exchange_n(&load->buf, &buf, NULL, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) {
        return ESP_GMF_IO_OK;
    }
    uint32_t valid_size = __atomic_exchange_n(&load->valid_size, 0u, __ATOMIC_SEQ_CST);

    esp_extractor_frame_info_t frame_info = {
        .frame_buffer = buf,
        .frame_size = valid_size,
    };
    if (player_extractor_release_frame(player_extractor_el(stream), &frame_info) != ESP_GMF_ERR_OK) {
        ESP_LOGE(TAG, "Failed to release frame, line: %d", __LINE__);
        return ESP_GMF_IO_FAIL;
    }
    return ESP_GMF_IO_OK;
}

void player_ports_buffer_note_queued(esp_player_stream_t *stream, bool is_audio, uint64_t pts_ms)
{
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    if (ctrl == NULL) {
        return;
    }
    player_buffer_span_t *buf_span = is_audio ? &ctrl->audio : &ctrl->video;
    if (buf_span->valid == false) {
        /* First frame of a track: the span starts here (PTS may not be 0). */
        buf_span->write_pts = pts_ms;
        buf_span->read_pts = pts_ms;
        buf_span->valid = true;
        return;
    }
    /* Reordered frames can have a lower PTS; keep the newest as the head. */
    if (pts_ms > buf_span->write_pts) {
        buf_span->write_pts = pts_ms;
    }
}

void player_ports_buffer_note_consumed(esp_player_stream_t *stream, bool is_audio, uint64_t pts_ms)
{
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    if (ctrl == NULL) {
        return;
    }
    player_buffer_span_t *buf_span = is_audio ? &ctrl->audio : &ctrl->video;
    if (buf_span->valid == false) {
        return;
    }
    if (pts_ms > buf_span->read_pts) {
        buf_span->read_pts = pts_ms;
    }
}

void player_ports_buffer_reset_tracking(esp_player_stream_t *stream)
{
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    if (ctrl == NULL) {
        return;
    }
    /* Seek / new URL: drop the old PTS span and EOS marker. */
    ctrl->audio = (player_buffer_span_t) {0};
    ctrl->video = (player_buffer_span_t) {0};
    ctrl->source_eos = false;
}

void player_ports_buffer_note_source_eos(esp_player_stream_t *stream)
{
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    if (ctrl == NULL || ctrl->source_eos) {
        return;
    }
    ctrl->source_eos = true;
    ESP_LOGD(TAG, "Source EOS queued, buffering gate disabled for the remaining tail");
}

bool player_ports_buffer_gate_wait(esp_player_stream_t *stream, bool is_audio_path)
{
    player_ports_buffer_gate_try_enter(stream, is_audio_path);
    player_buffer_ctrl_t *ctrl = player_buffer_ctrl(stream);
    if (ctrl == NULL || ctrl->gate_state == ESP_PLAYER_BUFFER_GATE_NONE) {
        return true;
    }
    if (player_ports_buffer_gate_try_leave(stream)) {
        return true;
    }
    vTaskDelay(pdMS_TO_TICKS(stream->main_state == ESP_PLAYER_STATE_PAUSED
                                 ? PLAYER_GATE_POLL_PAUSED_MS : PLAYER_GATE_POLL_MS));
    return false;
}
