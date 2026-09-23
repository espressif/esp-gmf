/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_data_queue.h"
#include "esp_gmf_err.h"
#include "esp_gmf_payload.h"

#include "player_stream.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define PLAYER_PORTS_EMPTY_LOAD(load)  do {  \
    load->buf        = NULL;                 \
    load->valid_size = 0;                    \
    load->is_done    = true;                 \
} while (0)

#define PLAYER_PORTS_DETACH_BUF(load)  do {  \
    load->buf        = NULL;                 \
    load->valid_size = 0;                    \
} while (0)

/* -------- GMF port callbacks (player_ports.c, audio/video player_ports_*.c) -------- */

esp_gmf_err_io_t extractor_audio_out_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t extractor_video_out_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t decoder_audio_in_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t decoder_audio_in_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t decoder_video_in_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
esp_gmf_err_io_t decoder_video_in_release(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);

/* -------- Component-internal: shared port helpers (player_ports.c) -------- */

esp_gmf_err_io_t player_ports_push_bounded(esp_player_stream_t *stream, esp_gmf_data_queue_t *q,
                                           esp_gmf_payload_t *load, bool is_audio);
esp_gmf_err_io_t player_ports_handle_stop_state(esp_player_stream_t *stream, esp_gmf_payload_t *load, const char *queue_name);

/* Buffer gate for one acquire. False: still closed, retry; the wait is already done. */
bool player_ports_buffer_gate_wait(esp_player_stream_t *stream, bool is_audio_path);

/* Report each frame the queues move; buffered duration is their PTS span. */
void player_ports_buffer_note_queued(esp_player_stream_t *stream, bool is_audio, uint64_t pts_ms);
void player_ports_buffer_note_consumed(esp_player_stream_t *stream, bool is_audio, uint64_t pts_ms);
void player_ports_buffer_reset_tracking(esp_player_stream_t *stream);

/* EOS is in a decoder queue: release the gate so FINISHED can fire. */
void player_ports_buffer_note_source_eos(esp_player_stream_t *stream);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
