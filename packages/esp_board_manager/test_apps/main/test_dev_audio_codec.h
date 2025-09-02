#pragma once

#include "dev_audio_codec.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define PLAYBACK_FILE_PATH "/sdcard/test.wav"
#define RECORD_FILE_PATH   "/sdcard/audio_adc_record.wav"

// Audio configuration structure
typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t duration_seconds;
} audio_config_t;

typedef struct {
    const char *i2c_periph;
    const char *i2s_periph;
    const char *sdcard_device;
    const char *codec_device;
} device_config_t;

esp_err_t initialize_devices(const device_config_t *dev_config);

esp_err_t configure_codec(const char *codec_name, const audio_config_t *config, bool is_dac, dev_audio_codec_handles_t **codec_handles);

esp_err_t cleanup_devices(const device_config_t *dev_config);

esp_err_t read_wav_header(FILE *fp, uint32_t *sample_rate, uint16_t *channels, uint16_t *bits_per_sample);

esp_err_t write_wav_header(FILE *fp, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, uint32_t duration_seconds);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
