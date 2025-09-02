/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_board_device.h"
#include "esp_board_periph.h"
#include "test_dev_audio_codec.h"

static const char *TAG = "TEST_CODEC_CFG";

esp_err_t initialize_devices(const device_config_t *dev_config)
{
    esp_err_t ret;
    if (dev_config->i2c_periph) {
        ret = esp_board_periph_init(dev_config->i2c_periph);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize I2C peripheral");
            return ret;
        }
    }
    if (dev_config->i2s_periph) {
        ret = esp_board_periph_init(dev_config->i2s_periph);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize I2S peripheral");
            return ret;
        }
    }
    if (dev_config->sdcard_device) {
        ret = esp_board_device_init(dev_config->sdcard_device);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize SD card");
            return ret;
        }
    }
    if (dev_config->codec_device) {
        ret = esp_board_device_init(dev_config->codec_device);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize codec device");
            return ret;
        }
    }
    return ESP_OK;
}

esp_err_t configure_codec(const char *codec_name, const audio_config_t *config, bool is_dac, dev_audio_codec_handles_t **codec_handles)
{
    esp_err_t ret = esp_board_device_get_handle(codec_name, (void **)codec_handles);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get %s handle", codec_name);
        return ret;
    }
    if ((*codec_handles)->codec_dev == NULL) {
        ESP_LOGE(TAG, "Failed to get %s handle", codec_name);
        return ESP_FAIL;
    }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = config->sample_rate,
        .channel = config->channels,
        .bits_per_sample = config->bits_per_sample,
    };
    ESP_LOGI(TAG, "%s sample rate: %" PRIu32 ", channel: %" PRIu8 ", bits: %" PRIu8,
             is_dac ? "DAC" : "ADC", fs.sample_rate, fs.channel, fs.bits_per_sample);
    ret = esp_codec_dev_open((*codec_handles)->codec_dev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open %s codec", codec_name);
        return ret;
    }
    if (is_dac) {
        ret = esp_codec_dev_set_out_vol((*codec_handles)->codec_dev, 60);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to set DAC volume");
        }
    } else {
        ret = esp_codec_dev_set_in_gain((*codec_handles)->codec_dev, 30);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to set ADC input gain");
        }
    }
    return ESP_OK;
}

esp_err_t cleanup_devices(const device_config_t *dev_config)
{
    esp_err_t ret = ESP_OK;
    // Cleanup codec device
    if (dev_config->codec_device) {
        esp_board_device_deinit(dev_config->codec_device);
    }
    if (dev_config->sdcard_device) {
        esp_board_device_deinit(dev_config->sdcard_device);
    }
    if (dev_config->i2c_periph) {
        esp_board_periph_deinit(dev_config->i2c_periph);
    }
    if (dev_config->i2s_periph) {
        esp_board_periph_deinit(dev_config->i2s_periph);
    }
    return ret;
}
