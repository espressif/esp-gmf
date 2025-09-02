#include <stdio.h>
#include <math.h>
#include <string.h>
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "WAV_HEADER";

// Common utility functions
esp_err_t read_wav_header(FILE *fp, uint32_t *sample_rate, uint16_t *channels, uint16_t *bits_per_sample)
{
    uint8_t header[44];
    if (fread(header, 1, 44, fp) != 44) {
        ESP_LOGE(TAG, "Failed to read WAV header");
        return ESP_FAIL;
    }

    // Verify RIFF header
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "Invalid WAV file format");
        return ESP_FAIL;
    }

    // Extract audio parameters
    *sample_rate = *(uint32_t *)(header + 24);
    *channels = *(uint16_t *)(header + 22);
    *bits_per_sample = *(uint16_t *)(header + 34);

    ESP_LOGI(TAG, "WAV file: %" PRIu32 " Hz, %" PRIu16 " channels, %" PRIu16 " bits", *sample_rate, *channels, *bits_per_sample);
    return ESP_OK;
}

esp_err_t write_wav_header(FILE *fp, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, uint32_t duration_seconds)
{
    uint32_t data_size = sample_rate * channels * (bits_per_sample / 8) * duration_seconds;
    uint32_t file_size = 44 + data_size;

    uint8_t wav_header[44] = {
        'R',
        'I',
        'F',
        'F',
        (uint8_t)(file_size & 0xFF),
        (uint8_t)((file_size >> 8) & 0xFF),
        (uint8_t)((file_size >> 16) & 0xFF),
        (uint8_t)((file_size >> 24) & 0xFF),
        'W',
        'A',
        'V',
        'E',
        'f',
        'm',
        't',
        ' ',
        16,
        0,
        0,
        0,
        1,
        0,
        (uint8_t)(channels & 0xFF),
        (uint8_t)((channels >> 8) & 0xFF),
        (uint8_t)(sample_rate & 0xFF),
        (uint8_t)((sample_rate >> 8) & 0xFF),
        (uint8_t)((sample_rate >> 16) & 0xFF),
        (uint8_t)((sample_rate >> 24) & 0xFF),
        (uint8_t)((sample_rate * channels * bits_per_sample / 8) & 0xFF),
        (uint8_t)(((sample_rate * channels * bits_per_sample / 8) >> 8) & 0xFF),
        (uint8_t)(((sample_rate * channels * bits_per_sample / 8) >> 16) & 0xFF),
        (uint8_t)(((sample_rate * channels * bits_per_sample / 8) >> 24) & 0xFF),
        (uint8_t)((channels * bits_per_sample / 8) & 0xFF),
        (uint8_t)(((channels * bits_per_sample / 8) >> 8) & 0xFF),
        (uint8_t)(bits_per_sample & 0xFF),
        (uint8_t)((bits_per_sample >> 8) & 0xFF),
        'd',
        'a',
        't',
        'a',
        (uint8_t)(data_size & 0xFF),
        (uint8_t)((data_size >> 8) & 0xFF),
        (uint8_t)((data_size >> 16) & 0xFF),
        (uint8_t)((data_size >> 24) & 0xFF),
    };

    if (fwrite(wav_header, 1, 44, fp) != 44) {
        ESP_LOGE(TAG, "Failed to write WAV header");
        return ESP_FAIL;
    }
    return ESP_OK;
}
