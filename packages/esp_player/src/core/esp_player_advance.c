/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>

#include "esp_player_advance.h"
#include "player_internal.h"
#include "player_submit_frame.h"

void player_free_custom_elements(esp_player_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }
    free(stream->custom_elements);
    stream->custom_elements = NULL;
}

esp_player_err_t esp_player_set_custom_elements(esp_player_handle_t handle,
                                                const esp_player_custom_elements_t *custom)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p", handle);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    if (!is_state_allowed_for_operation(stream)) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to set custom elements. state: %d", stream->main_state);
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    if (custom == NULL) {
        player_free_custom_elements(stream);
        return ESP_PLAYER_ERR_OK;
    }
    if (stream->custom_elements == NULL) {
        stream->custom_elements = (esp_player_custom_elements_t *)calloc(1, sizeof(esp_player_custom_elements_t));
        if (stream->custom_elements == NULL) {
            ESP_LOGE(ESP_PLAYER_TAG, "Failed to allocate custom elements");
            return ESP_PLAYER_ERR_NO_MEM;
        }
    }
    *stream->custom_elements = *custom;
    return ESP_PLAYER_ERR_OK;
}

esp_player_err_t esp_player_set_dec_cfg(esp_player_handle_t handle, esp_player_format_t type, void *cfg, uint32_t cfg_sz)
{
    if (handle == NULL || cfg == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, cfg: %p", handle, cfg);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    if (!is_state_allowed_for_operation(stream)) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to set dec cfg. state: %d", stream->main_state);
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    return player_set_dec_cfg_impl(stream, type, cfg, cfg_sz);
}

esp_player_err_t esp_player_submit_frame(esp_player_handle_t handle, esp_player_frame_t *frame, uint32_t timeout_ms)
{
    if (handle == NULL || frame == NULL || frame->data == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, frame: %p, frame->data: %p", handle, frame, frame == NULL ? NULL : frame->data);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    if (stream->main_state != PLAYER_STATE_PLAYING && stream->main_state != PLAYER_STATE_PREPARING) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to submit frame. state: %d", stream->main_state);
        return ESP_PLAYER_ERR_INVALID_STATE;
    }
    if (stream->av_mask == ESP_PLAYER_MASK_AV) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to submit frame: MASK_AV not supported");
        return ESP_PLAYER_ERR_FAIL;
    }
    if (stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_FILL) {
        return player_submit_frame_fill(stream, frame, timeout_ms);
    }
    if (stream->dec_frame_mode == ESP_PLAYER_DEC_FRAME_MODE_BLOCK) {
        return player_submit_frame_block(stream, frame);
    }
    ESP_LOGE(ESP_PLAYER_TAG, "Failed to submit frame: set correct url first");
    return ESP_PLAYER_ERR_NOT_SUPPORT;
}

esp_player_err_t esp_player_set_task_config(esp_player_handle_t handle, const esp_player_task_config_t *config)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p", handle);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    if (!is_state_allowed_for_operation(stream)) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to set task config. state: %d", stream->main_state);
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    return player_set_task_config_impl(stream, config);
}

esp_player_err_t esp_player_set_buffer_config(esp_player_handle_t handle, const esp_player_buffer_config_t *config)
{
    if (handle == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p", handle);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    esp_player_stream_t *stream = (esp_player_stream_t *)handle;
    if (!is_state_allowed_for_operation(stream)) {
        ESP_LOGE(ESP_PLAYER_TAG, "Failed to set buffer config. state: %d", stream->main_state);
        return ESP_PLAYER_ERR_NOT_SUPPORT;
    }
    return player_set_buffer_config_impl(stream, config);
}

esp_player_err_t esp_player_get_id3_info(esp_player_handle_t handle, void *id3_info)
{
    if (handle == NULL || id3_info == NULL) {
        ESP_LOGE(ESP_PLAYER_TAG, "Invalid argument, handle: %p, id3_info: %p", handle, id3_info);
        return ESP_PLAYER_ERR_INVALID_ARG;
    }
    /* TODO implement this function when extractor support id3 parser */
    return ESP_PLAYER_ERR_NOT_SUPPORT;
}
