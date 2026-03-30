/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_board_manager_includes.h"

static const char *TAG = "BMGR_RECORD_AND_PLAY";

#define DEFAULT_SAMPLE_RATE      16000
#define DEFAULT_CHANNELS         1
#define DEFAULT_BITS_PER_SAMPLE  16
#define DEFAULT_PLAY_VOL         60
#define DEFAULT_REC_GAIN         30
#define LOOPBACK_DURATION_SEC    10
#define LOOPBACK_BUFFER_SIZE     256

void app_main(void)
{
    ESP_LOGI(TAG, "Audio loopback: record from ADC and play through DAC for %d seconds", LOOPBACK_DURATION_SEC);
    esp_err_t ret = ESP_OK;
    dev_audio_codec_handles_t *adc_handle = NULL;
    dev_audio_codec_handles_t *dac_handle = NULL;
    uint8_t *loopback_buf = NULL;
    bool adc_inited = false;
    bool dac_inited = false;
    bool adc_opened = false;
    bool dac_opened = false;

    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio DAC device");
        goto cleanup;
    }
    dac_inited = true;

    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio ADC device");
        goto cleanup;
    }
    adc_inited = true;

    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&dac_handle);
    if (ret != ESP_OK || dac_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get DAC device handle");
        goto cleanup;
    }

    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&adc_handle);
    if (ret != ESP_OK || adc_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get ADC device handle");
        goto cleanup;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = DEFAULT_SAMPLE_RATE,
        .channel = DEFAULT_CHANNELS,
        .bits_per_sample = DEFAULT_BITS_PER_SAMPLE,
    };

    ret = esp_codec_dev_open(dac_handle->codec_dev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open DAC codec device");
        goto cleanup;
    }
    dac_opened = true;

    ret = esp_codec_dev_open(adc_handle->codec_dev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open ADC codec device");
        goto cleanup;
    }
    adc_opened = true;

    ret = esp_codec_dev_set_out_vol(dac_handle->codec_dev, DEFAULT_PLAY_VOL);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set DAC volume, continuing anyway");
    }

    ret = esp_codec_dev_set_in_gain(adc_handle->codec_dev, DEFAULT_REC_GAIN);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set ADC gain, continuing anyway");
    }

    loopback_buf = (uint8_t *)malloc(LOOPBACK_BUFFER_SIZE);
    if (loopback_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate loopback buffer");
        goto cleanup;
    }

    ESP_LOGI(TAG, "Audio loopback started: %" PRIu32 " Hz, %" PRIu16 " ch, %" PRIu16 " bit",
             fs.sample_rate, fs.channel, fs.bits_per_sample);

    vTaskDelay(pdMS_TO_TICKS(100));

    uint32_t loopback_duration_ms = LOOPBACK_DURATION_SEC * 1000;
    uint32_t start_time = esp_timer_get_time() / 1000;
    uint32_t total_bytes = 0;
    uint32_t last_log_sec = 0;

    while ((esp_timer_get_time() / 1000) - start_time < loopback_duration_ms) {
        ret = esp_codec_dev_read(adc_handle->codec_dev, loopback_buf, LOOPBACK_BUFFER_SIZE);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to read from ADC");
            break;
        }

        ret = esp_codec_dev_write(dac_handle->codec_dev, loopback_buf, LOOPBACK_BUFFER_SIZE);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to write to DAC");
            break;
        }

        total_bytes += LOOPBACK_BUFFER_SIZE;

        uint32_t elapsed_sec = ((esp_timer_get_time() / 1000) - start_time) / 1000;
        if (elapsed_sec > last_log_sec) {
            last_log_sec = elapsed_sec;
            ESP_LOGI(TAG, "Loopback running... %" PRIu32 "/%" PRIu32 "s, total %" PRIu32 " bytes",
                     elapsed_sec, (uint32_t)LOOPBACK_DURATION_SEC, total_bytes);
        }
    }

    ESP_LOGI(TAG, "Audio loopback completed. Total bytes transferred: %" PRIu32, total_bytes);

cleanup:
    if (loopback_buf) {
        free(loopback_buf);
    }

    if (dac_opened && dac_handle && dac_handle->codec_dev) {
        esp_codec_dev_close(dac_handle->codec_dev);
    }
    if (adc_opened && adc_handle && adc_handle->codec_dev) {
        esp_codec_dev_close(adc_handle->codec_dev);
    }

    if (adc_inited) {
        ret = esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deinitialize audio ADC device");
        }
    }
    if (dac_inited) {
        ret = esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deinitialize audio DAC device");
        }
    }
}
