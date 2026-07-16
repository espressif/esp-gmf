/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include <stdlib.h>

#include "player_state.h"
#include "player_state_path_ops.h"
#include "player_pipeline.h"
#include "player_url.h"
#include "player_defaults_cfg.h"

#define SET_EVENTS_BY_STATE_IN_ERROR(stream, old_state)  do {                                       \
    if ((stream)->is_seeking) {                                                                     \
        /* Unblock a pending esp_player_seek() caller if the seek was aborted by this error. */     \
        player_set_events((stream), _CTRL_PLAYER_SEEKING);                                          \
    }                                                                                               \
    switch (old_state) {                                                                            \
        case PLAYER_STATE_PREPARING:                                                                \
            player_set_events(stream, _CTRL_PLAYER_RUN | _CTRL_RUN_TO_END | _CTRL_PLAYER_RESUMED);  \
            break;                                                                                  \
        case PLAYER_STATE_PAUSED:                                                                   \
            player_set_events(stream, _CTRL_PLAYER_PAUSED);                                         \
            break;                                                                                  \
        case PLAYER_STATE_STOPPED:                                                                  \
            player_set_events(stream, _CTRL_PLAYER_STOPPED);                                        \
            break;                                                                                  \
        default:                                                                                    \
            break;                                                                                  \
    }                                                                                               \
} while (0)

static const char *TAG = "ESP_PLAYER_STATE";

static const player_st_path_ops_t *s_aud_ops;
static const player_st_path_ops_t *s_vid_ops;

static const char *const k_state_names[] = {
    [PLAYER_STATE_IDLE]      = "IDLE",
    [PLAYER_STATE_PREPARING] = "PREPARING",
    [PLAYER_STATE_PLAYING]   = "PLAYING",
    [PLAYER_STATE_PAUSED]    = "PAUSED",
    [PLAYER_STATE_STOPPED]   = "STOPPED",
    [PLAYER_STATE_FINISHED]  = "FINISHED",
    [PLAYER_STATE_ERROR]     = "ERROR",
};

static const char *const k_cmd_names[] = {
    [ESP_PLAYER_CMD_PREPARE]           = "PREPARE",
    [ESP_PLAYER_CMD_PLAYING]           = "PLAYING",
    [ESP_PLAYER_CMD_PAUSE]             = "PAUSE",
    [ESP_PLAYER_CMD_RESUME]            = "RESUME",
    [ESP_PLAYER_CMD_SEEK]              = "SEEK",
    [ESP_PLAYER_CMD_STOP]              = "STOP",
    [ESP_PLAYER_CMD_QUIT]              = "QUIT",
    [ESP_PLAYER_CMD_ERROR]             = "ERROR",
    [ESP_PLAYER_CMD_FINISHED]          = "FINISHED",
    [ESP_PLAYER_CMD_REPORT_AUDIO_INFO] = "REPORT_AUDIO_INFO",
    [ESP_PLAYER_CMD_REPORT_VIDEO_INFO] = "REPORT_VIDEO_INFO",
    [ESP_PLAYER_CMD_REPORT_TRACK_INFO] = "REPORT_TRACK_INFO",
};

typedef struct {
    esp_player_state_t     from;
    esp_player_cmd_type_t  cmd;
    esp_player_state_t     to;
} state_edge_t;

static const state_edge_t k_edges[] = {
    {PLAYER_STATE_IDLE, ESP_PLAYER_CMD_PREPARE, PLAYER_STATE_PREPARING},
    {PLAYER_STATE_IDLE, ESP_PLAYER_CMD_ERROR, PLAYER_STATE_ERROR},
    {PLAYER_STATE_PREPARING, ESP_PLAYER_CMD_STOP, PLAYER_STATE_STOPPED},
    {PLAYER_STATE_PREPARING, ESP_PLAYER_CMD_ERROR, PLAYER_STATE_ERROR},
    {PLAYER_STATE_PLAYING, ESP_PLAYER_CMD_PAUSE, PLAYER_STATE_PAUSED},
    {PLAYER_STATE_PLAYING, ESP_PLAYER_CMD_STOP, PLAYER_STATE_STOPPED},
    {PLAYER_STATE_PLAYING, ESP_PLAYER_CMD_ERROR, PLAYER_STATE_ERROR},
    {PLAYER_STATE_PAUSED, ESP_PLAYER_CMD_RESUME, PLAYER_STATE_PLAYING},
    {PLAYER_STATE_PAUSED, ESP_PLAYER_CMD_STOP, PLAYER_STATE_STOPPED},
    {PLAYER_STATE_PAUSED, ESP_PLAYER_CMD_ERROR, PLAYER_STATE_ERROR},
    {PLAYER_STATE_STOPPED, ESP_PLAYER_CMD_PREPARE, PLAYER_STATE_PREPARING},
    {PLAYER_STATE_STOPPED, ESP_PLAYER_CMD_ERROR, PLAYER_STATE_ERROR},
    {PLAYER_STATE_FINISHED, ESP_PLAYER_CMD_PREPARE, PLAYER_STATE_PREPARING},
    {PLAYER_STATE_ERROR, ESP_PLAYER_CMD_STOP, PLAYER_STATE_STOPPED},
    {PLAYER_STATE_ERROR, ESP_PLAYER_CMD_FINISHED, PLAYER_STATE_FINISHED},
    {PLAYER_STATE_ERROR, ESP_PLAYER_CMD_ERROR, PLAYER_STATE_ERROR},
};

static esp_player_err_t player_transition_to_state(esp_player_stream_t *stream, esp_player_state_t new_state);

static void enter_preparing(esp_player_stream_t *stream, esp_player_state_t old_state);
static void enter_playing(esp_player_stream_t *stream, esp_player_state_t old_state);
static void enter_paused(esp_player_stream_t *stream, esp_player_state_t old_state);
static void enter_stopped(esp_player_stream_t *stream, esp_player_state_t old_state);
static void enter_finished(esp_player_stream_t *stream, esp_player_state_t old_state);
static void enter_error(esp_player_stream_t *stream, esp_player_state_t old_state);

static void start_decoder_by_mode(esp_player_stream_t *stream, uint8_t av_mask);
static void start_playback(esp_player_stream_t *stream);
static void paused_playback(esp_player_stream_t *stream);
static void finished_playback(esp_player_stream_t *stream);
static void pause_playback(esp_player_stream_t *stream);
static void stop_playback(esp_player_stream_t *stream);
static void prepare_pipelines_for_seek(esp_player_stream_t *stream);
static void seek_playback(esp_player_stream_t *stream, esp_player_state_t old_state);
static void handle_playback_finished(esp_player_stream_t *stream);
static void handle_error_state(esp_player_stream_t *stream, esp_player_state_t old_state);

const char *get_state_name(esp_player_state_t state);
const char *get_cmd_name(esp_player_cmd_type_t cmd_type);

static bool handle_cmd_quit(esp_player_stream_t *stream)
{
    if (stream->is_seeking) {
        // A seek is in flight; QUIT historically ignored it — keep the same behaviour.
        return true;
    }
    ESP_LOGI(TAG, "Quitting player from %s state", get_state_name(stream->main_state));
    if (stream->main_state == PLAYER_STATE_PREPARING
        || stream->main_state == PLAYER_STATE_PLAYING
        || stream->main_state == PLAYER_STATE_PAUSED) {
        stop_playback(stream);
    }
    return true;
}

static bool handle_cmd_stop(esp_player_stream_t *stream)
{
    switch (stream->main_state) {
        case PLAYER_STATE_IDLE:
        case PLAYER_STATE_FINISHED:
            player_set_events(stream, _CTRL_RUN_TO_END);
            return true;
        case PLAYER_STATE_STOPPED:
            player_set_events(stream, _CTRL_PLAYER_STOPPED);
            return true;
        default:
            return false;
    }
}

static bool handle_cmd_pause(esp_player_stream_t *stream)
{
    if (stream->main_state != PLAYER_STATE_FINISHED) {
        return false;
    }
    player_set_events(stream, _CTRL_RUN_TO_END);
    return true;
}

static bool handle_cmd_seek(esp_player_stream_t *stream, const esp_player_cmd_msg_t *cmd)
{
    esp_player_state_t current_state = stream->main_state;
    if (cmd->data == NULL || cmd->data_len != sizeof(uint64_t)) {
        player_raise_error_source(stream, ESP_PLAYER_ERROR_SOURCE_EXTRACTOR, "invalid seek payload");
        player_transition_to_state(stream, PLAYER_STATE_ERROR);
        return true;
    }
    uint64_t seek_target = *(uint64_t *)cmd->data;
    free(cmd->data);

    player_sync_set_seek_target(stream->sync_handle, seek_target);
    stream->is_seeking = true;
    /* Unblock the esp_player_seek() caller (it is waiting on _CTRL_PLAYER_SEEKING). */
    player_set_events(stream, _CTRL_PLAYER_SEEKING);

    player_sync_set_seek_in_progress(stream->sync_handle, true);
    prepare_pipelines_for_seek(stream);
    if (current_state == PLAYER_STATE_PLAYING || current_state == PLAYER_STATE_PAUSED) {
        seek_playback(stream, current_state);
    }

    player_sync_set_seek_in_progress(stream->sync_handle, false);
    if (stream->error_source == ESP_PLAYER_ERROR_SOURCE_NONE) {
        player_sync_set_render_pts(stream->sync_handle, seek_target);
        esp_player_event_msg_t event_msg = {.event_type = ESP_PLAYER_EVENT_SEEK_DONE, .data = NULL, .data_len = 0};
        player_send_event(stream, &event_msg);
    }
    stream->is_seeking = false;
    return true;
}

static bool handle_cmd_finished(esp_player_stream_t *stream)
{
    if (stream->main_state != PLAYER_STATE_PREPARING && stream->main_state != PLAYER_STATE_PLAYING) {
        return false;
    }
    if (stream->task_status != 0) {
        ESP_LOGI(TAG, "Remaining active streams, task_status: 0x%" PRIx8, stream->task_status);
        return true;
    }
    ESP_LOGI(TAG, "All streams ended, sending finish event");
    player_set_events(stream, _CTRL_RUN_TO_END);
    player_transition_to_state(stream, PLAYER_STATE_FINISHED);
    return true;
}

static bool handle_cmd_playing(esp_player_stream_t *stream)
{
    if (stream->main_state != PLAYER_STATE_PREPARING) {
        return true;  // ignored in other states (matches historical default break)
    }
    if (stream->runned_status == stream->expected_tasks) {
        player_transition_to_state(stream, PLAYER_STATE_PLAYING);
    }
    return true;
}

static bool handle_cmd_report_info(esp_player_stream_t *stream, const esp_player_cmd_msg_t *cmd)
{
    if (stream->main_state != PLAYER_STATE_PREPARING) {
        return true;  // ignored in other states
    }
    esp_player_event_msg_t event_msg = {.event_type = 0, .data = NULL, .data_len = 0};
    switch (cmd->cmd_type) {
        case ESP_PLAYER_CMD_REPORT_AUDIO_INFO:
            ESP_LOGD(TAG, "PREPARING: Audio stream reported info, starting render directly");
            event_msg.event_type = ESP_PLAYER_EVENT_AUDIO_INFO_PARSED;
            player_send_event(stream, &event_msg);
            if (player_create_render_pipeline(stream, true) != ESP_PLAYER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to start audio renderer");
                player_transition_to_state(stream, PLAYER_STATE_ERROR);
            }
            return true;
        case ESP_PLAYER_CMD_REPORT_VIDEO_INFO:
            ESP_LOGD(TAG, "PREPARING: Video stream reported info, starting render directly");
            event_msg.event_type = ESP_PLAYER_EVENT_VIDEO_INFO_PARSED;
            player_send_event(stream, &event_msg);
            if (player_create_render_pipeline(stream, false) != ESP_PLAYER_ERR_OK) {
                ESP_LOGE(TAG, "Failed to start video renderer");
                player_transition_to_state(stream, PLAYER_STATE_ERROR);
            }
            return true;
        case ESP_PLAYER_CMD_REPORT_TRACK_INFO: {
            ESP_LOGD(TAG, "PLAYING: AV mode reported track info");
            event_msg.event_type = ESP_PLAYER_EVENT_TRACK_INFO_PARSED;
            player_send_event(stream, &event_msg);
            esp_gmf_element_handle_t extractor_el = NULL;
            esp_gmf_pipeline_get_el_by_name(stream->extractor, EXTRACTOR_TAG, &extractor_el);
            int8_t aud_idx = -1;
            int8_t vid_idx = -1;
            if (player_extractor_track_active(extractor_el, ESP_EXTRACTOR_STREAM_TYPE_AUDIO, &aud_idx) == ESP_GMF_ERR_OK && aud_idx >= 0) {
                if (!(stream->expected_tasks & TASK_STATUS_AUDIO_DECODER_RUNNING)) {
                    start_decoder_by_mode(stream, ESP_PLAYER_MASK_AUDIO);
                    if (stream->error_source != ESP_PLAYER_ERROR_SOURCE_NONE) {
                        player_transition_to_state(stream, PLAYER_STATE_ERROR);
                        return true;
                    }
                }
            } else if (stream->audio_side) {
                player_drop_single_queue(stream, stream->audio_side->extractor_queue);
            }
            if (player_extractor_track_active(extractor_el, ESP_EXTRACTOR_STREAM_TYPE_VIDEO, &vid_idx) == ESP_GMF_ERR_OK && vid_idx >= 0) {
                if (!(stream->expected_tasks & TASK_STATUS_VIDEO_DECODER_RUNNING)) {
                    start_decoder_by_mode(stream, ESP_PLAYER_MASK_VIDEO);
                    if (stream->error_source != ESP_PLAYER_ERROR_SOURCE_NONE) {
                        player_transition_to_state(stream, PLAYER_STATE_ERROR);
                        return true;
                    }
                }
                if (aud_idx < 0) {
                    player_sync_set_video_fps(stream->sync_handle, stream->video_side->track_info.video_info.fps);
                    player_sync_enable_video_fps_sync(stream->sync_handle, true);
                } else {
                    player_sync_enable_video_fps_sync(stream->sync_handle, false);
                }
            } else if (stream->video_side) {
                player_drop_single_queue(stream, stream->video_side->extractor_queue);
            }
            return true;
        }
        default:
            return false;
    }
}

static bool try_handle_complex_cmd(esp_player_stream_t *stream, const esp_player_cmd_msg_t *cmd)
{
    switch (cmd->cmd_type) {
        case ESP_PLAYER_CMD_QUIT:
            return handle_cmd_quit(stream);
        case ESP_PLAYER_CMD_PAUSE:
            return handle_cmd_pause(stream);
        case ESP_PLAYER_CMD_STOP:
            return handle_cmd_stop(stream);
        case ESP_PLAYER_CMD_FINISHED:
            return handle_cmd_finished(stream);
        case ESP_PLAYER_CMD_PLAYING:
            return handle_cmd_playing(stream);
        case ESP_PLAYER_CMD_SEEK:
            return handle_cmd_seek(stream, cmd);
        case ESP_PLAYER_CMD_REPORT_AUDIO_INFO:
        case ESP_PLAYER_CMD_REPORT_VIDEO_INFO:
        case ESP_PLAYER_CMD_REPORT_TRACK_INFO:
            return handle_cmd_report_info(stream, cmd);
        default:
            return false;
    }
}

static void enter_preparing(esp_player_stream_t *stream, esp_player_state_t old_state)
{
    (void)old_state;
    ESP_LOGI(TAG, "Entering PREPARING state");
    stream->_is_stop = false;
    if (stream->input_state == ESP_PLAYER_INPUT_OPEN_FAILED) {
        stream->input_state = ESP_PLAYER_INPUT_CLOSED;
    }
    start_playback(stream);
}

static void enter_playing(esp_player_stream_t *stream, esp_player_state_t old_state)
{
    ESP_LOGI(TAG, "Entering PLAYING state, old_state: %s", get_state_name(old_state));
    esp_player_event_msg_t event_msg = {.event_type = ESP_PLAYER_EVENT_PLAYED, .data = NULL, .data_len = 0};
    switch (old_state) {
        case PLAYER_STATE_PAUSED:
            paused_playback(stream);
            player_send_event(stream, &event_msg);
            player_set_events(stream, _CTRL_PLAYER_RESUMED);
            break;
        case PLAYER_STATE_STOPPED:
            player_send_event(stream, &event_msg);
            break;
        case PLAYER_STATE_FINISHED:
            finished_playback(stream);
            player_send_event(stream, &event_msg);
            break;
        case PLAYER_STATE_PREPARING:
            if (stream->runned_status == stream->expected_tasks) {
                player_send_event(stream, &event_msg);
            }
            break;
        case PLAYER_STATE_ERROR:
            player_send_event(stream, &event_msg);
            break;
        default:
            break;
    }
}

static void enter_paused(esp_player_stream_t *stream, esp_player_state_t old_state)
{
    (void)old_state;
    ESP_LOGI(TAG, "Entering PAUSED state");
    pause_playback(stream);
    esp_player_event_msg_t event_msg = {.event_type = ESP_PLAYER_EVENT_PAUSED, .data = NULL, .data_len = 0};
    player_send_event(stream, &event_msg);
    player_set_events(stream, _CTRL_PLAYER_PAUSED);
}

static void enter_stopped(esp_player_stream_t *stream, esp_player_state_t old_state)
{
    ESP_LOGI(TAG, "Entering STOPPED state");
    if (old_state == PLAYER_STATE_PAUSED) {
        paused_playback(stream);
    }
    stop_playback(stream);
    esp_player_event_msg_t event_msg = {.event_type = ESP_PLAYER_EVENT_STOPPED, .data = NULL, .data_len = 0};
    player_send_event(stream, &event_msg);
    player_set_events(stream, _CTRL_PLAYER_STOPPED | _CTRL_RUN_TO_END);
    if (old_state == PLAYER_STATE_PREPARING) {
        player_set_events(stream, _CTRL_PLAYER_RUN);
    }
}

static void enter_finished(esp_player_stream_t *stream, esp_player_state_t old_state)
{
    (void)old_state;
    ESP_LOGI(TAG, "Entering FINISHED state");
    handle_playback_finished(stream);
    esp_player_event_msg_t event_msg = {.event_type = ESP_PLAYER_EVENT_FINISHED, .data = NULL, .data_len = 0};
    player_send_event(stream, &event_msg);
}

static void enter_error(esp_player_stream_t *stream, esp_player_state_t old_state)
{
    ESP_LOGE(TAG, "Entering ERROR state");
    handle_error_state(stream, old_state);
    esp_player_event_msg_t event_msg = {
        .event_type = ESP_PLAYER_EVENT_ERROR,
        .data = &stream->error_source,
        .data_len = sizeof(stream->error_source),
    };
    player_send_event(stream, &event_msg);
}

typedef void (*state_enter_fn_t)(esp_player_stream_t *stream, esp_player_state_t old_state);

static const state_enter_fn_t k_state_enter[] = {
    [PLAYER_STATE_IDLE]      = NULL,
    [PLAYER_STATE_PREPARING] = enter_preparing,
    [PLAYER_STATE_PLAYING]   = enter_playing,
    [PLAYER_STATE_PAUSED]    = enter_paused,
    [PLAYER_STATE_STOPPED]   = enter_stopped,
    [PLAYER_STATE_FINISHED]  = enter_finished,
    [PLAYER_STATE_ERROR]     = enter_error,
};

static void prepare_pipelines_for_seek(esp_player_stream_t *stream)
{
    esp_gmf_err_t ret = ESP_GMF_ERR_FAIL;
    esp_gmf_event_state_t state = ESP_GMF_EVENT_STATE_INITIALIZED;
    esp_gmf_db_handle_t aud_db = player_audio_db(stream);
    esp_gmf_task_handle_t aud_dec_tsk = NULL;
    QueueHandle_t aud_q = NULL;
    s_aud_ops->seek_handles(stream, &aud_dec_tsk, &aud_q);

    esp_gmf_db_handle_t vid_db = player_video_db(stream);
    esp_gmf_task_handle_t vid_dec_tsk = NULL;
    QueueHandle_t vid_q = NULL;
    s_vid_ops->seek_handles(stream, &vid_dec_tsk, &vid_q);

    esp_gmf_task_handle_t ext_tsk = player_pipeline_task(stream->extractor);

    if (stream->task_status & TASK_STATUS_EXTRACTOR_RUNNING) {
        s_aud_ops->seek_pause_decoder(stream, aud_db, aud_dec_tsk, aud_q, &state, &ret);
        s_vid_ops->seek_pause_decoder(stream, vid_db, vid_dec_tsk, vid_q, &state, &ret);
        if (ext_tsk) {
            esp_gmf_task_get_state(ext_tsk, &state);
        }
        player_pause_extractor_task(stream, &state, &ret);
    } else {
        s_aud_ops->seek_stop_decoder_if_running(stream, aud_q, aud_db, &ret);
        s_vid_ops->seek_stop_decoder_if_running(stream, vid_q, vid_db, &ret);
    }
}

static esp_player_err_t player_transition_to_state(esp_player_stream_t *stream, esp_player_state_t new_state)
{
    esp_player_state_t old_state = stream->main_state;
    ESP_LOGI(TAG, "State transition: %s -> %s", get_state_name(old_state), get_state_name(new_state));

    stream->main_state = new_state;
    if (new_state < (sizeof(k_state_enter) / sizeof(k_state_enter[0])) && k_state_enter[new_state]) {
        k_state_enter[new_state](stream, old_state);
    }
    return ESP_PLAYER_ERR_OK;
}

static void _player_init_db(esp_player_stream_t *stream)
{
    stream->error_source = ESP_PLAYER_ERROR_SOURCE_NONE;
    player_drop_all_queues(stream);
    player_reset_all_db(stream);
}

static void _player_init_params(esp_player_stream_t *stream)
{
    stream->expected_tasks = 0;
    stream->task_status = 0;
    stream->runned_status = 0;
    _player_init_db(stream);
    s_aud_ops->init_params_format(stream);
    s_vid_ops->init_params_format(stream);
}

static void start_playback(esp_player_stream_t *stream)
{
    player_clear_all_queues(stream);
    if ((stream->av_mask & ESP_PLAYER_MASK_AV) == 0) {
        ESP_LOGE(TAG, "Invalid av_mask: 0x%x", stream->av_mask);
        player_transition_to_state(stream, PLAYER_STATE_ERROR);
        return;
    }
    _player_init_params(stream);
    bool enable_network_buffering = ESP_PLAYER_DEFAULT_NETWORK_BUFFERING && _player_is_network_source_uri(stream);
    if (enable_network_buffering) {
        if (stream->buffer_ctrl == NULL) {
            stream->buffer_ctrl = (player_buffer_ctrl_t *)calloc(1, sizeof(player_buffer_ctrl_t));
            if (stream->buffer_ctrl == NULL) {
                ESP_LOGW(TAG, "No mem for buffer_ctrl, buffering gate disabled for this run");
            }
        }
    } else if (stream->buffer_ctrl) {
        free(stream->buffer_ctrl);
        stream->buffer_ctrl = NULL;
    }
    if (stream->buffer_ctrl) {
        stream->buffer_ctrl->gate_state = ESP_PLAYER_BUFFER_GATE_NONE;
        stream->buffer_ctrl->low_since = 0;
        stream->buffer_ctrl->avg_audio_frame_ms = 0;
        stream->buffer_ctrl->avg_video_frame_ms = 0;
    }
    if (enable_network_buffering && stream->buffer_ctrl) {
        stream->buffer_ctrl->gate_state = ESP_PLAYER_BUFFER_GATE_PRE_BUFFERING;
        esp_player_event_msg_t event_msg = {
            .event_type = ESP_PLAYER_EVENT_BUFFERING,
            .data = NULL,
            .data_len = 0,
        };
        player_send_event(stream, &event_msg);
    }
    // Sync handle lives for the entire player lifetime; reset runtime pts/flags before each run.
    player_sync_reset(stream->sync_handle);
    uint64_t pending_seek = player_sync_get_seek_target(stream->sync_handle);
    if (pending_seek > 0) {
        player_sync_set_render_pts(stream->sync_handle, pending_seek);
    }
    if (stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_EXTRACTOR) {
        start_decoder_by_mode(stream, ESP_PLAYER_MASK_AV);
    } else {
        start_decoder_by_mode(stream, (uint8_t)stream->av_mask);
    }
    if (stream->error_source != ESP_PLAYER_ERROR_SOURCE_NONE && stream->expected_tasks == 0) {
        ESP_LOGE(TAG, "Pipeline bring-up failed (error_source=%d), transitioning to ERROR",
                 stream->error_source);
        player_transition_to_state(stream, PLAYER_STATE_ERROR);
    }
}

static void paused_playback(esp_player_stream_t *stream)
{
    ESP_LOGI(TAG, "Paused playback");
    if (stream->sync_handle) {
        player_sync_resume(stream->sync_handle);
    }
}

static void finished_playback(esp_player_stream_t *stream)
{
    if (stream->expected_tasks & TASK_STATUS_EXTRACTOR_RUNNING) {
        esp_gmf_pipeline_seek(stream->extractor, 0);
        return;
    }
    if (s_aud_ops->finished_try_seek(stream)) {
        return;
    }
    (void)s_vid_ops->finished_try_seek(stream);
}

static void pause_playback(esp_player_stream_t *stream)
{
    ESP_LOGI(TAG, "Pause playback");
    if (stream->sync_handle) {
        player_sync_pause(stream->sync_handle);
    }
}

static void stop_playback(esp_player_stream_t *stream)
{
    ESP_LOGD(TAG, "Stopping playback. Task status: %" PRIu8 ", runned_status: %" PRIu8 ", expected_tasks: %" PRIu8,
             stream->task_status, stream->runned_status, stream->expected_tasks);
    xSemaphoreTake(stream->lock_resource, portMAX_DELAY);
    stream->_is_stop = true;
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_db_handle_t aud_db = player_audio_db(stream);
    esp_gmf_db_handle_t vid_db = player_video_db(stream);
    if (stream->audio_side) {
        player_set_task_timeout(player_pipeline_task(stream->audio_side->decoder), 100);
        player_set_task_timeout(player_pipeline_task(stream->audio_side->render), 100);
    }
    if (stream->video_side) {
        player_set_task_timeout(player_pipeline_task(stream->video_side->decoder), 100);
        player_set_task_timeout(player_pipeline_task(stream->video_side->render), 100);
    }
    player_set_task_timeout(player_pipeline_task(stream->extractor), 100);
    if (stream->sync_handle) {
        player_sync_resume(stream->sync_handle);
    }
    uint8_t active = stream->task_status | stream->expected_tasks;
    do {
        ret = ESP_GMF_ERR_OK;
        if (active & TASK_STATUS_EXTRACTOR_RUNNING) {
            ret |= player_stop_extractor(stream);
        }
        if ((active & TASK_STATUS_AUDIO_DECODER_RUNNING) && stream->audio_side && stream->audio_side->decoder) {
            ret |= player_stop_decoder(stream, stream->audio_side->extractor_queue, active, TASK_STATUS_AUDIO_DECODER_RUNNING, stream->audio_side->decoder, aud_db);
        }
        if ((active & TASK_STATUS_VIDEO_DECODER_RUNNING) && stream->video_side && stream->video_side->decoder) {
            ret |= player_stop_decoder(stream, stream->video_side->extractor_queue, active, TASK_STATUS_VIDEO_DECODER_RUNNING, stream->video_side->decoder, vid_db);
        }
        if ((active & TASK_STATUS_AUDIO_RENDER_RUNNING) && stream->audio_side && stream->audio_side->render) {
            ret |= player_stop_render(stream, active, TASK_STATUS_AUDIO_RENDER_RUNNING, aud_db, stream->audio_side->render);
        }
        if ((active & TASK_STATUS_VIDEO_RENDER_RUNNING) && stream->video_side && stream->video_side->render) {
            ret |= player_stop_render(stream, active, TASK_STATUS_VIDEO_RENDER_RUNNING, vid_db, stream->video_side->render);
        }
    } while (0);

    // Keep sync_handle alive across run/stop cycles; only destroyed in esp_player_deinit.
    stream->runned_status = 0;
    stream->expected_tasks = 0;
    xSemaphoreGive(stream->lock_resource);
}

static void deinit_audio_path(esp_player_stream_t *stream)
{
    player_destroy_audio_path(stream);
    if (stream->audio_side) {
        memset(&stream->audio_side->track_info, 0, sizeof(esp_player_track_info_t));
        stream->audio_side->track_info.track_type = ESP_PLAYER_TRACK_TYPE_AUDIO;
    }
}

static void deinit_video_path(esp_player_stream_t *stream)
{
    player_destroy_video_path(stream);
    if (stream->video_side) {
        memset(&stream->video_side->track_info, 0, sizeof(esp_player_track_info_t));
        stream->video_side->track_info.track_type = ESP_PLAYER_TRACK_TYPE_VIDEO;
    }
}

static void deinit_extractor_path(esp_player_stream_t *stream)
{
    player_destroy_extractor_path(stream);
}

static void handle_playback_finished(esp_player_stream_t *stream)
{
    ESP_LOGI(TAG, "Playback finished");
    player_clear_all_queues(stream);
}

static void handle_error_state(esp_player_stream_t *stream, esp_player_state_t old_state)
{
    ESP_LOGE(TAG, "Handling error state");
    player_sync_set_seek_target(stream->sync_handle, 0);
    if (stream->error_source == ESP_PLAYER_ERROR_SOURCE_EXTRACTOR || stream->dec_frame_mode != ESP_PLAYER_DEC_FRAME_MODE_EXTRACTOR) {
        stop_playback(stream);
        deinit_audio_path(stream);
        deinit_video_path(stream);
        deinit_extractor_path(stream);
        player_clear_events(stream, _CTRL_ALL_EVENTS);
        ESP_LOGI(TAG, "State transition: %s -> %s (error auto-recovery)",
                 get_state_name(stream->main_state), get_state_name(PLAYER_STATE_IDLE));
        stream->main_state = PLAYER_STATE_IDLE;
        SET_EVENTS_BY_STATE_IN_ERROR(stream, old_state);
        return;
    }
    if (stream->error_source == ESP_PLAYER_ERROR_SOURCE_AUDIO_DECODER || stream->error_source == ESP_PLAYER_ERROR_SOURCE_AUDIO_RENDER) {
        s_aud_ops->stop_path(stream);
        deinit_audio_path(stream);
        player_clear_events(stream, _CTRL_PLAYER_SYNC_AUDIO_RESUMED | _CTRL_PLAYER_DECODER_AUDIO_SEEK_DONE);
        // After the audio path is torn down the extractor's audio track is disabled too
        // (via player_extractor_enable_stream(false) in the decoder/render error handler),
        // so player_audio_track_idx() now returns -1 and video_out_release already throttles.
        if (old_state == PLAYER_STATE_ERROR) {
            player_set_events(stream, _CTRL_ALL_EVENTS);
        }
    } else if (stream->error_source == ESP_PLAYER_ERROR_SOURCE_VIDEO_DECODER || stream->error_source == ESP_PLAYER_ERROR_SOURCE_VIDEO_RENDER) {
        s_vid_ops->stop_path(stream);
        deinit_video_path(stream);
        player_clear_events(stream, _CTRL_PLAYER_SYNC_AUDIO_RESUMED | _CTRL_PLAYER_DECODER_VIDEO_SEEK_DONE);
        if (old_state == PLAYER_STATE_ERROR) {
            player_set_events(stream, _CTRL_ALL_EVENTS);
        }
    }
    uint8_t survivor_render_bit = TASK_STATUS_AUDIO_RENDER_RUNNING | TASK_STATUS_VIDEO_RENDER_RUNNING;
    if (stream->expected_tasks & survivor_render_bit) {
        bool survivor_render_ready = (stream->task_status & survivor_render_bit) != 0;
        if (survivor_render_ready && old_state == PLAYER_STATE_PREPARING) {
            ESP_LOGI(TAG, "Partial error in PREPARING, surviving stream auto-play");
            player_transition_to_state(stream, PLAYER_STATE_PLAYING);
        } else {
            ESP_LOGI(TAG, "Partial error, keep surviving stream in %s (render_ready=%d)",
                     get_state_name(old_state), survivor_render_ready);
            stream->main_state = old_state;
        }
        return;
    } else {
        stop_playback(stream);
        deinit_audio_path(stream);
        deinit_video_path(stream);
        deinit_extractor_path(stream);
        player_clear_events(stream, _CTRL_ALL_EVENTS);
        ESP_LOGI(TAG, "State transition: %s -> %s (error auto-recovery)",
                 get_state_name(stream->main_state), get_state_name(PLAYER_STATE_IDLE));
        stream->main_state = PLAYER_STATE_IDLE;
        SET_EVENTS_BY_STATE_IN_ERROR(stream, old_state);
    }
}

static void seek_playback(esp_player_stream_t *stream, esp_player_state_t old_state)
{
    ESP_LOGI(TAG, "Seeking playback");
    player_clear_events(stream, _CTRL_PLAYER_DECODER_AUDIO_SEEK_DONE | _CTRL_PLAYER_DECODER_VIDEO_SEEK_DONE);

    esp_gmf_element_handle_t ext_el_seek = player_extractor_el(stream);
    if (ext_el_seek && (stream->expected_tasks & TASK_STATUS_EXTRACTOR_RUNNING)) {
        player_extractor_seek(ext_el_seek, player_sync_get_seek_target(stream->sync_handle));
    }
    ESP_LOGD(TAG, "%s:%d old_state=%s expected_tasks=0x%" PRIx8 " task_status=0x%" PRIx8,
             __func__, __LINE__, get_state_name(old_state), stream->expected_tasks, stream->task_status);
    if (stream->sync_handle) {
        player_sync_reset(stream->sync_handle);
        if (old_state == PLAYER_STATE_PAUSED) {
            player_sync_pause(stream->sync_handle);
        }
    }
    player_clear_all_queues(stream);
    if (stream->audio_side && stream->audio_side->decoder) {
        esp_gmf_element_handle_t aud_dec_el = NULL;
        if (esp_gmf_pipeline_get_el_by_name(stream->audio_side->decoder, AUDIO_DECODER_TAG, &aud_dec_el) == ESP_GMF_ERR_OK
            && aud_dec_el != NULL) {
            esp_gmf_element_process_reset(aud_dec_el, NULL);
        }
    }
    if (stream->video_side && stream->video_side->decoder) {
        esp_gmf_element_handle_t vid_dec_el = NULL;
        if (esp_gmf_pipeline_get_el_by_name(stream->video_side->decoder, VIDEO_DECODER_TAG, &vid_dec_el) == ESP_GMF_ERR_OK
            && vid_dec_el != NULL) {
            esp_gmf_element_process_reset(vid_dec_el, NULL);
        }
    }
    esp_gmf_element_handle_t audio_render_el = NULL;
    esp_gmf_element_handle_t video_render_el = NULL;
    s_aud_ops->seek_flush_render(stream, &audio_render_el);
    s_vid_ops->seek_flush_render(stream, &video_render_el);

    if (stream->task_status & TASK_STATUS_EXTRACTOR_RUNNING) {
        esp_gmf_pipeline_resume(stream->extractor);
    } else {
        player_create_extractor_pipeline(stream);
    }
    s_aud_ops->seek_resume_or_create_decoder(stream);
    s_vid_ops->seek_resume_or_create_decoder(stream);

    s_aud_ops->seek_create_render_if_expected(stream);
    s_vid_ops->seek_create_render_if_expected(stream);

    if (stream->sync_handle) {
        player_sync_resume(stream->sync_handle);
    }
    uint32_t seek_done_bits = s_aud_ops->seek_done_bit(stream) | s_vid_ops->seek_done_bit(stream);
    if (s_aud_ops->seek_wait_decoder_done(stream, seek_done_bits)) {
        return;
    }
    if (s_vid_ops->seek_wait_decoder_done(stream, seek_done_bits)) {
        return;
    }
    ESP_LOGD(TAG, "%s-%d-old_state: %s-stream->main_state: %s\n", __func__, __LINE__, get_state_name(old_state), get_state_name(stream->main_state));

    esp_gmf_db_handle_t aud_db_seek = player_audio_db(stream);
    esp_gmf_db_handle_t vid_db_seek = player_video_db(stream);
    if (old_state == PLAYER_STATE_PAUSED) {
        int seek_has_expected_decoder =
            (s_aud_ops->decoder_expected_running(stream) ? 1 : 0) | (s_vid_ops->decoder_expected_running(stream) ? 1 : 0);
        if (seek_has_expected_decoder) {
            if (stream->sync_handle) {
                player_sync_pause(stream->sync_handle);
            }
            s_aud_ops->seek_paused_disable_flush(stream, audio_render_el);
            s_vid_ops->seek_paused_disable_flush(stream, video_render_el);
        } else {
            if (stream->audio_side && stream->audio_side->data_bus) {
                player_data_bus_reset_meta(stream->audio_side->data_bus);
            }
            if (stream->video_side && stream->video_side->data_bus) {
                player_data_bus_reset_meta(stream->video_side->data_bus);
            }
            if (aud_db_seek) {
                esp_gmf_db_abort(aud_db_seek);
                esp_gmf_db_done_write(aud_db_seek);
            }
            if (vid_db_seek) {
                esp_gmf_db_abort(vid_db_seek);
                esp_gmf_db_done_write(vid_db_seek);
            }
        }

    } else if (old_state == PLAYER_STATE_PLAYING) {
        int seek_has_expected_decoder_play =
            (s_aud_ops->decoder_expected_running(stream) ? 1 : 0) | (s_vid_ops->decoder_expected_running(stream) ? 1 : 0);
        if (seek_has_expected_decoder_play) {
            s_aud_ops->seek_playing_disable_flush(stream, audio_render_el);
            s_vid_ops->seek_playing_disable_flush(stream, video_render_el);
        } else {
            if (stream->audio_side && stream->audio_side->data_bus) {
                player_data_bus_reset_meta(stream->audio_side->data_bus);
            }
            if (stream->video_side && stream->video_side->data_bus) {
                player_data_bus_reset_meta(stream->video_side->data_bus);
            }
            if (aud_db_seek) {
                esp_gmf_db_abort(aud_db_seek);
                esp_gmf_db_done_write(aud_db_seek);
            }
            if (vid_db_seek) {
                esp_gmf_db_abort(vid_db_seek);
                esp_gmf_db_done_write(vid_db_seek);
            }
        }
    }
}

static void start_decoder_by_mode(esp_player_stream_t *stream, uint8_t av_mask)
{
    switch (av_mask) {
        case ESP_PLAYER_MASK_AUDIO:
            s_aud_ops->start_decoder_mask(stream);
            break;
        case ESP_PLAYER_MASK_VIDEO:
            s_vid_ops->start_decoder_mask(stream);
            break;
        case ESP_PLAYER_MASK_AV:
            ESP_LOGD(TAG, "Starting extractor");
            if (player_create_extractor_pipeline(stream) == ESP_PLAYER_ERR_OK) {
                stream->expected_tasks |= TASK_STATUS_EXTRACTOR_RUNNING;
                ESP_LOGI(TAG, "Extractor started, waiting for track info event");
            }
            break;
        default:
            ESP_LOGE(TAG, "Invalid sub state for decoder start");
            break;
    }
}

void player_state_init(void)
{
    s_aud_ops = player_st_get_audio_ops();
    s_vid_ops = player_st_get_video_ops();
}

const char *get_state_name(esp_player_state_t state)
{
    if (state < (sizeof(k_state_names) / sizeof(k_state_names[0])) && k_state_names[state]) {
        return k_state_names[state];
    }
    return "UNKNOWN";
}

const char *get_cmd_name(esp_player_cmd_type_t cmd_type)
{
    if (cmd_type < (sizeof(k_cmd_names) / sizeof(k_cmd_names[0])) && k_cmd_names[cmd_type]) {
        return k_cmd_names[cmd_type];
    }
    return "UNKNOWN";
}

esp_player_err_t handle_state_cmd(esp_player_stream_t *stream, esp_player_cmd_msg_t *cmd)
{
    ESP_LOGI(TAG, "Handling cmd: %s in state: %s", get_cmd_name(cmd->cmd_type), get_state_name(stream->main_state));

    /* 1. State- or runtime-dependent commands are routed to dedicated handlers. */
    if (try_handle_complex_cmd(stream, cmd)) {
        return ESP_PLAYER_ERR_OK;
    }

    /* 2. Fall through to the (from, cmd) -> to lookup table. */
    for (size_t i = 0; i < sizeof(k_edges) / sizeof(k_edges[0]); i++) {
        const state_edge_t *e = &k_edges[i];
        if (e->from != stream->main_state || e->cmd != cmd->cmd_type) {
            continue;
        }
        if (e->to != stream->main_state) {
            player_transition_to_state(stream, e->to);
        }
        return ESP_PLAYER_ERR_OK;
    }

    /* 3. Unhandled command: log and drop (matches the historical warning path). */
    ESP_LOGW(TAG, "Unsupported cmd (%s) in %s state",
             get_cmd_name(cmd->cmd_type), get_state_name(stream->main_state));
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_send_cmd(esp_player_stream_t *stream, esp_player_cmd_msg_t *cmd)
{
    if (stream == NULL || cmd == NULL) {
        ESP_LOGE(TAG, "Invalid argument: stream=%p, cmd=%p", stream, cmd);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (stream->cmd_queue == NULL) {
        ESP_LOGE(TAG, "Command queue is NULL, player may be deinitialized");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (xQueueSend(stream->cmd_queue, cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send command to queue");
        return ESP_PLAYER_ERR_TIMEOUT;
    }
    return ESP_PLAYER_ERR_OK;
}
