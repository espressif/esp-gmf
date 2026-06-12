/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_err.h"

#include "esp_board_manager_includes.h"

#include "esp_player.h"
#include "audio_player_setup.h"

#define EVT_ANY_ERROR  BIT(15)

static const char *TAG = "AUDIO_PLAYER";

typedef struct {
    EventGroupHandle_t  evt;
    int                 stream_id;
} player_evt_ctx_t;

static esp_err_t mount_sdcard(void)
{
    esp_err_t ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SD card");
    }
    return ret;
}

static void unmount_sdcard(void)
{
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
}

static EventBits_t all_streams_finished_bits(void)
{
    EventBits_t bits = 0;
    for (int i = 0; i < AUDIO_PLAYER_STREAM_NUM; i++) {
        bits |= BIT(i);
    }
    return bits;
}

static esp_player_err_t player_event_cb(esp_player_event_msg_t *msg, void *ctx)
{
    player_evt_ctx_t *event_ctx = (player_evt_ctx_t *)ctx;
    if (event_ctx == NULL || event_ctx->evt == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    switch (msg->event_type) {
        case ESP_PLAYER_EVENT_PLAYED:
            ESP_LOGI(TAG, "[stream %d] playback started", event_ctx->stream_id);
            break;
        case ESP_PLAYER_EVENT_FINISHED:
            ESP_LOGI(TAG, "[stream %d] playback finished", event_ctx->stream_id);
            xEventGroupSetBits(event_ctx->evt, BIT(event_ctx->stream_id));
            break;
        case ESP_PLAYER_EVENT_ERROR:
            ESP_LOGE(TAG, "[stream %d] playback error (error_source=%d)",
                     event_ctx->stream_id, *(esp_player_error_source_t *)msg->data);
            xEventGroupSetBits(event_ctx->evt, EVT_ANY_ERROR);
            break;
        default:
            break;
    }
    return ESP_PLAYER_ERR_OK;
}

static void playback(void)
{
    esp_player_handle_t players[AUDIO_PLAYER_STREAM_NUM] = {0};
    player_evt_ctx_t evt_ctx[AUDIO_PLAYER_STREAM_NUM] = {0};
    EventGroupHandle_t evt = NULL;
    int players_created = 0;

    ESP_LOGI(TAG, "[ 2 ] Set up %d playback stream(s)", AUDIO_PLAYER_STREAM_NUM);
    audio_render_settings_t render_settings = AUDIO_RENDER_SETTINGS_DEFAULT();
    if (audio_player_setup(&render_settings) != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "audio_player_setup failed");
        goto out;
    }

    for (int i = 0; i < AUDIO_PLAYER_STREAM_NUM; i++) {
        if (audio_player_new(&players[i]) != ESP_PLAYER_ERR_OK) {
            ESP_LOGE(TAG, "audio_player_new stream %d failed", i);
            players_created = i;
            goto out;
        }
        players_created = i + 1;
    }

    evt = xEventGroupCreate();
    if (evt == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        goto out;
    }

    for (int i = 0; i < AUDIO_PLAYER_STREAM_NUM; i++) {
        evt_ctx[i].evt = evt;
        evt_ctx[i].stream_id = i;
        esp_player_set_event_cb(players[i], player_event_cb, &evt_ctx[i]);
    }

    for (int i = 0; i < AUDIO_PLAYER_STREAM_NUM; i++) {
        const char *url = audio_player_play_url(i);
        if (url == NULL) {
            ESP_LOGE(TAG, "No URL for stream %d", i);
            goto out;
        }
        ESP_LOGI(TAG, "[ 3 ] Play stream %d: %s", i, url);
        esp_player_data_src_t src = ESP_PLAYER_DATA_SRC(url, ESP_PLAYER_MASK_AUDIO);
        if (esp_player_set_data_src(players[i], &src) != ESP_PLAYER_ERR_OK
            || esp_player_run(players[i]) != ESP_PLAYER_ERR_OK) {
            ESP_LOGE(TAG, "stream %d start failed", i);
            goto out;
        }
        if (i + 1 < AUDIO_PLAYER_STREAM_NUM) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    ESP_LOGI(TAG, "[ 4 ] Wait until all streams finish");
    EventBits_t want = all_streams_finished_bits();
    EventBits_t bits = 0;
    while (1) {
        bits = xEventGroupWaitBits(evt, want | EVT_ANY_ERROR, pdFALSE, pdFALSE, portMAX_DELAY);
        if (bits & EVT_ANY_ERROR) {
            break;
        }
        if ((bits & want) == want) {
            break;
        }
    }
    if (bits & EVT_ANY_ERROR) {
        ESP_LOGE(TAG, "Audio playback failed");
    } else {
        ESP_LOGI(TAG, "Audio playback finished, total streams = %d", AUDIO_PLAYER_STREAM_NUM);
    }
    for (int i = 0; i < AUDIO_PLAYER_STREAM_NUM; i++) {
        esp_player_stop(players[i]);
    }

out:
    if (evt != NULL) {
        vEventGroupDelete(evt);
    }
    for (int i = players_created - 1; i >= 0; i--) {
        audio_player_delete(players[i]);
        players[i] = NULL;
    }
    audio_player_teardown();
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI(TAG, "[ 1 ] Mount SD card");
    if (mount_sdcard() != ESP_OK) {
        return;
    }

    playback();

    unmount_sdcard();

    ESP_LOGI(TAG, "audio_player example finished");
}
