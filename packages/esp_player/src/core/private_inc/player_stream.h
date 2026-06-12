/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "sdkconfig.h"

#include "esp_gmf_io.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_task.h"
#include "esp_gmf_event.h"
#include "esp_gmf_new_databus.h"
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
#include "esp_gmf_audio_dec.h"
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

#include "esp_player_types.h"
#include "esp_player.h"
#include "esp_player_advance.h"
#include "player_sync.h"
#include "player_helper.h"
#include "player_extractor.h"
#include "player_data_bus.h"
#include "player_state.h"
#include "player_defaults_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ESP_PLAYER_TAG  "ESP_PLAYER"

#define ESP_PLAYER_CMD_QUEUE_SIZE     (20)
#define ESP_PLAYER_AUDIO_SPEED        (1.0)
#define ESP_PLAYER_VIDEO_SPEED        (1.0)
#define ESP_PLAYER_READ_WAIT_TIME_MS  (10 * 1000)

#define ESP_PLAYER_VIDEO_QUEUE_SIZE  (128)
#define ESP_PLAYER_AUDIO_QUEUE_SIZE  (128)

#define ESP_PLAYER_VIDEO_BLOCK_NUM        (4)
#define ESP_PLAYER_AUDIO_RB_TIME_MS       (400)
#define ESP_PLAYER_AUDIO_RENDER_FRAME_MS  (ESP_PLAYER_AUDIO_RB_TIME_MS >> 2)

#define LOCK_TIMEOUT_MS  5000
#define TASK_TIMEOUT_MS  60000

#define EXTRACTOR_TAG      "extractor"
#define AUDIO_DECODER_TAG  "aud_dec"
#define VIDEO_DECODER_TAG  "vid_dec"
#define AUDIO_RENDER_TAG   "aud_render"
#define VIDEO_RENDER_TAG   "vid_render"

#define TASK_STATUS_EXTRACTOR_RUNNING      (1u << 0)
#define TASK_STATUS_AUDIO_DECODER_RUNNING  (1u << 1)
#define TASK_STATUS_VIDEO_DECODER_RUNNING  (1u << 2)
#define TASK_STATUS_AUDIO_RENDER_RUNNING   (1u << 3)
#define TASK_STATUS_VIDEO_RENDER_RUNNING   (1u << 4)
#define TASK_STATUS_ALL_RUNNING            (TASK_STATUS_EXTRACTOR_RUNNING | TASK_STATUS_AUDIO_DECODER_RUNNING | TASK_STATUS_VIDEO_DECODER_RUNNING | TASK_STATUS_AUDIO_RENDER_RUNNING | TASK_STATUS_VIDEO_RENDER_RUNNING)
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO && CONFIG_ESP_PLAYER_ENABLE_VIDEO
#define TASK_STATUS_MASK_NO_RENDER  (TASK_STATUS_EXTRACTOR_RUNNING | TASK_STATUS_AUDIO_DECODER_RUNNING | TASK_STATUS_VIDEO_DECODER_RUNNING)
#elif CONFIG_ESP_PLAYER_ENABLE_AUDIO
#define TASK_STATUS_MASK_NO_RENDER  (TASK_STATUS_EXTRACTOR_RUNNING | TASK_STATUS_AUDIO_DECODER_RUNNING)
#elif CONFIG_ESP_PLAYER_ENABLE_VIDEO
#define TASK_STATUS_MASK_NO_RENDER  (TASK_STATUS_EXTRACTOR_RUNNING | TASK_STATUS_VIDEO_DECODER_RUNNING)
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO && CONFIG_ESP_PLAYER_ENABLE_VIDEO */

/**
 * @brief  Player frame mode structure
 */
typedef enum {
    ESP_PLAYER_DEC_FRAME_MODE_EXTRACTOR,  /*!< Extractor mode */
    ESP_PLAYER_DEC_FRAME_MODE_FILL,       /*!< External frames: deep copy (pool-backed) */
    ESP_PLAYER_DEC_FRAME_MODE_BLOCK,      /*!< External frames: zero-copy */
    ESP_PLAYER_DEC_FRAME_MODE_UNKNOWN,    /*!< Unknown mode */
} esp_player_dec_frame_mode_t;

typedef struct frame_pool_s frame_pool_t;

/**
 * @brief  Per-track "side" (audio or video) — holds everything that only
 *         matters when that track type is selected via `av_mask`.
 *
 *         The two sides are allocated on demand in `esp_player_set_av_mask()`
 *         (see `player_side_reconcile()`) and freed either when the mask drops
 *         the bit or at `esp_player_deinit()`. An audio-only player therefore
 *         never pays for video fields (and vice versa).
 */
typedef struct {
    esp_gmf_pipeline_handle_t  decoder;          /*!< Audio decoder pipeline handle */
    esp_gmf_pipeline_handle_t  render;           /*!< Audio renderer pipeline handle */
    QueueHandle_t              extractor_queue;  /*!< Queue carrying extracted audio payloads */
    esp_player_track_info_t    track_info;       /*!< Cached audio track metadata */
    player_data_bus_t         *data_bus;         /*!< Player data bus with meta sidecar (decoder↔render) */
} player_audio_side_t;

typedef struct {
    esp_gmf_pipeline_handle_t  decoder;          /*!< Video decoder pipeline handle */
    esp_gmf_pipeline_handle_t  render;           /*!< Video renderer pipeline handle */
    QueueHandle_t              extractor_queue;  /*!< Queue carrying extracted video payloads */
    esp_player_track_info_t    track_info;       /*!< Cached video track metadata */
    player_data_bus_t         *data_bus;         /*!< Player data bus with meta sidecar (decoder↔render) */
} player_video_side_t;

typedef enum {
    ESP_PLAYER_BUFFER_GATE_NONE          = 0,  /*!< No buffering gate */
    ESP_PLAYER_BUFFER_GATE_PRE_BUFFERING = 1,  /*!< Startup pre-buffering gate */
    ESP_PLAYER_BUFFER_GATE_RE_BUFFERING  = 2,  /*!< Runtime re-buffering gate */
} esp_player_buffer_gate_t;

typedef struct {
    esp_player_buffer_gate_t  gate_state;          /*!< Buffer gate sub-state (PRE_BUFFERING/RE_BUFFERING/NONE). */
    TickType_t                low_since;           /*!< Effective-buffer low-watermark enter tick (0 = inactive). */
    uint32_t                  avg_audio_frame_ms;  /*!< EWMA audio frame duration for queue→ms estimate. */
    uint32_t                  avg_video_frame_ms;  /*!< EWMA video frame duration for queue→ms estimate. */
} player_buffer_ctrl_t;

typedef struct esp_player_stream {
    /* Protect */
    SemaphoreHandle_t   lock;           /*!< API mutex: cmd enqueue & short sections; release before blocking sync wait. */
    SemaphoreHandle_t   lock_resource;  /*!< Pipeline/stop/dec_cfg/buffer-gate; longer holds, usually finite timeout. */
    EventGroupHandle_t  sync_evt;       /*!< Wait/advertise bits for blocking APIs (e.g. run/stop). */
    TaskHandle_t        cmd_task;       /*!< Command task handle */
    QueueHandle_t       cmd_queue;      /*!< Command queue handle */

    /* Properties */
    void                         *audio_render_hd;  /*!< esp_audio_render_stream_handle_t from esp_audio_render_stream_get() */
    void                         *video_render_hd;  /*!< esp_video_render_handle_t from esp_video_render_create() */
    esp_player_custom_elements_t *custom_elements;  /*!< Custom decoders; player-owned copy from set_custom_elements() */
    esp_player_task_config_t     *task_cfg;         /*!< NULL = built-in defaults; see esp_player_set_task_config() */
    esp_player_buffer_config_t   *buffer_cfg;       /*!< NULL = built-in defaults; see esp_player_set_buffer_config() */
    esp_player_event_callback_t   event_cb;         /*!< Event callback (set_event_cb) */
    void                         *event_ctx;        /*!< Callback context */
    QueueHandle_t                 event_queue;      /*!< Async `esp_player_event_msg_t` queue (set_event_queue) */

    /* Private */
    esp_gmf_io_handle_t        input_handle;  /*!< Input IO handle */
    esp_gmf_pool_handle_t      io_pool;       /*!< Input IO pool handle */
    esp_gmf_pipeline_handle_t  extractor;     /*!< Extractor pipeline; av_mask == AV */
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
    esp_audio_simple_dec_cfg_t  dec_cfg;          /*!< Decoder cfg; dec_cfg.heap player-owned when set */
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
    esp_player_dec_frame_mode_t  dec_frame_mode;  /*!< Decoder frame mode */
    char                        *frame_url;       /*!< Active fill/block URL; owned by player */
    frame_pool_t                *fill_pool;       /*!< FILL mode pool; NULL otherwise */
    player_audio_side_t         *audio_side;      /*!< Audio track side (lazy alloc) */
    player_video_side_t         *video_side;      /*!< Video track side (lazy alloc) */
    player_sync_handle_t         sync_handle;     /*!< PTS / sync logic (reused across runs) */
    player_buffer_ctrl_t        *buffer_ctrl;     /*!< Optional network rebuffer ctrl */

    /* Private (masks & flags) */
    uint8_t  av_mask;         /*!< Audio/Video mask */
    uint8_t  task_status;     /*!< TASK_STATUS_* running bits */
    uint8_t  expected_tasks;  /*!< Expected TASK_STATUS_* bits */
    uint8_t  runned_status;   /*!< Reached RUNNING at least once */
    bool     _is_stop;        /*!< Stop requested (internal) */
    bool     is_seeking;      /*!< Seek in flight */
    bool     input_opened;    /*!< Input IO opened */

    /* Private (FSM) */
    esp_player_state_t         main_state;    /*!< High-level player state */
    esp_player_error_source_t  error_source;  /*!< Sticky error source; cleared on ERROR→IDLE recovery */
} esp_player_stream_t;

/** @brief  Signature of the pipeline factory accepted by `player_create_pipeline_if_expected`. */
typedef esp_player_err_t (*player_pipeline_factory_t)(esp_player_stream_t *stream, bool is_audio);

/**
 * @brief  Return a payload's buffer to whoever produced it.
 *
 *         The right "undo" action depends on how the payload entered the
 *         pipeline, which the caller does not need to know:
 *         - EXTRACTOR mode : returned to the extractor element's free-list
 *         - FILL mode      : pool slot's in-use bit is cleared
 *         - BLOCK mode     : signal decoder-frame-done (user buffer not freed here)
 *
 *         Every drain / drop-frame / stop-abort path routes through this
 *         function so that ownership is honoured regardless of mode.
 *         Implemented in player_ports.c; declared here because the
 *         `player_drop_single_queue` (player_stream.c) depends on it.
 */
extern esp_gmf_err_io_t player_release_payload(esp_player_stream_t *stream, esp_gmf_payload_t *load);

#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
/**
 * @brief  Ensure heap-backed default sub-config for audio dec_cfg.
 *
 * @note  Implemented in src/audio/player_dec_cfg.c. Declared here (instead of player_adec_defaults_cfg.h)
 *        because it depends on the full esp_player_stream_t typedef defined above.
 */
esp_player_err_t player_prepare_dec_cfg(esp_player_stream_t *stream, esp_player_format_t format);

/**
 * @brief  Free the heap-allocated decoder sub-config buffer attached to
 *         `stream->dec_cfg.dec_cfg` (owned by the player for non-simple codecs)
 *         and reset `cfg_size` to 0.
 *
 * @note  Implemented in src/audio/player_dec_cfg.c. Declared here for the same reason as
 *        player_prepare_dec_cfg.
 */
void player_free_dec_subcfg_heap(esp_player_stream_t *stream);
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */

/**
 * @brief  Retrieve the databus attached to a pipeline through its (single) element port ctx.
 *
 * @note  Implemented in player_stream.c (single copy; was static inline per TU).
 */
esp_gmf_db_handle_t player_get_db(esp_gmf_pipeline_handle_t pipe, bool use_out_port);

/* player_glue_{audio,video}.c when enabled; else player_stubs_{audio,video}.c */
esp_gmf_db_handle_t player_audio_db(esp_player_stream_t *stream);
esp_gmf_db_handle_t player_video_db(esp_player_stream_t *stream);

esp_gmf_element_handle_t player_extractor_el(esp_player_stream_t *stream);

int8_t player_audio_track_idx(esp_player_stream_t *stream);
int8_t player_video_track_idx(esp_player_stream_t *stream);

esp_player_err_t player_get_favor_type(const char *url, esp_player_format_t *format);

esp_player_format_t player_current_format(esp_player_stream_t *stream);

esp_gmf_task_handle_t player_pipeline_task(esp_gmf_pipeline_handle_t pipe);

void player_send_null_queue(QueueHandle_t queue);
void player_drop_single_queue(esp_player_stream_t *stream, QueueHandle_t queue);
void player_drop_all_queues(esp_player_stream_t *stream);
void player_set_task_timeout(esp_gmf_task_handle_t task, uint32_t timeout_ms);
void player_reset_audio_db(esp_player_stream_t *stream);
void player_reset_video_db(esp_player_stream_t *stream);
void player_reset_all_db(esp_player_stream_t *stream);
void player_clear_all_queues(esp_player_stream_t *stream);
void player_send_event(esp_player_stream_t *stream, esp_player_event_msg_t *event_msg);
void player_destroy_input_io(esp_player_stream_t *stream);
void player_destroy_audio_path(esp_player_stream_t *stream);
void player_destroy_video_path(esp_player_stream_t *stream);
void player_destroy_extractor_path(esp_player_stream_t *stream);
esp_gmf_err_t player_stop_decoder(esp_player_stream_t *stream, QueueHandle_t queue,
                                  uint8_t task_status, uint8_t bit,
                                  esp_gmf_pipeline_handle_t pipe_hd,
                                  esp_gmf_db_handle_t db);
esp_gmf_err_t player_stop_render(esp_player_stream_t *stream, uint8_t task_status, uint8_t bit,
                                 esp_gmf_db_handle_t db,
                                 esp_gmf_pipeline_handle_t pipe_hd);
esp_gmf_err_t player_stop_extractor(esp_player_stream_t *stream);
void player_pause_extractor_task(esp_player_stream_t *stream,
                                 esp_gmf_event_state_t *state,
                                 esp_gmf_err_t *ret);
void player_pause_decoder_task(esp_player_stream_t *stream,
                               esp_gmf_task_handle_t decoder_task,
                               esp_gmf_db_handle_t db, QueueHandle_t queue,
                               esp_gmf_event_state_t *state,
                               uint8_t bit, esp_gmf_err_t *ret);
void player_raise_error_source(esp_player_stream_t *stream,
                               esp_player_error_source_t error_source,
                               const char *reason);
esp_player_err_t player_run_pipeline_with_timeout(esp_player_stream_t *stream,
                                                  esp_gmf_task_handle_t task,
                                                  uint32_t timeout_ms,
                                                  esp_gmf_pipeline_handle_t pipeline,
                                                  esp_player_error_source_t error_source);

esp_player_err_t player_create_pipeline_if_expected(esp_player_stream_t *stream,
                                                    player_pipeline_factory_t func,
                                                    uint8_t bit, bool is_audio);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
