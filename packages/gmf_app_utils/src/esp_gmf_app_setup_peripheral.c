/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "driver/sdmmc_host.h"
#include "vfs_fat_internal.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "protocol_examples_common.h"
#include "esp_board_manager_includes.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_codec_dev.h"

#define SETUP_AUDIO_FORCED_CHANNELS 2
#define DEFAULT_VOLUME              60.0
#define DEFAULT_IN_GAIN             30.0

static void setup_create_codec_dev(esp_gmf_app_codec_info_t *codec_info)
{
    void *playback_handle = esp_gmf_app_get_playback_handle();
    void *record_handle = esp_gmf_app_get_record_handle();
    if (playback_handle) {
        esp_codec_dev_set_out_vol(playback_handle, DEFAULT_VOLUME);
        esp_codec_dev_sample_info_t fs = {
            .sample_rate = codec_info->play_info.sample_rate,
            .channel = codec_info->play_info.channel,
            .bits_per_sample = codec_info->play_info.bits_per_sample,
        };
        esp_codec_dev_open(playback_handle, &fs);
    }
    if (record_handle) {
        esp_codec_dev_set_in_gain(record_handle, DEFAULT_IN_GAIN);
        esp_codec_dev_sample_info_t fs = {
            .sample_rate = codec_info->record_info.sample_rate,
            .channel = codec_info->record_info.channel,
            .bits_per_sample = codec_info->record_info.bits_per_sample,
        };
#ifdef CONFIG_ESP32_LYRAT_MINI_V1_BOARD
        if (fs.channel == 1) {
            fs.channel = SETUP_AUDIO_FORCED_CHANNELS;
            fs.channel_mask = SETUP_AUDIO_FORCED_CHANNELS;
        }
#endif  /* CONFIG_ESP32_LYRAT_MINI_V1_BOARD */
        esp_codec_dev_open(record_handle, &fs);
    }
}

void esp_gmf_app_setup_sdcard(void **sdcard_handle)
{
    ESP_ERROR_CHECK(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD));
    if (sdcard_handle) {
        esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_FS_SDCARD, sdcard_handle);
    }
}

void esp_gmf_app_teardown_sdcard(void *sdcard_handle)
{
    ESP_ERROR_CHECK(esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD));
}

void esp_gmf_app_setup_codec_dev(esp_gmf_app_codec_info_t *codec_info)
{
    ESP_ERROR_CHECK(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC));
    ESP_ERROR_CHECK(esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC));
    if (codec_info) {
        setup_create_codec_dev(codec_info);
    } else {
        esp_gmf_app_codec_info_t _info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
        setup_create_codec_dev(&_info);
    }
}

void *esp_gmf_app_get_i2c_handle(void)
{
    void *i2c_handle = NULL;
    esp_board_manager_get_periph_handle("i2c_master", &i2c_handle);
    return i2c_handle;
}

void *esp_gmf_app_get_playback_handle(void)
{
    void *playback_handle = NULL;
    dev_audio_codec_handles_t *device_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&device_handle);
    if (device_handle) {
        playback_handle = device_handle->codec_dev;
    }
    return playback_handle;
}

void *esp_gmf_app_get_record_handle(void)
{
    void *record_handle = NULL;
    dev_audio_codec_handles_t *device_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&device_handle);
    if (device_handle) {
        record_handle = device_handle->codec_dev;
    }
    return record_handle;
}

void esp_gmf_app_teardown_codec_dev(void)
{
    ESP_ERROR_CHECK(esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC));
    ESP_ERROR_CHECK(esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC));
}

void esp_gmf_app_wifi_connect(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());
}

void esp_gmf_app_wifi_disconnect(void)
{
    ESP_ERROR_CHECK(example_disconnect());
    ESP_ERROR_CHECK(esp_event_loop_delete_default());
    esp_netif_deinit();
}
