/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_board_manager_includes.h"

static const char *TAG = "BMGR_EMBED_MUSIC";

#define DEFAULT_PLAY_VOL 60

// Embedded audio file
extern const uint8_t test_wav_start[] asm("_binary_test_wav_start");
extern const uint8_t test_wav_end[] asm("_binary_test_wav_end");

void app_main(void)
{
    ESP_LOGI(TAG, "Playing embedded music");
    esp_err_t ret = ESP_OK;
    dev_audio_codec_handles_t *dac_handle = NULL;
    const size_t buffer_size = 5 * 1024;

    // Initialize audio DAC device
    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize audio DAC device");
        return;
    }

    // Get DAC device handle
    ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&dac_handle);
    if (ret != ESP_OK || dac_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get DAC device handle");
        return;
    }

    // Calculate embedded file size
    size_t embedded_file_size = test_wav_end - test_wav_start;
    ESP_LOGI(TAG, "Embedded WAV file size: %zu bytes", embedded_file_size);

    // Create a temporary file pointer to read the embedded data
    // We'll use a memory stream approach
    uint8_t *current_pos = (uint8_t *)test_wav_start;
    // Read WAV header from embedded data
    if (embedded_file_size < 44) {
        ESP_LOGE(TAG, "Embedded file too small to contain WAV header");
        return;
    }

    // Parse WAV header manually
    uint32_t sample_rate = *(uint32_t *)(current_pos + 24);
    uint16_t channels = *(uint16_t *)(current_pos + 22);
    uint16_t bits_per_sample = *(uint16_t *)(current_pos + 34);
    ESP_LOGI(TAG, "WAV file info: %" PRIu32 " Hz, %d channels, %d bits", sample_rate, channels, bits_per_sample);

    // Configure DAC
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = sample_rate,
        .channel = channels,
        .bits_per_sample = bits_per_sample,
    };
    ret = esp_codec_dev_open(dac_handle->codec_dev, &fs);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "Failed to open codec device");
        return;
    }

    // Set output volume
    ret = esp_codec_dev_set_out_vol(dac_handle->codec_dev, DEFAULT_PLAY_VOL);
    if (ret != ESP_CODEC_DEV_OK) {
        ESP_LOGW(TAG, "Failed to set DAC volume");
        // Continue playback anyway
    }

    // Skip WAV header (44 bytes) and play audio data
    current_pos += 44;
    size_t audio_data_size = embedded_file_size - 44;
    size_t remaining_data = audio_data_size;
    while (remaining_data > 0) {
        size_t bytes_to_write = (remaining_data > buffer_size) ? buffer_size : remaining_data;
        ret = esp_codec_dev_write(dac_handle->codec_dev, current_pos, bytes_to_write);
        if (ret != ESP_CODEC_DEV_OK) {
            ESP_LOGE(TAG, "Failed to write to DAC");
            break;
        }
        current_pos += bytes_to_write;
        remaining_data -= bytes_to_write;
    }

    ESP_LOGI(TAG, "Embedded WAV file playback completed");
    // Deinitialize audio DAC device
    ret = esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize audio DAC device");
    }
    return;
}
