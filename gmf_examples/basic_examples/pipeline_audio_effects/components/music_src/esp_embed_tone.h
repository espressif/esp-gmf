/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

/**
 * @brief Structure for embedding tone information
 */
typedef struct {
    const uint8_t *address;  /*!< Pointer to the embedded tone data */
    int           size;      /*!< Size of the tone data in bytes */
} esp_embed_tone_t;

/**
 * @brief External reference to embedded tone data: manloud_48000_2_16_10.wav
 */
extern const uint8_t manloud_48000_2_16_10_wav[] asm("_binary_manloud_48000_2_16_10_wav_start");

/**
 * @brief External reference to embedded tone data: tone.mp3
 */
extern const uint8_t tone_mp3[] asm("_binary_tone_mp3_start");

/**
 * @brief Array of embedded tone information
 */
esp_embed_tone_t g_esp_embed_tone[] = {
    [0] = {
        .address = manloud_48000_2_16_10_wav,
        .size    = 1920044,
    },
    [1] = {
        .address = tone_mp3,
        .size    = 22651,
    },
};

/**
 * @brief Enumeration for tone URLs
 */
enum esp_embed_tone_index {
    ESP_EMBED_TONE_MANLOUD_48000_2_16_10_WAV = 0,
    ESP_EMBED_TONE_TONE_MP3 = 1,
    ESP_EMBED_TONE_URL_MAX = 2
};

/**
 * @brief Array of tone URLs
 */
const char * esp_embed_tone_url[] = {
    "embed://tone/0_manloud_48000_2_16_10.wav",
    "embed://tone/1_tone.mp3",
};
