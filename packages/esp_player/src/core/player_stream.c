/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stddef.h>

#include "player_stream.h"

static void player_reset_data_bus_meta_for_db(esp_player_stream_t *stream, esp_gmf_db_handle_t db)
{
    if (stream == NULL || db == NULL) {
        return;
    }
    if (stream->audio_side && stream->audio_side->data_bus &&
        player_data_bus_inner(stream->audio_side->data_bus) == db) {
        player_data_bus_reset_meta(stream->audio_side->data_bus);
    }
    if (stream->video_side && stream->video_side->data_bus &&
        player_data_bus_inner(stream->video_side->data_bus) == db) {
        player_data_bus_reset_meta(stream->video_side->data_bus);
    }
}

esp_gmf_db_handle_t player_get_db(esp_gmf_pipeline_handle_t pipe, bool use_out_port)
{
    if (pipe == NULL) {
        return NULL;
    }
    esp_gmf_element_handle_t el = ESP_GMF_PIPELINE_GET_FIRST_ELEMENT((esp_gmf_pipeline_t *)pipe);
    if (el == NULL) {
        return NULL;
    }
    esp_gmf_port_t *port = use_out_port ? ESP_GMF_ELEMENT_GET_OUT_PORT(el)
                                        : ESP_GMF_ELEMENT_GET_IN_PORT(el);
    if (!port || !port->ctx) {
        return NULL;
    }
    if (player_data_bus_is_handle(port->ctx)) {
        return player_data_bus_inner((player_data_bus_t *)port->ctx);
    }
    return (esp_gmf_db_handle_t)port->ctx;
}

esp_gmf_element_handle_t player_extractor_el(esp_player_stream_t *stream)
{
    esp_gmf_element_handle_t el = NULL;
    if (stream == NULL || stream->extractor == NULL) {
        return NULL;
    }
    if (ESP_GMF_PIPELINE_GET_FIRST_ELEMENT((esp_gmf_pipeline_t *)stream->extractor) == NULL) {
        return NULL;
    }
    if (esp_gmf_pipeline_get_el_by_name(stream->extractor, EXTRACTOR_TAG, &el) != ESP_GMF_ERR_OK) {
        return NULL;
    }
    return el;
}

esp_player_format_t player_current_format(esp_player_stream_t *stream)
{
    esp_player_format_t fmt = ESP_PLAYER_FORMAT_NONE;
    if (stream->input_handle == NULL) {
        return fmt;
    }
    char *uri = NULL;
    if (esp_gmf_io_get_uri(stream->input_handle, &uri) != ESP_GMF_ERR_OK || uri == NULL) {
        return fmt;
    }
    player_get_favor_type(uri, &fmt);
    return fmt;
}

esp_gmf_task_handle_t player_pipeline_task(esp_gmf_pipeline_handle_t pipe)
{
    return pipe ? ((esp_gmf_pipeline_t *)pipe)->thread : NULL;
}

void player_send_null_queue(QueueHandle_t queue)
{
    if (queue == NULL) {
        return;
    }
    esp_gmf_payload_t load = {
        .buf = NULL,
        .valid_size = 0,
        .is_done = true,
    };
    xQueueSend(queue, &load, pdMS_TO_TICKS(1000));
}

void player_drop_single_queue(esp_player_stream_t *stream, QueueHandle_t queue)
{
    while (queue && uxQueueMessagesWaiting(queue) > 0) {
        esp_gmf_payload_t tmp_load = {0};
        if (xQueueReceive(queue, &tmp_load, 0) == pdTRUE) {
            player_release_payload(stream, &tmp_load);
        }
    }
}

void player_drop_all_queues(esp_player_stream_t *stream)
{
    if (stream->audio_side) {
        player_drop_single_queue(stream, stream->audio_side->extractor_queue);
    }
    if (stream->video_side) {
        player_drop_single_queue(stream, stream->video_side->extractor_queue);
    }
}

void player_set_task_timeout(esp_gmf_task_handle_t task, uint32_t timeout_ms)
{
    if (task) {
        esp_gmf_task_set_timeout(task, timeout_ms);
    }
}

void player_reset_audio_db(esp_player_stream_t *stream)
{
    if (stream == NULL || stream->audio_side == NULL) {
        return;
    }
    if (stream->audio_side->data_bus) {
        player_data_bus_reset(stream->audio_side->data_bus);
        return;
    }
    esp_gmf_db_handle_t db = player_audio_db(stream);
    if (db) {
        esp_gmf_db_reset(db);
    }
}

void player_reset_video_db(esp_player_stream_t *stream)
{
    if (stream == NULL || stream->video_side == NULL) {
        return;
    }
    if (stream->video_side->data_bus) {
        player_data_bus_reset(stream->video_side->data_bus);
        return;
    }
    esp_gmf_db_handle_t db = player_video_db(stream);
    if (db) {
        esp_gmf_db_reset(db);
    }
}

void player_reset_all_db(esp_player_stream_t *stream)
{
    player_reset_audio_db(stream);
    player_reset_video_db(stream);
}

void player_clear_all_queues(esp_player_stream_t *stream)
{
    player_drop_all_queues(stream);
    player_reset_all_db(stream);
}

void player_send_event(esp_player_stream_t *stream, esp_player_event_msg_t *event_msg)
{
    if (stream->event_cb) {
        stream->event_cb(event_msg, stream->event_ctx);
    }
    if (stream->event_queue) {
        if (xQueueSend(stream->event_queue, event_msg, pdMS_TO_TICKS(100)) != pdTRUE) {
            ESP_LOGW(ESP_PLAYER_TAG, "Event queue full, drop event %d", event_msg ? event_msg->event_type : -1);
        }
    }
}

void player_destroy_input_io(esp_player_stream_t *stream)
{
    if (stream->input_handle) {
        esp_gmf_io_close(stream->input_handle);
        esp_gmf_io_deinit(stream->input_handle);
        esp_gmf_obj_delete(stream->input_handle);
        stream->input_handle = NULL;
    }
    stream->input_state = ESP_PLAYER_INPUT_CLOSED;
}

void player_destroy_audio_path(esp_player_stream_t *stream)
{
    if (stream->audio_side == NULL) {
        return;
    }
    esp_gmf_db_handle_t db = player_audio_db(stream);
    esp_gmf_task_handle_t dec_tsk = player_pipeline_task(stream->audio_side->decoder);
    esp_gmf_task_handle_t ren_tsk = player_pipeline_task(stream->audio_side->render);
    if (stream->audio_side->decoder) {
        esp_gmf_pipeline_destroy(stream->audio_side->decoder);
        stream->audio_side->decoder = NULL;
    }
    if (stream->audio_side->render) {
        esp_gmf_pipeline_destroy(stream->audio_side->render);
        stream->audio_side->render = NULL;
    }
    if (dec_tsk) {
        esp_gmf_task_deinit(dec_tsk);
    }
    if (ren_tsk) {
        esp_gmf_task_deinit(ren_tsk);
    }
    if (db) {
        esp_gmf_db_deinit(db);
    }
    if (stream->audio_side && stream->audio_side->data_bus) {
        player_data_bus_destroy(stream->audio_side->data_bus);
        stream->audio_side->data_bus = NULL;
    }
}

void player_destroy_video_path(esp_player_stream_t *stream)
{
    if (stream->video_side == NULL) {
        return;
    }
    esp_gmf_db_handle_t db = player_video_db(stream);
    esp_gmf_task_handle_t dec_tsk = player_pipeline_task(stream->video_side->decoder);
    esp_gmf_task_handle_t ren_tsk = player_pipeline_task(stream->video_side->render);
    if (stream->video_side->decoder) {
        esp_gmf_pipeline_destroy(stream->video_side->decoder);
        stream->video_side->decoder = NULL;
    }
    if (stream->video_side->render) {
        esp_gmf_pipeline_destroy(stream->video_side->render);
        stream->video_side->render = NULL;
    }
    if (dec_tsk) {
        esp_gmf_task_deinit(dec_tsk);
    }
    if (ren_tsk) {
        esp_gmf_task_deinit(ren_tsk);
    }
    if (db) {
        esp_gmf_db_deinit(db);
    }
    if (stream->video_side && stream->video_side->data_bus) {
        player_data_bus_destroy(stream->video_side->data_bus);
        stream->video_side->data_bus = NULL;
    }
}

void player_destroy_extractor_path(esp_player_stream_t *stream)
{
    esp_gmf_task_handle_t ext_tsk = player_pipeline_task(stream->extractor);
    if (ext_tsk) {
        esp_gmf_task_deinit(ext_tsk);
    }
    if (stream->extractor) {
#if CONFIG_ESP_PLAYER_ENABLE_AUDIO
        if (stream->audio_side) {
            stream->audio_side->track_info.audio_info.spec_info = NULL;
            stream->audio_side->track_info.audio_info.spec_info_len = 0;
        }
#endif  /* CONFIG_ESP_PLAYER_ENABLE_AUDIO */
        esp_gmf_pipeline_destroy(stream->extractor);
        stream->extractor = NULL;
    }
    if (stream->audio_side && stream->audio_side->extractor_queue) {
        xQueueReset(stream->audio_side->extractor_queue);
        vQueueDelete(stream->audio_side->extractor_queue);
        stream->audio_side->extractor_queue = NULL;
    }
    if (stream->video_side && stream->video_side->extractor_queue) {
        xQueueReset(stream->video_side->extractor_queue);
        vQueueDelete(stream->video_side->extractor_queue);
        stream->video_side->extractor_queue = NULL;
    }
}

esp_gmf_err_t player_stop_decoder(esp_player_stream_t *stream, QueueHandle_t queue,
                                  uint8_t task_status, uint8_t bit,
                                  esp_gmf_pipeline_handle_t pipe_hd,
                                  esp_gmf_db_handle_t db)
{
    player_drop_single_queue(stream, queue);
    if (db) {
        esp_gmf_db_abort(db);
    }
    player_send_null_queue(queue);
    if (task_status & bit) {
        return esp_gmf_pipeline_stop(pipe_hd);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_stop_render(esp_player_stream_t *stream, uint8_t task_status, uint8_t bit,
                                 esp_gmf_db_handle_t db,
                                 esp_gmf_pipeline_handle_t pipe_hd)
{
    if (task_status & bit) {
        if (db) {
            esp_gmf_db_abort(db);
            esp_gmf_db_done_write(db);
        }
        return esp_gmf_pipeline_stop(pipe_hd);
    }
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t player_stop_extractor(esp_player_stream_t *stream)
{
    if ((stream->task_status & TASK_STATUS_EXTRACTOR_RUNNING) == 0) {
        return ESP_GMF_ERR_OK;
    }
    player_drop_all_queues(stream);
    esp_gmf_io_t *io = (esp_gmf_io_t *)stream->input_handle;
    if (io && io->prev_close) {
        io->prev_close(io);
    }
    return esp_gmf_pipeline_stop(stream->extractor);
}

void player_pause_extractor_task(esp_player_stream_t *stream,
                                 esp_gmf_event_state_t *state,
                                 esp_gmf_err_t *ret)
{
    esp_gmf_task_handle_t tsk = player_pipeline_task(stream->extractor);
    uint32_t retry = 0;
    player_drop_all_queues(stream);
    while (tsk && *state != ESP_GMF_EVENT_STATE_PAUSED
           && (stream->task_status & TASK_STATUS_EXTRACTOR_RUNNING)) {
        *ret |= esp_gmf_task_pause(tsk);
        esp_gmf_task_get_state(tsk, state);
        if (++retry > 1000) {
            ESP_LOGW(ESP_PLAYER_TAG, "Pause extractor retry overflow");
            break;
        }
    }
}

void player_pause_decoder_task(esp_player_stream_t *stream,
                               esp_gmf_task_handle_t decoder_task,
                               esp_gmf_db_handle_t db, QueueHandle_t queue,
                               esp_gmf_event_state_t *state,
                               uint8_t bit, esp_gmf_err_t *ret)
{
    player_set_task_timeout(decoder_task, 100);
    if (queue) {
        player_drop_single_queue(stream, queue);
    }
    uint32_t retry = 0;
    while (*state != ESP_GMF_EVENT_STATE_PAUSED && (stream->task_status & bit)) {
        if (queue && uxQueueSpacesAvailable(queue) > 0) {
            esp_gmf_payload_t load = {
                .buf = NULL,
                .valid_size = 0,
                .is_done = false,
            };
            xQueueSend(queue, &load, 0);
        }
        if (db) {
            player_reset_data_bus_meta_for_db(stream, db);
            esp_gmf_db_abort(db);
        }
        *ret |= esp_gmf_task_pause(decoder_task);
        esp_gmf_task_get_state(decoder_task, state);
        if (++retry > 1000) {
            ESP_LOGW(ESP_PLAYER_TAG, "Pause decoder retry overflow");
            break;
        }
    }
    player_set_task_timeout(decoder_task, TASK_TIMEOUT_MS);
}

void player_raise_error_source(esp_player_stream_t *stream,
                               esp_player_error_source_t error_source,
                               const char *reason)
{
    if (stream->error_source != ESP_PLAYER_ERROR_SOURCE_NONE && stream->error_source != error_source) {
        ESP_LOGW(ESP_PLAYER_TAG, "Error source overwrite: %d -> %d (%s)",
                 stream->error_source, error_source, reason ? reason : "");
    } else {
        ESP_LOGE(ESP_PLAYER_TAG, "Error source raised: %d (%s)", error_source, reason ? reason : "");
    }
    stream->error_source = error_source;
}

esp_player_err_t player_run_pipeline_with_timeout(esp_player_stream_t *stream,
                                                  esp_gmf_task_handle_t task,
                                                  uint32_t timeout_ms,
                                                  esp_gmf_pipeline_handle_t pipeline,
                                                  esp_player_error_source_t error_source)
{
    esp_gmf_task_set_timeout(task, timeout_ms);
    if (esp_gmf_pipeline_loading_jobs(pipeline) != ESP_GMF_ERR_OK) {
        return ESP_PLAYER_ERR_FAIL;
    }
    if (esp_gmf_pipeline_run(pipeline) != ESP_GMF_ERR_OK) {
        player_raise_error_source(stream, error_source, "pipeline_run failed");
        return ESP_PLAYER_ERR_NO_MEM;
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t player_create_pipeline_if_expected(esp_player_stream_t *stream,
                                                    player_pipeline_factory_t func,
                                                    uint8_t bit, bool is_audio)
{
    if ((stream->expected_tasks & bit) && !(stream->task_status & bit)) {
        return func(stream, is_audio);
    }
    return ESP_PLAYER_ERR_OK;
}
