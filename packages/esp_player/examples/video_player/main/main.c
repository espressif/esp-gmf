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
#include "video_player_setup.h"

#define EVT_FINISHED  BIT(0)
#define EVT_ERROR     BIT(1)

static const char *TAG = "VIDEO_PLAYER";

typedef struct {
    EventGroupHandle_t  evt;
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

static esp_player_err_t player_event_cb(esp_player_event_msg_t *msg, void *ctx)
{
    player_evt_ctx_t *event_ctx = (player_evt_ctx_t *)ctx;
    if (event_ctx == NULL || event_ctx->evt == NULL) {
        return ESP_PLAYER_ERR_INVALID_ARG;
    }

    switch (msg->event_type) {
        case ESP_PLAYER_EVENT_PLAYED:
            ESP_LOGI(TAG, "Playback started");
            break;
        case ESP_PLAYER_EVENT_FINISHED:
            ESP_LOGI(TAG, "Playback finished");
            xEventGroupSetBits(event_ctx->evt, EVT_FINISHED);
            break;
        case ESP_PLAYER_EVENT_ERROR:
            ESP_LOGE(TAG, "Playback error (error_source=%d)", *(esp_player_error_source_t *)msg->data);
            xEventGroupSetBits(event_ctx->evt, EVT_ERROR);
            break;
        default:
            break;
    }
    return ESP_PLAYER_ERR_OK;
}

static void playback(void)
{
    esp_player_handle_t player = NULL;
    EventGroupHandle_t evt = NULL;
    player_evt_ctx_t event_ctx = {0};

    ESP_LOGI(TAG, "[ 2 ] Set up media stack, audio/video render, and ESP Player");
    video_render_settings_t render_settings = VIDEO_RENDER_SETTINGS_DEFAULT();
    if (video_player_setup(&render_settings) != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "video_player_setup failed");
        goto out;
    }
    if (video_player_new(&player) != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "video_player_new failed");
        goto out;
    }

    evt = xEventGroupCreate();
    if (evt == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        goto out;
    }
    event_ctx.evt = evt;
    esp_player_set_event_cb(player, player_event_cb, &event_ctx);

    ESP_LOGI(TAG, "[ 3 ] Play %s", VIDEO_PLAYER_PLAY_URL);
    esp_player_data_src_t src = ESP_PLAYER_DATA_SRC(VIDEO_PLAYER_PLAY_URL, ESP_PLAYER_MASK_AV);
    if (esp_player_set_data_src(player, &src) != ESP_PLAYER_ERR_OK
        || esp_player_run(player) != ESP_PLAYER_ERR_OK) {
        ESP_LOGE(TAG, "Playback start failed");
        goto out;
    }

    ESP_LOGI(TAG, "[ 4 ] Wait until playback finishes");
    EventBits_t bits = xEventGroupWaitBits(evt, EVT_FINISHED | EVT_ERROR,
                                           pdFALSE, pdFALSE, portMAX_DELAY);
    if (bits & EVT_ERROR) {
        ESP_LOGE(TAG, "Video playback failed");
    } else {
        ESP_LOGI(TAG, "Video playback finished");
    }
    esp_player_stop(player);

out:
    if (evt != NULL) {
        vEventGroupDelete(evt);
    }
    video_player_delete(player);
    player = NULL;
    video_player_teardown();
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

    ESP_LOGI(TAG, "video_player example finished");
}
