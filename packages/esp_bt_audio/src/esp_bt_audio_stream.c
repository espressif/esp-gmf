/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "string.h"

#include "esp_check.h"
#include "esp_bt_audio_stream.h"

static const char *TAG = "BT_AUD_STREAM";

esp_err_t esp_bt_audio_stream_get_codec_info(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_codec_info_t *codec)
{
    esp_bt_audio_stream_base_t *item = (esp_bt_audio_stream_base_t *)handle;
    ESP_RETURN_ON_FALSE(item != NULL && codec != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments: handle=%p, codec=%p", item, codec);
    ESP_RETURN_ON_FALSE(item->profile != ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN, ESP_ERR_INVALID_STATE, TAG, "Invalid state: profile is unknown");
    memcpy(codec, &item->codec_info, sizeof(esp_bt_audio_stream_codec_info_t));
    return ESP_OK;
}

esp_err_t esp_bt_audio_stream_get_local_data(esp_bt_audio_stream_handle_t handle, void **local_data)
{
    esp_bt_audio_stream_base_t *item = (esp_bt_audio_stream_base_t *)handle;
    ESP_RETURN_ON_FALSE(item != NULL && local_data != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments: handle=%p, local_data=%p", item, local_data);
    ESP_RETURN_ON_FALSE(item->profile != ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN, ESP_ERR_INVALID_STATE, TAG, "Invalid state: profile is unknown");
    *local_data = item->local_data;
    return ESP_OK;
}

void *esp_bt_audio_stream_set_local_data(esp_bt_audio_stream_handle_t handle, void *local_data)
{
    esp_bt_audio_stream_base_t *item = (esp_bt_audio_stream_base_t *)handle;
    ESP_RETURN_ON_FALSE(item != NULL, NULL, TAG, "Invalid arguments: handle=%p, local_data=%p", item, local_data);
    ESP_RETURN_ON_FALSE(item->profile != ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN, NULL, TAG, "Invalid state: profile is unknown");
    void *previous = item->local_data;
    item->local_data = local_data;
    return previous;
}

esp_err_t esp_bt_audio_stream_get_dir(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_dir_t *dir)
{
    esp_bt_audio_stream_base_t *item = (esp_bt_audio_stream_base_t *)handle;
    ESP_RETURN_ON_FALSE(item != NULL && dir != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments: handle=%p, dir=%p", item, dir);
    ESP_RETURN_ON_FALSE(item->profile != ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN, ESP_ERR_INVALID_STATE, TAG, "Invalid state: profile is unknown");
    *dir = item->direction;
    return ESP_OK;
}

esp_err_t esp_bt_audio_stream_get_profile(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_profile_t *profile)
{
    esp_bt_audio_stream_base_t *item = (esp_bt_audio_stream_base_t *)handle;
    ESP_RETURN_ON_FALSE(item != NULL && profile != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments: handle=%p, profile=%p", item, profile);
    *profile = item->profile;
    return ESP_OK;
}

esp_err_t esp_bt_audio_stream_get_context(esp_bt_audio_stream_handle_t handle, uint32_t *context)
{
    esp_bt_audio_stream_base_t *item = (esp_bt_audio_stream_base_t *)handle;
    ESP_RETURN_ON_FALSE(item != NULL && context != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments: handle=%p, context=%p", item, context);
    ESP_RETURN_ON_FALSE(item->profile != ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN, ESP_ERR_INVALID_STATE, TAG, "Invalid state: profile is unknown");
    *context = item->context;
    return ESP_OK;
}

esp_err_t esp_bt_audio_stream_acquire_read(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wait)
{
    esp_bt_audio_stream_base_t *item = (esp_bt_audio_stream_base_t *)handle;
    ESP_RETURN_ON_FALSE(item != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments: handle=%p", item);
    ESP_RETURN_ON_FALSE(item->profile != ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN, ESP_ERR_INVALID_STATE, TAG, "Invalid state: profile is unknown");
    ESP_RETURN_ON_FALSE(item->ops.acquire_read != NULL, ESP_ERR_INVALID_STATE, TAG, "Invalid state: acquire_read is NULL");
    return item->ops.acquire_read(handle, packet, wait);
}

esp_err_t esp_bt_audio_stream_release_read(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet)
{
    esp_bt_audio_stream_base_t *item = (esp_bt_audio_stream_base_t *)handle;
    ESP_RETURN_ON_FALSE(item != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments: handle=%p", item);
    ESP_RETURN_ON_FALSE(item->profile != ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN, ESP_ERR_INVALID_STATE, TAG, "Invalid state: profile is unknown");
    ESP_RETURN_ON_FALSE(item->ops.release_read != NULL, ESP_ERR_INVALID_STATE, TAG, "Invalid state: release_read is NULL");
    return item->ops.release_read(handle, packet);
}

esp_err_t esp_bt_audio_stream_acquire_write(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wanted_size)
{
    esp_bt_audio_stream_base_t *item = (esp_bt_audio_stream_base_t *)handle;
    ESP_RETURN_ON_FALSE(item != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments: handle=%p", item);
    ESP_RETURN_ON_FALSE(item->profile != ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN, ESP_ERR_INVALID_STATE, TAG, "Invalid state: profile is unknown");
    ESP_RETURN_ON_FALSE(item->ops.acquire_write != NULL, ESP_ERR_INVALID_STATE, TAG, "Invalid state: acquire_write is NULL");
    return item->ops.acquire_write(handle, packet, wanted_size);
}

esp_err_t esp_bt_audio_stream_release_write(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wait_ms)
{
    esp_bt_audio_stream_base_t *item = (esp_bt_audio_stream_base_t *)handle;
    ESP_RETURN_ON_FALSE(item != NULL, ESP_ERR_INVALID_ARG, TAG, "Invalid arguments: handle=%p", item);
    ESP_RETURN_ON_FALSE(item->profile != ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN, ESP_ERR_INVALID_STATE, TAG, "Invalid state: profile is unknown");
    ESP_RETURN_ON_FALSE(item->ops.release_write != NULL, ESP_ERR_INVALID_STATE, TAG, "Invalid state: release_write is NULL");
    return item->ops.release_write(handle, packet, wait_ms);
}
