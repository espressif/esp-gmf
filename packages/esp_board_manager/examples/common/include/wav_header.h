/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Read WAV file header and extract audio parameters
 *
 *         This function reads the first 44 bytes of a WAV file and validates the RIFF header.
 *         It extracts sample rate, number of channels, and bits per sample.
 *
 * @param[in]   fp               Pointer to the opened file stream (must be opened in binary read mode)
 * @param[out]  sample_rate      Pointer to store the sample rate in Hz
 * @param[out]  channels         Pointer to store the number of audio channels (1 for mono, 2 for stereo)
 * @param[out]  bits_per_sample  Pointer to store the bits per sample (e.g., 16, 24, 32)
 *
 * @return
 *       - ESP_OK    Successfully read and parsed the WAV header
 *       - ESP_FAIL  File read error or invalid WAV format
 */
esp_err_t read_wav_header(FILE *fp, uint32_t *sample_rate, uint16_t *channels, uint16_t *bits_per_sample);

/**
 * @brief  Write a standard 44‑byte WAV header to a file
 *
 *         This function writes a RIFF/WAVE header with the provided audio parameters.
 *         The file pointer should be positioned at the beginning of the file (or where the header should be written).
 *         The header includes a placeholder for the data size based on the given duration.
 *
 * @param[in]  fp                Pointer to the opened file stream (must be opened in binary write mode)
 * @param[in]  sample_rate       Sample rate in Hz
 * @param[in]  channels          Number of audio channels (1 for mono, 2 for stereo)
 * @param[in]  bits_per_sample   Bits per sample (e.g., 16, 24, 32)
 * @param[in]  duration_seconds  Duration of the audio data in seconds (used to calculate data size)
 *
 * @return
 *       - ESP_OK    Header written successfully
 *       - ESP_FAIL  File write error
 */
esp_err_t write_wav_header(FILE *fp, uint32_t sample_rate, uint16_t channels, uint16_t bits_per_sample, uint32_t duration_seconds);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
