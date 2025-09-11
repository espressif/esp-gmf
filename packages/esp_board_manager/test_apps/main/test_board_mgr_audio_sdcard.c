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
#include "test_dev_audio_codec.h"
#include "esp_board_manager.h"
#include "dev_audio_codec.h"
#include "periph_i2s.h"

static bool playback_finished = false;
static bool recording_finished = false;

static const char *TAG = "BMGR_AUDIO_SDCARD";

// Task for reading WAV file and playing (SD card version)
static void wav_playback_task(void *pvParameters)
{
    const char *wav_file_path = (const char *)pvParameters;
    FILE *fp = fopen(wav_file_path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open WAV file for playback: %s", wav_file_path);
        goto cleanup_playback;
    }

    // Read WAV header
    uint32_t sample_rate;
    uint16_t channels, bits_per_sample;
    esp_err_t ret = read_wav_header(fp, &sample_rate, &channels, &bits_per_sample);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read WAV header");
        goto cleanup_playback;
    }

    ESP_LOGI(TAG, "WAV file info: %" PRIu32 " Hz, %" PRIu16 " channels, %" PRIu16 " bits", sample_rate, channels, bits_per_sample);

    // Configure DAC
    audio_config_t dac_config = {
        .sample_rate = sample_rate,
        .bits_per_sample = bits_per_sample,
        .duration_seconds = 10
    };

    dev_audio_codec_config_t *dac_cfg = NULL;
    ret = esp_board_manager_get_device_config("audio_dac", (void**)&dac_cfg);
    if (ret != ESP_OK || dac_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get audio_dac device config");
        goto cleanup_playback;
    }

    periph_i2s_config_t *i2s_tx_cfg = NULL;
    ret = esp_board_manager_get_periph_config(dac_cfg->i2s_cfg.name, (void**)&i2s_tx_cfg);
    if (ret != ESP_OK || i2s_tx_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get I2S TX config for %s", dac_cfg->i2s_cfg.name);
        goto cleanup_playback;
    }

    if (i2s_tx_cfg->mode == I2S_COMM_MODE_TDM) {
        dac_config.channels = i2s_tx_cfg->i2s_cfg.tdm.slot_cfg.total_slot;
    } else {
        // When mode is I2S_STD/PDM, there is no total_slot, so use slot_mode instead
        dac_config.channels = i2s_tx_cfg->i2s_cfg.std.slot_cfg.slot_mode == I2S_SLOT_MODE_STEREO ? 2 : 1;
    }
    dev_audio_codec_handles_t *dac_handles = NULL;
    ret = configure_codec("audio_dac", &dac_config, true, &dac_handles);
    if (ret != ESP_OK) {
        goto cleanup_playback;
    }

    const size_t buffer_size = 5 * 1024;
    uint8_t *playback_buffer = malloc(buffer_size);
    if (playback_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate playback buffer");
        goto cleanup_playback;
    }
    size_t bytes_read;
    while ((bytes_read = fread(playback_buffer, 1, buffer_size, fp)) > 0) {
        ret = esp_codec_dev_write(dac_handles->codec_dev, playback_buffer, bytes_read);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to write to DAC");
            break;
        }
    }

    ESP_LOGI(TAG, "WAV file playback completed");
    free(playback_buffer);
    fclose(fp);
    playback_finished = true;
    vTaskDelete(NULL);
    return;

cleanup_playback:
    if (fp) {
        fclose(fp);
    }
    playback_finished = true;
    vTaskDelete(NULL);
}

// Task for reading I2S data and saving (SD card version)
static void i2s_recording_task(void *pvParameters)
{
    const char *output_file_path = (const char *)pvParameters;
    FILE *fp = fopen(output_file_path, "wb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "Failed to open file for recording: %s", output_file_path);
        goto cleanup_recording;
    }

    dev_audio_codec_config_t *adc_cfg = NULL;
    esp_err_t ret = esp_board_manager_get_device_config("audio_adc", (void**)&adc_cfg);
    if (ret != ESP_OK || adc_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get audio_adc device config");
        goto cleanup_recording;
    }

    periph_i2s_config_t *i2s_rx_cfg = NULL;
    ret = esp_board_manager_get_periph_config(adc_cfg->i2s_cfg.name, (void**)&i2s_rx_cfg);
    if (ret != ESP_OK || i2s_rx_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get I2S RX config for %s", adc_cfg->i2s_cfg.name);
        goto cleanup_recording;
    }

    // Configure ADC
    audio_config_t adc_config = {
        .sample_rate = i2s_rx_cfg->i2s_cfg.std.clk_cfg.sample_rate_hz,
        .bits_per_sample = 16,
        .duration_seconds = 10
    };
    if (i2s_rx_cfg->mode == I2S_COMM_MODE_TDM) {
        adc_config.channels = i2s_rx_cfg->i2s_cfg.tdm.slot_cfg.total_slot;
    } else {
        // When mode is I2S_STD/PDM, there is no total_slot, so use slot_mode instead
        adc_config.channels = i2s_rx_cfg->i2s_cfg.std.slot_cfg.slot_mode == I2S_SLOT_MODE_STEREO ? 2 : 1;
    }
    dev_audio_codec_handles_t *adc_handles = NULL;
    ret = configure_codec("audio_adc", &adc_config, false, &adc_handles);
    if (ret != ESP_OK) {
        goto cleanup_recording;
    }

    // Write WAV header
    ret = write_wav_header(fp, adc_config.sample_rate, adc_config.channels, adc_config.bits_per_sample, adc_config.duration_seconds);
    if (ret != ESP_OK) {
        goto cleanup_recording;
    }

    const size_t buffer_size = 4096;
    uint8_t *recording_buffer = malloc(buffer_size);
    if (recording_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate recording buffer");
        goto cleanup_recording;
    }

    ESP_LOGI(TAG, "Starting I2S recording...");
    uint32_t total_bytes = 0;
    uint32_t record_duration_ms = adc_config.duration_seconds * 1000;
    uint32_t start_time = esp_timer_get_time() / 1000;

    while ((esp_timer_get_time() / 1000) - start_time < record_duration_ms) {
        ret = esp_codec_dev_read(adc_handles->codec_dev, recording_buffer, buffer_size);
        if (ret == ESP_CODEC_DEV_OK) {
            size_t bytes_written = fwrite(recording_buffer, 1, buffer_size, fp);
            if (bytes_written != buffer_size) {
                ESP_LOGE(TAG, "Failed to write audio data to file");
                break;
            }
            total_bytes += bytes_written;
        } else {
            ESP_LOGE(TAG, "Failed to read audio data from ADC");
            break;
        }
    }

    ESP_LOGI(TAG, "I2S recording completed. Total bytes recorded: %" PRIu32, total_bytes);
    free(recording_buffer);
    fclose(fp);
    recording_finished = true;
    vTaskDelete(NULL);
    return;

cleanup_recording:
    if (fp) {
        fclose(fp);
    }
    recording_finished = true;
    vTaskDelete(NULL);
}

void test_board_mgr_audio_playback_and_record(void)
{
    ESP_LOGI(TAG, "Starting audio playback and record (SD card version)...");
    playback_finished = false;
    recording_finished = false;

    // File paths
    const char *wav_file_path = "/sdcard/test.wav";
    const char *output_file_path = "/sdcard/recording_loopback.wav";

    // Step 1: Play the original WAV file
    ESP_LOGI(TAG, ">>>>> Step 1: Playing original WAV file and recording ...");
    xTaskCreate(wav_playback_task, "wav_playback", 4096, (void *)wav_file_path, 1, NULL);
    xTaskCreate(i2s_recording_task, "i2s_recording", 4096, (void *)output_file_path, 1, NULL);

    // Wait for both tasks to complete
    while (!playback_finished || !recording_finished) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Wait a moment before playing back recorded audio
    vTaskDelay(pdMS_TO_TICKS(500));

    // Step 2: Play back the recorded audio file
    ESP_LOGI(TAG, ">>>>> Step 2: Playing back recorded audio file...");
    playback_finished = false;
    xTaskCreate(wav_playback_task, "wav_playback_recorded", 4096, (void *)output_file_path, 1, NULL);

    // Wait for playback to complete
    while (!playback_finished) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Audio playback and record completed, Playback from: %s, Recording saved to: %s\r\n", wav_file_path, output_file_path);
}
