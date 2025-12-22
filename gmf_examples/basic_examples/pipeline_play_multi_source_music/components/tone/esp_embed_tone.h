/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#pragma once

/**
 * @brief  Structure for embedding tone information
 */
typedef struct {
    const uint8_t *address;  /*!< Pointer to the embedded tone data */
    int            size;     /*!< Size of the tone data in bytes */
} esp_embed_tone_t;

/**
 * @brief  External reference to embedded tone data: alarm.mp3
 */
extern const uint8_t alarm_mp3[] asm("_binary_alarm_mp3_start");

/**
 * @brief  External reference to embedded tone data: dingdong.mp3
 */
extern const uint8_t dingdong_mp3[] asm("_binary_dingdong_mp3_start");

/**
 * @brief  External reference to embedded tone data: haode.mp3
 */
extern const uint8_t haode_mp3[] asm("_binary_haode_mp3_start");

/**
 * @brief  External reference to embedded tone data: new_message.mp3
 */
extern const uint8_t new_message_mp3[] asm("_binary_new_message_mp3_start");

/**
 * @brief  Array of embedded tone information
 */
esp_embed_tone_t g_esp_embed_tone[] = {
    [0] = {
        .address = alarm_mp3,
        .size    = 36018,
    },
    [1] = {
        .address = dingdong_mp3,
        .size    = 8527,
    },
    [2] = {
        .address = haode_mp3,
        .size    = 6384,
    },
    [3] = {
        .address = new_message_mp3,
        .size    = 22284,
    },
};

/**
 * @brief  Enumeration for tone URLs
 */
enum esp_embed_tone_index {
    ESP_EMBED_TONE_ALARM_MP3       = 0,
    ESP_EMBED_TONE_DINGDONG_MP3    = 1,
    ESP_EMBED_TONE_HAODE_MP3       = 2,
    ESP_EMBED_TONE_NEW_MESSAGE_MP3 = 3,
    ESP_EMBED_TONE_URL_MAX         = 4
};

/**
 * @brief  Array of tone URLs
 */
const char *esp_embed_tone_url[] = {
    "embed://tone/0_alarm.mp3",
    "embed://tone/1_dingdong.mp3",
    "embed://tone/2_haode.mp3",
    "embed://tone/3_new_message.mp3",
};
