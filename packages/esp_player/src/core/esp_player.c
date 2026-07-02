/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "esp_player.h"
#include "player_internal.h"
#include "player_pipeline.h"
#include "player_submit_frame.h"
#include "player_url.h"

static void player_cmd_task(void *arg)
{
    esp_player_stream_t *stream = (esp_player_stream_t *)arg;
    esp_player_cmd_msg_t cmd;
    while (1) {
        if (xQueueReceive(stream->cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            handle_state_cmd(stream, &cmd);
            if (cmd.cmd_type == ESP_PLAYER_CMD_QUIT) {
                ESP_LOGD(ESP_PLAYER_TAG, "Command task exiting, deleting itself");
                player_set_events(stream, _CTRL_PLAYER_QUIT);
                stream->cmd_task = NULL;
                vTaskDelete(NULL);
                return;
            }
        }
    }
}

static void _player_clean_pipeline(esp_player_stream_t *stream)
{
    player_destroy_audio_path(stream);
    player_destroy_video_path(stream);
    player_destroy_extractor_path(stream);
}

static esp_player_track_info_t *player_cached_track_info(esp_player_stream_t *stream, esp_player_track_type_t type)
{
    if (type == ESP_PLAYER_TRACK_TYPE_AUDIO) {
        if ((stream->av_mask & ESP_PLAYER_MASK_AUDIO) && stream->audio_side) {
            return &stream->audio_side->track_info;
        }
    } else if ((stream->av_mask & ESP_PLAYER_MASK_VIDEO) && stream->video_side) {
        return &stream->video_side->track_info;
    }
    return NULL;
}

esp_player_err_t esp_player_init(esp_player_config_t *config, esp_player_handle_t *handle)
{
    if (handle == NULL || config == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, config: %p", handle, config);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_err_t ve = player_validate_init_config(config);
    if (ve != ESP_PLAYER_ERR_OK) {
        return ve;
    }
    esp_player_err_t ret = ESP_PLAYER_ERR_OK;

    esp_player_stream_t *stream = (esp_player_stream_t *)calloc(1, sizeof(esp_player_stream_t));
    if (stream == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to allocate memory for stream");
        return ESP_PLAYER_ERR_NO_MEM;
    }
    stream->audio_render_hd = config->audio_render_hd;
    stream->video_render_hd = config->video_render_hd;

    stream->cmd_queue = xQueueCreate(ESP_PLAYER_CMD_QUEUE_SIZE, sizeof(esp_player_cmd_msg_t));
    if (stream->cmd_queue == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to create command queue");
        ret = ESP_PLAYER_ERR_NO_MEM;
        goto _exit;
    }

    stream->sync_evt = xEventGroupCreate();
    if (stream->sync_evt == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to create sync event group");
        ret = ESP_PLAYER_ERR_NO_MEM;
        goto _exit;
    }

    stream->lock = xSemaphoreCreateMutex();
    if (stream->lock == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to create mutex");
        ret = ESP_PLAYER_ERR_NO_MEM;
        goto _exit;
    }

    stream->lock_resource = xSemaphoreCreateMutex();
    if (stream->lock_resource == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to create mutex");
        ret = ESP_PLAYER_ERR_NO_MEM;
        goto _exit;
    }

    player_state_init();

    BaseType_t task_ret = xTaskCreate(player_cmd_task, "player_cmd", 4096, stream, 5, &stream->cmd_task);
    if (task_ret != pdPASS) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to create command task");
        ret = ESP_PLAYER_ERR_FAIL;
        goto _exit;
    }

    stream->dec_frame_mode = ESP_PLAYER_DEC_FRAME_MODE_UNKNOWN;
    player_sync_config_t sync_cfg = PLAYER_SYNC_CONFIG_DEFAULT();
    sync_cfg.bit_ctx = stream;
    sync_cfg.audio_resume_bit = _CTRL_PLAYER_SYNC_AUDIO_RESUMED;
    sync_cfg.video_resume_bit = _CTRL_PLAYER_SYNC_VIDEO_RESUMED;
    sync_cfg.audio_render_stream = stream->audio_render_hd;
    ret = player_sync_create(&sync_cfg, &stream->sync_handle);
    if (ret != ESP_PLAYER_ERR_OK || stream->sync_handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to create sync handle");
        goto _exit;
    }

    *handle = stream;
    return ret;
_exit:
    esp_player_deinit(stream);
    return ret;
}

esp_player_err_t esp_player_deinit(esp_player_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p", handle);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;

    if (stream->cmd_task) {
        esp_player_cmd_msg_t cmd = {
            .cmd_type = ESP_PLAYER_CMD_QUIT,
            .data = NULL,
            .data_len = 0};
        player_send_cmd(stream, &cmd);
        player_wait_events(stream, _CTRL_PLAYER_QUIT, portMAX_DELAY);
    }

    _player_clean_pipeline(stream);

    if (stream->audio_side) {
        free(stream->audio_side);
        stream->audio_side = NULL;
    }
    if (stream->video_side) {
        free(stream->video_side);
        stream->video_side = NULL;
    }

    player_destroy_input_io(stream);
    if (stream->io_pool) {
        esp_gmf_pool_deinit(stream->io_pool);
        stream->io_pool = NULL;
    }

    if (stream->sync_handle) {
        player_sync_destroy(stream->sync_handle);
        stream->sync_handle = NULL;
    }

    if (stream->sync_evt) {
        player_clear_events(stream, _CTRL_ALL_EVENTS);
        vEventGroupDelete(stream->sync_evt);
        stream->sync_evt = NULL;
    }

    if (stream->lock) {
        vSemaphoreDelete(stream->lock);
        stream->lock = NULL;
    }
    if (stream->lock_resource) {
        vSemaphoreDelete(stream->lock_resource);
        stream->lock_resource = NULL;
    }

    frame_pool_destroy(stream->fill_pool);
    stream->fill_pool = NULL;
    if (stream->buffer_ctrl) {
        free(stream->buffer_ctrl);
        stream->buffer_ctrl = NULL;
    }

    player_deinit_decoder_subcfg(stream);
    player_free_custom_elements(stream);
    player_free_runtime_config(stream);
    player_id3_reset(stream);
    free(stream->frame_url);
    stream->frame_url = NULL;

    if (stream->cmd_queue) {
        vQueueDelete(stream->cmd_queue);
        stream->cmd_queue = NULL;
    }
    free(stream);
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_set_av_mask(esp_player_handle_t handle, uint8_t mask)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p", handle);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    if (!is_state_allowed_for_operation(stream)) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to set av_mask. state: %d", stream->main_state);
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    const uint8_t build_mask = player_build_time_av_mask();
    if ((mask & ~build_mask) != 0) {
        ESP_LOGE(ESP_PLAYER_TAG, "av_mask 0x%x requests path(s) not built in (build_mask 0x%x)", mask, build_mask);
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    if (!player_build_time_has_full_av() && mask == ESP_PLAYER_MASK_AV) {
        ESP_LOGE(ESP_PLAYER_TAG, "ESP_PLAYER_MASK_AV requires both audio and video at build time");
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    if (stream->av_mask == mask) {
        return ESP_PLAYER_ERR_OK;
    }

    _player_clean_pipeline(stream);
    esp_player_err_t ret = player_side_reconcile(stream, mask);
    if (ret != ESP_PLAYER_ERR_OK) {
        return ret;
    }
    stream->av_mask = mask;
    ESP_LOGI(ESP_PLAYER_TAG, "Set av_mask: %d", mask);
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_set_url(esp_player_handle_t handle, const char *url)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle is NULL");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    if (!is_state_allowed_for_operation(stream)) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to set url. url: %s, state: %d", url ? url : "(null)", stream->main_state);
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    return _player_update_url(stream, url);
}

esp_player_err_t esp_player_set_sync_mode(esp_player_handle_t handle, esp_player_sync_mode_t sync_mode)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle is NULL");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_err_t ret = player_validate_sync_mode(sync_mode);
    if (ret != ESP_PLAYER_ERR_OK) {
        return ret;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    if (!is_state_allowed_for_operation(stream)) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to set sync mode. mode: %d, state: %d", (int)sync_mode, stream->main_state);
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    if (stream->sync_handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Sync handle not ready");
        return ESP_PLAYER_ERR_FAIL;
    }
    ret = player_sync_set_mode(stream->sync_handle, sync_mode);
    if (ret == ESP_PLAYER_ERR_OK) {
        ESP_LOGI(ESP_PLAYER_TAG, "Set sync_mode: %d", (int)sync_mode);
    }
    return ret;
}

esp_player_err_t esp_player_set_data_src(esp_player_handle_t handle, const esp_player_data_src_t *src)
{
    if (handle == NULL || src == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, src: %p", handle, src);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (src->av_mask == 0) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid av_mask: 0");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (src->url == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, url is NULL");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    esp_player_err_t ret = esp_player_set_av_mask(handle, src->av_mask);
    if (ret != ESP_PLAYER_ERR_OK) {
        return ret;
    }
    ret = esp_player_set_url(handle, src->url);
    if (ret != ESP_PLAYER_ERR_OK) {
        return ret;
    }
    ret = esp_player_set_sync_mode(handle, src->sync_mode);
    if (ret != ESP_PLAYER_ERR_OK) {
        return ret;
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_set_event_cb(esp_player_handle_t handle, esp_player_event_callback_t cb, void *ctx)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p", handle);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    if (!is_state_allowed_for_operation(stream)) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to set event cb. state: %d", stream->main_state);
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    stream->event_cb = cb;
    stream->event_ctx = ctx;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_set_event_queue(esp_player_handle_t handle, void *queue)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, queue: %p", handle, (void *)queue);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    if (!is_state_allowed_for_operation(stream)) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to set event queue. state: %d", stream->main_state);
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    stream->event_queue = (QueueHandle_t)queue;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_run(esp_player_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p", handle);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;

    if ((stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_FILL || stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_BLOCK) && stream->av_mask == ESP_PLAYER_MASK_AV) {
        ESP_LOGE(ESP_PLAYER_TAG, "Cannot run in dec_frame_mode: %d, av_mask: %d", stream->dec_frame_mode, stream->av_mask);
        return ESP_PLAYER_ERR_FAIL;
    }

    if (stream->main_state != PLAYER_STATE_IDLE && stream->main_state != PLAYER_STATE_STOPPED && stream->main_state != PLAYER_STATE_FINISHED) {
        ESP_LOGW(ESP_PLAYER_TAG, "Cannot run in state: %s", get_state_name(stream->main_state));
        return ESP_PLAYER_ERR_INVALID_STATE;
    }

    if (stream->lock == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Lock is NULL, player may be deinitialized");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    xSemaphoreTake(stream->lock, portMAX_DELAY);
    player_clear_events(stream, _CTRL_PLAYER_RUN);

    esp_player_cmd_msg_t cmd = {
        .cmd_type = ESP_PLAYER_CMD_PREPARE,
        .data = NULL,
        .data_len = 0};

    esp_player_err_t ret = player_send_cmd(stream, &cmd);
    if (ret != ESP_PLAYER_ERR_OK) {
        xSemaphoreGive(stream->lock);
        return ret;
    }

    xSemaphoreGive(stream->lock);
    player_wait_events(stream, _CTRL_PLAYER_RUN, portMAX_DELAY);
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_run_to_end(esp_player_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p", handle);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;

    if ((stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_FILL || stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_BLOCK) && stream->av_mask == ESP_PLAYER_MASK_AV) {
        ESP_LOGE(ESP_PLAYER_TAG, "Cannot run in dec_frame_mode: %d, av_mask: %d", stream->dec_frame_mode, stream->av_mask);
        return ESP_PLAYER_ERR_FAIL;
    }

    if (stream->main_state != PLAYER_STATE_IDLE && stream->main_state != PLAYER_STATE_STOPPED && stream->main_state != PLAYER_STATE_FINISHED) {
        ESP_LOGW(ESP_PLAYER_TAG, "Cannot run_to_end in state: %s", get_state_name(stream->main_state));
        return ESP_PLAYER_ERR_INVALID_STATE;
    }
    player_clear_events(stream, _CTRL_RUN_TO_END);
    esp_player_cmd_msg_t cmd = {
        .cmd_type = ESP_PLAYER_CMD_PREPARE,
        .data = NULL,
        .data_len = 0};
    esp_player_err_t ret = player_send_cmd(stream, &cmd);
    if (ret != ESP_PLAYER_ERR_OK) {
        return ret;
    }

    ret = player_wait_events(stream, _CTRL_RUN_TO_END, portMAX_DELAY);
    if (ret != ESP_PLAYER_ERR_OK) {
        return ret;
    }
    ESP_LOGD(ESP_PLAYER_TAG, "esp_player_run_to_end completed");
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_pause(esp_player_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, line: %d", handle, __LINE__);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;

    if (stream->main_state != PLAYER_STATE_PLAYING) {
        ESP_LOGW(ESP_PLAYER_TAG, "Cannot pause in state: %s", get_state_name(stream->main_state));
        return ESP_PLAYER_ERR_INVALID_STATE;
    }
    if (stream->lock == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Lock is NULL, player may be deinitialized");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    xSemaphoreTake(stream->lock, portMAX_DELAY);
    esp_player_cmd_msg_t cmd = {
        .cmd_type = ESP_PLAYER_CMD_PAUSE,
        .data = NULL,
        .data_len = 0};
    esp_player_err_t ret = player_send_cmd(stream, &cmd);
    if (ret != ESP_PLAYER_ERR_OK) {
        xSemaphoreGive(stream->lock);
        return ret;
    }

    xSemaphoreGive(stream->lock);
    player_wait_events(stream, _CTRL_PLAYER_PAUSED | _CTRL_RUN_TO_END, portMAX_DELAY);
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_resume(esp_player_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, line: %d", handle, __LINE__);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;

    if (stream->main_state != PLAYER_STATE_PAUSED) {
        ESP_LOGW(ESP_PLAYER_TAG, "Cannot resume in state: %s", get_state_name(stream->main_state));
        return ESP_PLAYER_ERR_INVALID_STATE;
    }
    if (stream->lock == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Lock is NULL, player may be deinitialized");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    xSemaphoreTake(stream->lock, portMAX_DELAY);
    esp_player_cmd_msg_t cmd = {
        .cmd_type = ESP_PLAYER_CMD_RESUME,
        .data = NULL,
        .data_len = 0};
    esp_player_err_t ret = player_send_cmd(stream, &cmd);
    if (ret != ESP_PLAYER_ERR_OK) {
        xSemaphoreGive(stream->lock);
        return ret;
    }

    xSemaphoreGive(stream->lock);
    player_wait_events(stream, _CTRL_PLAYER_RESUMED, portMAX_DELAY);
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_stop(esp_player_handle_t handle)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, line: %d", handle, __LINE__);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;

    if (stream->main_state == PLAYER_STATE_IDLE || stream->main_state == PLAYER_STATE_STOPPED ||
        stream->main_state == PLAYER_STATE_FINISHED || stream->main_state == PLAYER_STATE_ERROR) {
        ESP_LOGW(ESP_PLAYER_TAG, "No need to stop in state: %s", get_state_name(stream->main_state));
        return ESP_PLAYER_ERR_OK;
    }
    if (stream->lock == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Lock is NULL, player may be deinitialized");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    xSemaphoreTake(stream->lock, portMAX_DELAY);
    esp_player_cmd_msg_t cmd = {
        .cmd_type = ESP_PLAYER_CMD_STOP,
        .data = NULL,
        .data_len = 0};
    esp_player_err_t ret = player_send_cmd(stream, &cmd);
    if (ret != ESP_PLAYER_ERR_OK) {
        xSemaphoreGive(stream->lock);
        return ret;
    }

    xSemaphoreGive(stream->lock);
    player_wait_events(stream, _CTRL_PLAYER_STOPPED | _CTRL_RUN_TO_END, portMAX_DELAY);
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_seek(esp_player_handle_t handle, uint64_t time_ms)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, time_ms: %" PRIu64 ", line: %d", handle, time_ms, __LINE__);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;

    if (stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_BLOCK || stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_FILL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Cannot seek in dec_frame_mode: %d", stream->dec_frame_mode);
        return ESP_PLAYER_ERR_FAIL;
    }

    if (stream->main_state == PLAYER_STATE_IDLE || stream->main_state == PLAYER_STATE_PREPARING) {
        ESP_LOGE(ESP_PLAYER_TAG, "Cannot seek in state: %s", get_state_name(stream->main_state));
        return ESP_PLAYER_ERR_INVALID_STATE;
    }
    if (stream->is_seeking) {
        ESP_LOGW(ESP_PLAYER_TAG, "Seek rejected: previous seek in flight (state=%s, is_seeking=1)", get_state_name(stream->main_state));
        return ESP_PLAYER_ERR_INVALID_STATE;
    }

    if (stream->main_state == PLAYER_STATE_FINISHED || stream->main_state == PLAYER_STATE_STOPPED) {
        player_sync_set_seek_target(stream->sync_handle, time_ms);
        player_sync_set_render_pts(stream->sync_handle, time_ms);
        esp_player_event_msg_t event_msg = {
            .event_type = ESP_PLAYER_EVENT_SEEK_DONE,
            .data = NULL,
            .data_len = 0,
        };
        ESP_LOGD(ESP_PLAYER_TAG, "Seek done, time_ms: %" PRIu64 ", state: %s", time_ms, get_state_name(stream->main_state));
        player_send_event(stream, &event_msg);
        return ESP_PLAYER_ERR_OK;
    }
    if (stream->lock == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Lock is NULL, player may be deinitialized");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    uint64_t *seek_time = (uint64_t *)malloc(sizeof(uint64_t));
    if (seek_time == NULL) {
        return ESP_PLAYER_ERR_NO_MEM;
    }
    *seek_time = time_ms;
    xSemaphoreTake(stream->lock, portMAX_DELAY);
    esp_player_cmd_msg_t cmd = {
        .cmd_type = ESP_PLAYER_CMD_SEEK,
        .data = seek_time,
        .data_len = sizeof(time_ms)};
    esp_player_err_t ret = player_send_cmd(stream, &cmd);
    if (ret != ESP_PLAYER_ERR_OK) {
        free(seek_time);
        xSemaphoreGive(stream->lock);
        return ret;
    }

    xSemaphoreGive(stream->lock);
    player_wait_events(stream, _CTRL_PLAYER_SEEKING, portMAX_DELAY);
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_set_speed(esp_player_handle_t handle, float speed)
{
    if (handle == NULL || speed <= 0.0f) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, speed: %f, line: %d", handle, speed, __LINE__);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_err_t ret = ESP_PLAYER_ERR_OK;
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    xSemaphoreTake(stream->lock, portMAX_DELAY);
    player_set_speed_impl(stream, speed, &ret);
    xSemaphoreGive(stream->lock);
    return ret;
}

esp_player_err_t esp_player_get_duration(esp_player_handle_t handle, uint64_t *duration)
{
    if (handle == NULL || duration == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, duration: %p", handle, duration);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    *duration = 0;
    esp_gmf_element_handle_t ext_el = player_extractor_el(stream);
    if (ext_el == NULL) {
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    const esp_extractor_stream_type_t types[2] = {
        ESP_EXTRACTOR_STREAM_TYPE_AUDIO,
        ESP_EXTRACTOR_STREAM_TYPE_VIDEO,
    };
    uint64_t max_duration = 0;
    bool any_track = false;
    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        int8_t active = -1;
        if (player_extractor_track_active(ext_el, types[i], &active) != ESP_GMF_ERR_OK || active < 0) {
            continue;
        }
        esp_extractor_stream_info_t info = {0};
        if (player_extractor_get_stream_info(ext_el, types[i], (uint16_t)active, &info) == ESP_GMF_ERR_OK) {
            any_track = true;
            if (info.duration > max_duration) {
                max_duration = info.duration;
            }
        }
    }
    if (!any_track) {
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    *duration = max_duration;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_get_play_time(esp_player_handle_t handle, uint64_t *current_time)
{
    if (handle == NULL || current_time == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, current_time: %p", handle, current_time);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    if (stream->sync_handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, Sync handle: %p", stream->sync_handle);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    uint64_t audio_pts = player_sync_get_audio_render_pts_ms_with_latency(stream->sync_handle);
    uint64_t video_pts = player_sync_get_video_render_pts_ms(stream->sync_handle);
    if (stream->av_mask == ESP_PLAYER_MASK_AUDIO) {
        *current_time = audio_pts;
    } else if (stream->av_mask == ESP_PLAYER_MASK_VIDEO) {
        *current_time = video_pts;
    } else {
        *current_time = (audio_pts < video_pts) ? audio_pts : video_pts;
    }
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_get_track_num(esp_player_handle_t handle, esp_player_track_type_t type, uint16_t *track_num)
{
    if (handle == NULL || track_num == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, track_num: %p", handle, track_num);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (type != ESP_PLAYER_TRACK_TYPE_AUDIO && type != ESP_PLAYER_TRACK_TYPE_VIDEO) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid track type: %d", (int)type);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    esp_gmf_element_handle_t ext_el = player_extractor_el(stream);
    if (ext_el != NULL) {
        esp_extractor_stream_type_t extractor_type = (type == ESP_PLAYER_TRACK_TYPE_AUDIO) ? ESP_EXTRACTOR_STREAM_TYPE_AUDIO : ESP_EXTRACTOR_STREAM_TYPE_VIDEO;
        if (player_extractor_get_stream_num(ext_el, extractor_type, track_num) != ESP_GMF_ERR_OK) {
            ESP_LOGE(ESP_PLAYER_TAG, "Failed to get stream num");
            return ESP_PLAYER_ERR_FAIL;
        }
        return ESP_PLAYER_ERR_OK;
    }
    if (player_cached_track_info(stream, type) != NULL) {
        *track_num = 1;
        return ESP_PLAYER_ERR_OK;
    }
    ESP_LOGW(ESP_PLAYER_TAG, "Unsupported operation, It is not a streaming, player handle: %p line: %d", handle, __LINE__);
    return ESP_PLAYER_ERR_FAIL;
}

esp_player_err_t esp_player_get_track_info(esp_player_handle_t handle, esp_player_track_type_t type, uint16_t track_idx, esp_player_track_info_t *track_info)
{
    if (handle == NULL || track_info == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, track_info: %p", handle, track_info);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    if (type != ESP_PLAYER_TRACK_TYPE_AUDIO && type != ESP_PLAYER_TRACK_TYPE_VIDEO) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid track type: %d", (int)type);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    esp_player_track_info_t *cached = player_cached_track_info(stream, type);
    if (cached == NULL) {
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    esp_gmf_element_handle_t ext_el = player_extractor_el(stream);
    if (ext_el != NULL) {
        if (player_extractor_get_track_info(ext_el, type, track_idx, track_info) == ESP_GMF_ERR_OK) {
            return ESP_PLAYER_ERR_OK;
        }
        return ESP_PLAYER_ERR_FAIL;
    }
    if (track_idx != 0) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid track_idx: %u (single-path mode supports index 0 only)", (unsigned)track_idx);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    memcpy(track_info, cached, sizeof(esp_player_track_info_t));
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_enable_track(esp_player_handle_t handle, esp_player_track_type_t type, uint16_t track_idx, bool enable)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p", handle);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    esp_gmf_element_handle_t ext_el = player_extractor_el(stream);
    if (stream->av_mask == ESP_PLAYER_MASK_AV && ext_el) {
        if (type != ESP_PLAYER_TRACK_TYPE_AUDIO && type != ESP_PLAYER_TRACK_TYPE_VIDEO) {
            ESP_LOGE(ESP_PLAYER_TAG, "Invalid track type: %d", (int)type);
            return ESP_PLAYER_ERR_INVALID_ARG;
        }
        esp_extractor_stream_type_t extrator_type = (type == ESP_PLAYER_TRACK_TYPE_AUDIO) ? ESP_EXTRACTOR_STREAM_TYPE_AUDIO : ESP_EXTRACTOR_STREAM_TYPE_VIDEO;
        if (player_extractor_enable_stream(ext_el, extrator_type, track_idx, enable) != ESP_GMF_ERR_OK) {
            ESP_LOGE(ESP_PLAYER_TAG, "Failed to enable track, type: %d, track_idx: %d, enable: %d, line: %d", extrator_type, track_idx, enable, __LINE__);
            return ESP_PLAYER_ERR_FAIL;
        }
        return ESP_PLAYER_ERR_OK;
    }
    ESP_LOGI(ESP_PLAYER_TAG, "operation is only supported in AV extractor mode, player handle: %p line: %d", handle, __LINE__);
    return ESP_PLAYER_ERR_NOT_SUPPORT;
}
