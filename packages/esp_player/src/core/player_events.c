/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "player_stream.h"
#include "player_events.h"

static const char *TAG = "ESP_PLAYER_EVENTS";

void player_set_events(esp_player_stream_t *stream, uint32_t event_bits)
{
    if (stream && stream->sync_evt) {
        xEventGroupSetBits(stream->sync_evt, event_bits);
        ESP_LOGI(TAG, "Set events: 0x%" PRIx32, event_bits);
    }
}

void player_clear_events(esp_player_stream_t *stream, uint32_t event_bits)
{
    if (stream && stream->sync_evt) {
        xEventGroupClearBits(stream->sync_evt, event_bits);
        ESP_LOGI(TAG, "Clear events: 0x%" PRIx32, event_bits);
    }
}

esp_player_err_t player_wait_events(esp_player_stream_t *stream, uint32_t event_bits, uint32_t timeout_ms)
{
    if (!stream || !stream->sync_evt) {
        ESP_LOGE(TAG, "Invalid stream or sync event");
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Waiting for events: 0x%" PRIx32 " (timeout: %" PRIu32 "ms)", event_bits, timeout_ms);

    EventBits_t received_bits = xEventGroupWaitBits(
        stream->sync_evt,
        event_bits,
        pdTRUE,
        pdFALSE,
        pdMS_TO_TICKS(timeout_ms));

    if (received_bits & event_bits) {
        ESP_LOGI(TAG, "Events received successfully: 0x%" PRIx32, received_bits);
        return ESP_PLAYER_ERR_OK;
    } else {
        ESP_LOGW(TAG, "Timeout waiting for events, received: 0x%" PRIx32 ", expected: 0x%" PRIx32,
                 received_bits, event_bits);
        return ESP_PLAYER_ERR_TIMEOUT;
    }
}
