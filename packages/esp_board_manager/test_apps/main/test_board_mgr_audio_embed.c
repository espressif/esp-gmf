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
#include "esp_partition.h"

// Embedded audio file
extern const uint8_t test_wav_start[] asm("_binary_test_wav_start");
extern const uint8_t test_wav_end[] asm("_binary_test_wav_end");

static bool playback_finished = false;
static bool recording_finished = false;

static const char *TAG = "BMGR_AUDIO_EMBED";

// Partition-based recording variables
static const esp_partition_t *record_partition = NULL;
static size_t record_partition_offset = 0;
static size_t record_partition_used = 0;

// Store recording configuration for consistent playback
static audio_config_t recorded_audio_config = {0};

// Clean up recording resources
static void cleanup_recording_resources(void)
{
    if (record_partition) {
        // Erase the partition to clean up
        esp_partition_erase_range(record_partition, 0, record_partition->size);
        record_partition = NULL;
        record_partition_offset = 0;
        record_partition_used = 0;
    }
    memset(&recorded_audio_config, 0, sizeof(recorded_audio_config));
}

// Task for playing embedded WAV file
static void embedded_wav_playback_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting embedded WAV file playback...");

    // Calculate embedded file size， -1 make the size is correctly
    size_t embedded_file_size = test_wav_end - test_wav_start - 1;
    ESP_LOGI(TAG, "Embedded WAV file size: %d bytes", embedded_file_size);

    // Create a temporary file pointer to read the embedded data
    // We'll use a memory stream approach
    const uint8_t *current_pos = test_wav_start;

    // Read WAV header from embedded data
    if (embedded_file_size < 44) {
        ESP_LOGE(TAG, "Embedded file too small to contain WAV header");
        playback_finished = true;
        vTaskDelete(NULL);
        return;
    }

    // Parse WAV header manually
    uint32_t sample_rate = *(uint32_t *)(current_pos + 24);
    uint16_t channels = *(uint16_t *)(current_pos + 22);
    uint16_t bits_per_sample = *(uint16_t *)(current_pos + 34);

    ESP_LOGI(TAG, "WAV file info: %d Hz, %d channels, %d bits", sample_rate, channels, bits_per_sample);

    // Configure DAC
    audio_config_t dac_config = {
        .sample_rate = sample_rate,
        .channels = channels,
        .bits_per_sample = bits_per_sample,
        .duration_seconds = 10
    };

    dev_audio_codec_handles_t *dac_handles = NULL;
    esp_err_t ret = configure_codec("audio_dac", &dac_config, true, &dac_handles);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure DAC");
        playback_finished = true;
        vTaskDelete(NULL);
        return;
    }

    // Skip WAV header (44 bytes) and play audio data
    current_pos += 44;
    size_t audio_data_size = embedded_file_size - 44;

    const size_t buffer_size = 5 * 1024;
    uint8_t *playback_buffer = malloc(buffer_size);
    if (playback_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate playback buffer");
        playback_finished = true;
        vTaskDelete(NULL);
        return;
    }
    size_t remaining_data = audio_data_size;

    while (remaining_data > 0) {
        size_t bytes_to_write = (remaining_data > buffer_size) ? buffer_size : remaining_data;
        memcpy(playback_buffer, current_pos, bytes_to_write);

        ret = esp_codec_dev_write(dac_handles->codec_dev, playback_buffer, bytes_to_write);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to write to DAC");
            break;
        }

        current_pos += bytes_to_write;
        remaining_data -= bytes_to_write;
    }

    ESP_LOGI(TAG, "Embedded WAV file playback completed");
    free(playback_buffer);
    playback_finished = true;
    vTaskDelete(NULL);
}

// Task for recording audio to partition
static void partition_recording_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Starting partition-based recording...");

    // Find the record partition
    record_partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "record");
    if (!record_partition) {
        ESP_LOGE(TAG, "Failed to find record partition");
        recording_finished = true;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Found record partition: size=%d bytes", record_partition->size);

    // Erase the partition before recording
    esp_err_t ret = esp_partition_erase_range(record_partition, 0, record_partition->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase record partition");
        recording_finished = true;
        vTaskDelete(NULL);
        return;
    }

    record_partition_offset = 0;
    record_partition_used = 0;

    dev_audio_codec_config_t *adc_cfg = NULL;
    esp_board_manager_get_device_config("audio_adc", (void**)&adc_cfg);
    periph_i2s_config_t *i2s_cfg = NULL;
    esp_board_manager_get_periph_config(adc_cfg->i2s_cfg.name, (void**)&i2s_cfg);
    // Configure ADC for recording
    audio_config_t adc_config = {
        .sample_rate = i2s_cfg->i2s_cfg.std.clk_cfg.sample_rate_hz,
        .channels = i2s_cfg->i2s_cfg.std.slot_cfg.total_slot,
        .bits_per_sample = 16,
        .duration_seconds = 3  // Record for 3 seconds
    };

    // Save configuration for playback
    recorded_audio_config = adc_config;

    ESP_LOGI(TAG, "Recording config: %d Hz, %d channels, %d bits",
             adc_config.sample_rate, adc_config.channels, adc_config.bits_per_sample);

    dev_audio_codec_handles_t *adc_handles = NULL;
    ret = configure_codec("audio_adc", &adc_config, false, &adc_handles);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC");
        recording_finished = true;
        vTaskDelete(NULL);
        return;
    }

    const size_t buffer_size = 4096;
    uint8_t *recording_buffer = malloc(buffer_size);
    if (recording_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate recording buffer");
        recording_finished = true;
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Recording 3 seconds of audio to partition (48kHz, stereo)...");
    uint32_t record_duration_ms = adc_config.duration_seconds * 1000;
    uint32_t start_time = esp_timer_get_time() / 1000;
    record_partition_used = 0;

    while ((esp_timer_get_time() / 1000) - start_time < record_duration_ms) {
        ret = esp_codec_dev_read(adc_handles->codec_dev, recording_buffer, buffer_size);
        if (ret == ESP_CODEC_DEV_OK) {
            if (record_partition_offset + record_partition_used + buffer_size <= record_partition->size) {
                ret = esp_partition_write(record_partition, record_partition_offset + record_partition_used, recording_buffer, buffer_size);
                if (ret == ESP_OK) {
                    record_partition_used += buffer_size;
                } else {
                    ESP_LOGE(TAG, "Failed to write to partition");
                    break;
                }
            } else {
                ESP_LOGW(TAG, "Partition full, stopping recording");
                break;
            }
        } else {
            ESP_LOGE(TAG, "Failed to read audio data from ADC");
            break;
        }
    }

    ESP_LOGI(TAG, "Partition recording completed. Recorded %d bytes", record_partition_used);
    free(recording_buffer);
    recording_finished = true;
    vTaskDelete(NULL);
}

// Task for playing back recorded audio from memory
static void play_recorded_audio_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Playing back recorded audio from partition...");
    if (record_partition == NULL || record_partition_used == 0) {
        ESP_LOGE(TAG, "No recorded audio data available");
        vTaskDelete(NULL);
        return;
    }

    // Get the same I2S configuration used for recording to ensure consistency
    dev_audio_codec_config_t *dac_cfg = NULL;
    esp_board_manager_get_device_config("audio_dac", (void**)&dac_cfg);
    periph_i2s_config_t *i2s_cfg = NULL;
    esp_board_manager_get_periph_config(dac_cfg->i2s_cfg.name, (void**)&i2s_cfg);

    // Configure DAC for playback of recorded audio - use same config as recording
    audio_config_t dac_config = {
        .sample_rate = recorded_audio_config.sample_rate,
        .channels = recorded_audio_config.channels,
        .bits_per_sample = recorded_audio_config.bits_per_sample,
        .duration_seconds = recorded_audio_config.duration_seconds
    };

    dev_audio_codec_handles_t *dac_handles = NULL;
    esp_err_t ret = configure_codec("audio_dac", &dac_config, true, &dac_handles);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure DAC for recorded audio playback");
        vTaskDelete(NULL);
        return;
    }

    const size_t buffer_size = 4096;
    uint8_t *playback_buffer = malloc(buffer_size);
    if (playback_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate playback buffer");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Playing back %d bytes of recorded audio...", record_partition_used);
    size_t remaining_data = record_partition_used;
    size_t read_offset = 0;

    while (remaining_data > 0) {
        size_t bytes_to_read = (remaining_data > buffer_size) ? buffer_size : remaining_data;

        // Read data from partition
        ret = esp_partition_read(record_partition, record_partition_offset + read_offset, playback_buffer, bytes_to_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read from partition");
            break;
        }

        ret = esp_codec_dev_write(dac_handles->codec_dev, playback_buffer, bytes_to_read);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to write recorded audio to DAC");
            break;
        }

        read_offset += bytes_to_read;
        remaining_data -= bytes_to_read;
    }

    ESP_LOGI(TAG, "Recorded audio playback completed");
    free(playback_buffer);
    vTaskDelete(NULL);
}

void test_board_mgr_audio_playback_only(void)
{
    ESP_LOGI(TAG, "Starting audio playback only test (using embedded file)...");
    playback_finished = false;

    // Create playback task
    xTaskCreate(embedded_wav_playback_task, "embedded_wav_playback", 4096, NULL, 1, NULL);

    // Wait for playback to complete
    while (!playback_finished) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    ESP_LOGI(TAG, "Audio playback only test completed");
}

void test_board_mgr_audio_recording_after_playback(void)
{
    ESP_LOGI(TAG, "Starting recording after playback test (partition version)...");

    // Clean up any previous recording state
    cleanup_recording_resources();

    recording_finished = false;

    // First, play the embedded audio file
    ESP_LOGI(TAG, "Step 1: Playing embedded audio file...");
    test_board_mgr_audio_playback_only();

    // Wait a moment between playback and recording
    vTaskDelay(pdMS_TO_TICKS(500));

    // Then record audio to partition
    ESP_LOGI(TAG, "Step 2: Recording audio to partition...");
    xTaskCreate(partition_recording_task, "partition_recording", 4096, NULL, 1, NULL);

    // Wait for recording to complete
    while (!recording_finished) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // Wait a moment before playing back recorded audio
    vTaskDelay(pdMS_TO_TICKS(500));

    // Finally, play back the recorded audio
    ESP_LOGI(TAG, "Step 3: Playing back recorded audio...");
    xTaskCreate(play_recorded_audio_task, "play_recorded_audio", 4096, NULL, 1, NULL);

    // Wait for playback to complete
    vTaskDelay(pdMS_TO_TICKS(4000));  // Wait 4 seconds for playback (3s recording + 1s buffer)

    // Clean up
    cleanup_recording_resources();

    ESP_LOGI(TAG, "Recording after playback test completed");
}

// Main function that combines both steps
void test_board_mgr_audio_playback_and_record(void)
{
    ESP_LOGI(TAG, "Starting partition-based audio playback and record test...");
    test_board_mgr_audio_recording_after_playback();
}
