/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#include "esp_gmf_app_setup_peripheral.h"
#include "esp_board_manager_includes.h"
#include "esp_gmf_app_cli.h"

#include "audio_player/audio_multi_source_player.h"
#include "command_interface/audio_commands.h"
#include "common/audio_config.h"

static const char *TAG = "MULTI_SOURCE_PLAYER";

static esp_err_t setup_peripheral_devices(void **playback_handle)
{
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Initializing SD card");
    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SD card: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "Initializing audio codec");
    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init audio DAC: %d", ret);
        return ret;
    }

    dev_audio_codec_handles_t *dac_dev_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&dac_dev_handle);
    if (dac_dev_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get DAC device handle");
        return ESP_ERR_NOT_FOUND;
    }

    esp_codec_dev_handle_t codec_handle = dac_dev_handle->codec_dev;
    if (codec_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get codec handle");
        return ESP_ERR_NOT_FOUND;
    }

    ret = esp_codec_dev_set_out_vol(codec_handle, PLAYBACK_DEFAULT_VOLUME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set output volume: %d", ret);
        return ret;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = CONFIG_GMF_AUDIO_EFFECT_RATE_CVT_DEST_RATE,
        .channel = CONFIG_GMF_AUDIO_EFFECT_CH_CVT_DEST_CH,
        .bits_per_sample = CONFIG_GMF_AUDIO_EFFECT_BIT_CVT_DEST_BITS,
    };
    ret = esp_codec_dev_open(codec_handle, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open playback codec: %d", ret);
        return ret;
    }

    *playback_handle = codec_handle;
    ESP_LOGI(TAG, "Peripheral devices initialized successfully");
    return ESP_OK;
}

static void print_commands_help(void)
{
    ESP_LOGI(TAG, "=== Multi-Source Audio Player Ready ===");
    ESP_LOGI(TAG, "Available commands:");
    ESP_LOGI(TAG, "  play      - Start playback");
    ESP_LOGI(TAG, "  pause     - Pause playback");
    ESP_LOGI(TAG, "  resume    - Resume playback");
    ESP_LOGI(TAG, "  stop      - Stop playback");
    ESP_LOGI(TAG, "  switch    - Switch audio source (http or sdcard, or use without arg to toggle)");
    ESP_LOGI(TAG, "  tone      - Play flash tone (pauses current, plays tone, then resumes)");
    ESP_LOGI(TAG, "  get_vol   - Get current volume (0-100)");
    ESP_LOGI(TAG, "  set_vol   - Set volume (0-100)");
    ESP_LOGI(TAG, "  status    - Show playback status");
    ESP_LOGI(TAG, "  exit      - Exit the application");
    ESP_LOGI(TAG, "  help      - Show all available commands");
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_TASK", ESP_LOG_WARN);
    esp_log_level_set("ESP_GMF_BLOCK", ESP_LOG_WARN);
    esp_log_level_set("BOARD_DEVICE", ESP_LOG_WARN);

    ESP_LOGI(TAG, "=== Multi-Source Audio Player ===");

    esp_err_t ret = ESP_OK;
    void *playback_handle = NULL;

    ret = setup_peripheral_devices(&playback_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to setup peripheral devices");
        return;
    }

    esp_gmf_app_wifi_connect();

    ESP_LOGI(TAG, "Initializing audio manager");
    audio_ms_player_config_t init_config = {
        .playback_handle = playback_handle,
    };
    audio_ms_player_err_t audio_ret = audio_multi_source_player_init(&init_config);
    if (audio_ret != AUDIO_MS_PLAYER_OK) {
        ESP_LOGE(TAG, "Failed to initialize multi-source player: %d", audio_ret);
        goto cleanup_peripheral;
    }

    ESP_LOGI(TAG, "Initializing command interface");
    ret = audio_commands_init();
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize command interface: %d", ret);
        goto cleanup_player;
    }

    ESP_LOGI(TAG, "Setting up CLI");
    ret = esp_gmf_app_cli_init("Audio> ", audio_commands_register_all);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize CLI: %d", ret);
        goto cleanup_commands;
    }

    ESP_LOGI(TAG, "Starting initial playback from SD card");
    audio_ret = audio_multi_source_player_switch_source(AUDIO_SRC_SDCARD);
    if (audio_ret != AUDIO_MS_PLAYER_OK) {
        ESP_LOGW(TAG, "Failed to start initial playback: %d", audio_ret);
    }

    print_commands_help();

    ESP_LOGI(TAG, "Entering main application loop");

    while (audio_commands_keep_running()) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG, "Application shutting down");

cleanup_commands:
    audio_commands_deinit();

cleanup_player:
    audio_multi_source_player_deinit();

cleanup_peripheral:
    esp_gmf_app_wifi_disconnect();
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);

    ESP_LOGI(TAG, "Application cleanup completed");
}
