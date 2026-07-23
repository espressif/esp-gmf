/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_a2dp_api.h"
#if CONFIG_BT_HFP_CLIENT_ENABLE
#include "esp_hf_client_api.h"
#endif  /* CONFIG_BT_HFP_CLIENT_ENABLE */
#if CONFIG_BT_HFP_AG_ENABLE
#include "esp_hf_ag_api.h"
#endif  /* CONFIG_BT_HFP_AG_ENABLE */

#include "bt_audio_classic_stream.h"

#define CLASSIC_STREAM_QUEUE_SIZE  (20)

static const char *TAG = "BT_AUD_CLASSIC_STREAM";

#if CONFIG_BT_HFP_CLIENT_ENABLE || CONFIG_BT_HFP_AG_ENABLE
static uint8_t *bt_audio_hfp_buff_get_data(void *audio_buf)
{
    return ((esp_hf_audio_buff_t *)audio_buf)->data;
}

static void bt_audio_hfp_buff_set_data_len(void *audio_buf, uint32_t data_len)
{
    ((esp_hf_audio_buff_t *)audio_buf)->data_len = data_len;
}
#endif  /* CONFIG_BT_HFP_CLIENT_ENABLE || CONFIG_BT_HFP_AG_ENABLE */

#if CONFIG_BT_HFP_CLIENT_ENABLE
static void *bt_audio_hfp_client_buff_alloc(uint32_t size)
{
    return esp_hf_client_audio_buff_alloc(size);
}

static void bt_audio_hfp_client_buff_free(void *audio_buf)
{
    esp_hf_client_audio_buff_free(audio_buf);
}

static esp_err_t bt_audio_hfp_client_data_send(uint32_t conn_handle, void *audio_buf)
{
    return esp_hf_client_audio_data_send(conn_handle, audio_buf);
}

const bt_audio_classic_stream_data_ops_t bt_audio_hfp_client_stream_data_ops = {
    .audio_buff_alloc = bt_audio_hfp_client_buff_alloc,
    .audio_buff_free = bt_audio_hfp_client_buff_free,
    .audio_buff_get_data = bt_audio_hfp_buff_get_data,
    .audio_buff_set_data_len = bt_audio_hfp_buff_set_data_len,
    .audio_data_send = bt_audio_hfp_client_data_send,
};
#endif  /* CONFIG_BT_HFP_CLIENT_ENABLE */

#if CONFIG_BT_HFP_AG_ENABLE
static void *bt_audio_hfp_ag_buff_alloc(uint32_t size)
{
    return esp_hf_ag_audio_buff_alloc(size);
}

static void bt_audio_hfp_ag_buff_free(void *audio_buf)
{
    esp_hf_ag_audio_buff_free(audio_buf);
}

static esp_err_t bt_audio_hfp_ag_data_send(uint32_t conn_handle, void *audio_buf)
{
    return esp_hf_ag_audio_data_send(conn_handle, audio_buf);
}

const bt_audio_classic_stream_data_ops_t bt_audio_hfp_ag_stream_data_ops = {
    .audio_buff_alloc = bt_audio_hfp_ag_buff_alloc,
    .audio_buff_free = bt_audio_hfp_ag_buff_free,
    .audio_buff_get_data = bt_audio_hfp_buff_get_data,
    .audio_buff_set_data_len = bt_audio_hfp_buff_set_data_len,
    .audio_data_send = bt_audio_hfp_ag_data_send,
};
#endif  /* CONFIG_BT_HFP_AG_ENABLE */

static void bt_audio_classic_stream_free_data(bt_audio_classic_stream_t *stream, void *data_owner)
{
    if (!stream || !data_owner) {
        return;
    }
#ifdef CONFIG_BT_A2DP_ENABLE
    if (stream->base.profile == ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP) {
        esp_a2d_audio_buff_free(data_owner);
        return;
    }
#endif  /* CONFIG_BT_A2DP_ENABLE */
    if (stream->base.profile == ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_HFP && stream->data_ops && stream->data_ops->audio_buff_free) {
        stream->data_ops->audio_buff_free(data_owner);
    }
}

static esp_err_t bt_audio_classic_stream_acquire_read(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wait_ms)
{
    bt_audio_classic_stream_t *stream = (bt_audio_classic_stream_t *)handle;
    if (!stream || stream->base.direction != ESP_BT_AUDIO_STREAM_DIR_SINK) {
        ESP_LOGE(TAG, "Invalid stream handle or direction");
        return ESP_ERR_INVALID_ARG;
    }
    if (!stream->base.data_q) {
        ESP_LOGE(TAG, "Stream data queue not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (xQueueReceive(stream->base.data_q, packet, pdMS_TO_TICKS(wait_ms)) != pdTRUE) {
        ESP_LOGD(TAG, "Failed to receive audio data from queue");
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static esp_err_t bt_audio_classic_stream_release_read(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet)
{
    bt_audio_classic_stream_t *stream = (bt_audio_classic_stream_t *)handle;
    if (!stream || stream->base.direction != ESP_BT_AUDIO_STREAM_DIR_SINK) {
        ESP_LOGE(TAG, "Invalid stream handle or direction");
        return ESP_ERR_INVALID_ARG;
    }

    if (packet->data_owner) {
        bt_audio_classic_stream_free_data(stream, packet->data_owner);
        packet->data_owner = NULL;
    }
    return ESP_OK;
}

static esp_err_t bt_audio_classic_stream_acquire_write(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wanted_size)
{
    bt_audio_classic_stream_t *stream = (bt_audio_classic_stream_t *)handle;
    if (!stream || !packet || wanted_size == 0) {
        ESP_LOGE(TAG, "Invalid args: stream=%p, packet=%p, wanted_size=%d", stream, packet, wanted_size);
        return ESP_ERR_INVALID_ARG;
    }

    if (!stream->conn_handle || stream->base.direction != ESP_BT_AUDIO_STREAM_DIR_SOURCE) {
        ESP_LOGE(TAG, "Invalid state: conn_handle=%p, direction=%d", stream->conn_handle, stream->base.direction);
        return ESP_ERR_INVALID_STATE;
    }

    switch (stream->base.profile) {
        case ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_HFP: {
            if (!stream->data_ops || !stream->data_ops->audio_buff_alloc || !stream->data_ops->audio_buff_get_data) {
                return ESP_ERR_INVALID_STATE;
            }
            void *hf_buf = stream->data_ops->audio_buff_alloc(wanted_size);
            if (!hf_buf) {
                return ESP_ERR_NO_MEM;
            }
            packet->data = stream->data_ops->audio_buff_get_data(hf_buf);
            packet->size = wanted_size;
            packet->data_owner = hf_buf;
            break;
        }
        case ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP: {
#ifdef CONFIG_BT_A2DP_ENABLE
            if (!stream->base.data_q) {
                return ESP_ERR_INVALID_STATE;
            }
            esp_a2d_audio_buff_t *a2dp_buf = esp_a2d_audio_buff_alloc(wanted_size);
            if (!a2dp_buf) {
                return ESP_ERR_NO_MEM;
            }
            packet->data = a2dp_buf->data;
            packet->size = wanted_size;
            packet->data_owner = a2dp_buf;
#endif  /* CONFIG_BT_A2DP_ENABLE */
            break;
        }
        default:
            ESP_LOGE(TAG, "Invalid profile: %d", stream->base.profile);
            return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

static esp_err_t bt_audio_classic_stream_release_write(esp_bt_audio_stream_handle_t handle, esp_bt_audio_stream_packet_t *packet, uint32_t wait_ms)
{
    esp_err_t ret = ESP_OK;
    bt_audio_classic_stream_t *stream = (bt_audio_classic_stream_t *)handle;
    if (!stream || !packet || !packet->data_owner) {
        ESP_LOGE(TAG, "Invalid args: stream=%p, packet=%p, packet->data_owner=%p", stream, packet, packet->data_owner);
        ret = ESP_ERR_INVALID_ARG;
        goto exit;
    }

    if (!stream->conn_handle || stream->base.direction != ESP_BT_AUDIO_STREAM_DIR_SOURCE) {
        ESP_LOGE(TAG, "Invalid state: conn_handle=%p, direction=%d", stream->conn_handle, stream->base.direction);
        ret = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    switch (stream->base.profile) {
        case ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_HFP: {
            if (!stream->data_ops || !stream->data_ops->audio_buff_set_data_len || !stream->data_ops->audio_data_send) {
                ret = ESP_ERR_INVALID_STATE;
                break;
            }
            stream->data_ops->audio_buff_set_data_len(packet->data_owner, packet->size);
            ret = stream->data_ops->audio_data_send(stream->conn_handle, packet->data_owner);
            break;
        }
        case ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP: {
#ifdef CONFIG_BT_A2DP_ENABLE
            esp_a2d_audio_buff_t *a2dp_buf = (esp_a2d_audio_buff_t *)packet->data_owner;
            a2dp_buf->data_len = packet->size;
            ret = xQueueSend(stream->base.data_q, packet, pdMS_TO_TICKS(wait_ms)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
#endif  /* CONFIG_BT_A2DP_ENABLE */
            break;
        }
        default:
            ESP_LOGE(TAG, "Invalid profile: %d", stream->base.profile);
            ret = ESP_ERR_INVALID_STATE;
            break;
    }

exit:
    if (stream && packet && packet->data_owner && ret != ESP_OK) {
        bt_audio_classic_stream_free_data(stream, packet->data_owner);
        packet->data_owner = NULL;
        packet->data = NULL;
    }
    return ret;
}

esp_err_t bt_audio_classic_stream_create(bt_audio_classic_stream_t **out_stream)
{
    if (!out_stream) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_stream = NULL;

    bt_audio_classic_stream_t *stream = heap_caps_calloc_prefer(1, sizeof(bt_audio_classic_stream_t), 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, MALLOC_CAP_DEFAULT);
    if (!stream) {
        ESP_LOGE(TAG, "Stream calloc failed");
        return ESP_ERR_NO_MEM;
    }
    stream->base.profile = ESP_BT_AUDIO_STREAM_PROFILE_UNKNOWN;
    stream->base.data_q = xQueueCreate(CLASSIC_STREAM_QUEUE_SIZE, sizeof(esp_bt_audio_stream_packet_t));
    if (!stream->base.data_q) {
        ESP_LOGE(TAG, "Stream queue create failed");
        free(stream);
        return ESP_ERR_NO_MEM;
    }
    stream->base.ops.acquire_read = bt_audio_classic_stream_acquire_read;
    stream->base.ops.release_read = bt_audio_classic_stream_release_read;
    stream->base.ops.acquire_write = bt_audio_classic_stream_acquire_write;
    stream->base.ops.release_write = bt_audio_classic_stream_release_write;
    ESP_LOGI(TAG, "stream new %p", stream);
    *out_stream = stream;
    return ESP_OK;
}

void bt_audio_classic_stream_destroy(bt_audio_classic_stream_t *stream)
{
    ESP_LOGI(TAG, "stream release %p", stream);
    if (stream) {
        esp_bt_audio_stream_packet_t msg = {0};
        if (stream->base.data_q) {
            while (xQueueReceive(stream->base.data_q, &msg, 0) == pdTRUE) {
                bt_audio_classic_stream_free_data(stream, msg.data_owner);
            }
            vQueueDelete(stream->base.data_q);
        }

        if (stream->base.codec_info.codec_cfg) {
            free(stream->base.codec_info.codec_cfg);
            stream->base.codec_info.codec_cfg = NULL;
        }

        free(stream);
    }
}
