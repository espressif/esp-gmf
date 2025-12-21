#include <string.h>
#include "esp_log.h"
#include "esp_err.h"

static const char *TAG = "WAV_HEADER";

#pragma pack(push, 1)
typedef struct {
    char      riff[4];
    uint32_t  file_size;
    char      wave[4];
    char      fmt[4];
    uint32_t  fmt_size;
    uint16_t  audio_format;
    uint16_t  num_channels;
    uint32_t  sample_rate;
    uint32_t  byte_rate;
    uint16_t  block_align;
    uint16_t  bits_per_sample;
    char      data[4];
    uint32_t  data_size;
} wav_header_t;
#pragma pack(pop)

esp_err_t read_wav_header(FILE *fp, uint32_t *sample_rate, uint16_t *channels, uint16_t *bits_per_sample)
{
    wav_header_t header;

    if (fread(&header, sizeof(wav_header_t), 1, fp) != 1) {
        ESP_LOGE(TAG, "Failed to read WAV header");
        return ESP_FAIL;
    }

    if (memcmp(header.riff, "RIFF", 4) != 0 ||
        memcmp(header.wave, "WAVE", 4) != 0 ||
        memcmp(header.fmt, "fmt ", 4) != 0 ||
        memcmp(header.data, "data", 4) != 0) {
        ESP_LOGE(TAG, "Invalid WAV file format");
        return ESP_FAIL;
    }

    *sample_rate = header.sample_rate;
    *channels = header.num_channels;
    *bits_per_sample = header.bits_per_sample;

    ESP_LOGI(TAG, "WAV file: %" PRIu32 " Hz, %" PRIu16 " channels, %" PRIu16 " bits", *sample_rate, *channels, *bits_per_sample);
    return ESP_OK;
}

esp_err_t write_wav_header(FILE *fp, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, uint32_t duration_seconds)
{
    wav_header_t header = {0};

    memcpy(header.riff, "RIFF", 4);
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    memcpy(header.data, "data", 4);

    uint32_t data_size = sample_rate * channels * (bits_per_sample / 8) * duration_seconds;
    uint32_t file_size = data_size + sizeof(wav_header_t) - 8;

    header.file_size = file_size;
    header.fmt_size = 16;
    header.audio_format = 1;
    header.num_channels = channels;
    header.sample_rate = sample_rate;
    header.byte_rate = sample_rate * channels * bits_per_sample / 8;
    header.block_align = channels * bits_per_sample / 8;
    header.bits_per_sample = bits_per_sample;
    header.data_size = data_size;

    if (fwrite(&header, sizeof(wav_header_t), 1, fp) != 1) {
        ESP_LOGE(TAG, "Failed to write WAV header");
        return ESP_FAIL;
    }
    return ESP_OK;
}
