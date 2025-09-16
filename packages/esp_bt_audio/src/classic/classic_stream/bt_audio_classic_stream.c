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
#include "esp_hf_client_api.h"

#include "bt_audio_classic_stream.h"

#define CLASSIC_STREAM_QUEUE_SIZE  (20)

static const char *TAG = "BT_AUD_CLASSIC_STREAM";

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
#ifdef CONFIG_BT_A2DP_ENABLE
        if (stream->base.profile == ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP) {
            esp_a2d_audio_buff_free(packet->data_owner);
            packet->data_owner = NULL;
        }
#endif  /* CONFIG_BT_A2DP_ENABLE */
#ifdef CONFIG_BT_HFP_ENABLE
        if (stream->base.profile == ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_HFP) {
            esp_hf_client_audio_buff_free(packet->data_owner);
            packet->data_owner = NULL;
        }
#endif  /* CONFIG_BT_HFP_ENABLE */
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
#ifdef CONFIG_BT_HFP_ENABLE
            esp_hf_audio_buff_t *hf_buf = esp_hf_client_audio_buff_alloc(wanted_size);
            if (!hf_buf) {
                return ESP_ERR_NO_MEM;
            }
            packet->data = hf_buf->data;
            packet->size = wanted_size;
            packet->data_owner = hf_buf;
#endif  /* CONFIG_BT_HFP_ENABLE */
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
#ifdef CONFIG_BT_HFP_ENABLE
            esp_hf_audio_buff_t *hf_buf = (esp_hf_audio_buff_t *)packet->data_owner;
            hf_buf->data_len = packet->size;
            ret = esp_hf_client_audio_data_send(stream->conn_handle, hf_buf);
#endif  /* CONFIG_BT_HFP_ENABLE */
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
    if (packet && packet->data_owner && ret != ESP_OK) {
#ifdef CONFIG_BT_A2DP_ENABLE
        if (stream->base.profile == ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP) {
            esp_a2d_audio_buff_free(packet->data_owner);
        }
#endif  /* CONFIG_BT_A2DP_ENABLE */
#ifdef CONFIG_BT_HFP_ENABLE
        if (stream->base.profile == ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_HFP) {
            esp_hf_client_audio_buff_free(packet->data_owner);
        }
#endif  /* CONFIG_BT_HFP_ENABLE */
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
#ifdef CONFIG_BT_A2DP_ENABLE
                if (stream->base.profile == ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_A2DP) {
                    esp_a2d_audio_buff_free(msg.data_owner);
                }
#endif  /* CONFIG_BT_A2DP_ENABLE */
#ifdef CONFIG_BT_HFP_ENABLE
                if (stream->base.profile == ESP_BT_AUDIO_STREAM_PROFILE_CLASSIC_HFP) {
                    esp_hf_client_audio_buff_free(msg.data_owner);
                }
#endif  /* CONFIG_BT_HFP_ENABLE */
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
