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
#include "codec_init.h"
#include "codec_board.h"
#include "esp_gmf_app_setup_peripheral.h"

#ifndef CONFIG_AUDIO_BOARD
#define CONFIG_AUDIO_BOARD "NULL"
#endif  /* CONFIG_AUDIO_BOARD */

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
    set_codec_board_type(CONFIG_AUDIO_BOARD);
    mount_sdcard();
    if (sdcard_handle) {
        *sdcard_handle = get_sdcard_handle();
    }
}

void esp_gmf_app_teardown_sdcard(void *sdcard_handle)
{
    unmount_sdcard();
}

void esp_gmf_app_setup_codec_dev(esp_gmf_app_codec_info_t *codec_info)
{
    set_codec_board_type(CONFIG_AUDIO_BOARD);
    if (codec_info) {
        codec_init_cfg_t codec_cfg = {
            .in_mode = codec_info->record_info.mode,
            .out_mode = codec_info->play_info.mode,
            .in_use_tdm = false,
            .reuse_dev = false,
        };
        ESP_ERROR_CHECK(init_codec(&codec_cfg));
        setup_create_codec_dev(codec_info);
    } else {
        esp_gmf_app_codec_info_t _info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
        codec_init_cfg_t codec_cfg = {
            .in_mode = _info.record_info.mode,
            .out_mode = _info.play_info.mode,
            .in_use_tdm = false,
            .reuse_dev = false,
        };
        ESP_ERROR_CHECK(init_codec(&codec_cfg));
        setup_create_codec_dev(&_info);
    }
}

void *esp_gmf_app_get_i2c_handle(void)
{
    return get_i2c_bus_handle(0);
}

void *esp_gmf_app_get_playback_handle(void)
{
    return get_playback_handle();
}

void *esp_gmf_app_get_record_handle(void)
{
    return get_record_handle();
}

void esp_gmf_app_teardown_codec_dev(void)
{
    deinit_codec();
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
