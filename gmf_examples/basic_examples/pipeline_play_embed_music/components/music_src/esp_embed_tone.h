/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

/**
 * @brief  Structure for embedding tone information
 */
typedef struct {
    const uint8_t *address; /**< Pointer to the embedded tone data */
    int            size;    /**< Size of the tone data in bytes */
} esp_embed_tone_t;

/**
 * @brief  External reference to embedded tone data
 */
extern const uint8_t alarm_mp3[] asm("_binary_alarm_mp3_start");

/**
 * @brief  External reference to embedded tone data
 */
extern const uint8_t ff_16b_1c_44100hz_mp3[] asm("_binary_ff_16b_1c_44100hz_mp3_start");

/**
 * @brief  Array of embedded tone information, use in `esp_gmf_io_embed_flash_set_context`
 */
esp_embed_tone_t g_esp_embed_tone[] = {
    [0] = {
        .address = alarm_mp3,             /**< Tone data address */
        .size    = 36018,                 /**< Tone data size */
    },
    [1] = {
        .address = ff_16b_1c_44100hz_mp3, /**< Tone data address */
        .size    = 231725,                /**< Tone data size */
    },
};

/**
 * @brief  Enumeration for tone URLs
 */
enum esp_embed_tone_index {
    ESP_EMBED_TONE_ALARM_MP3             = 0,
    ESP_EMBED_TONE_FF_16B_1C_44100HZ_MP3 = 1,
    ESP_EMBED_TONE_URL_MAX               = 2
};

/**
 * @brief  Array of tone URLs
 */
const char *esp_embed_tone_url[] = {
    "embed://tone/0_alarm.mp3",
    "embed://tone/1_ff_16b_1c_44100hz.mp3",
};
