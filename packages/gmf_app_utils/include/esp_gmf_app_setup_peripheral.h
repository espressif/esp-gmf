/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "stdint.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  I2S mode
 */
typedef enum {
    ESP_GMF_APP_I2S_MODE_STD = 0,  /*!< Standard I2S mode */
    ESP_GMF_APP_I2S_MODE_TDM,      /*!< TDM mode */
    ESP_GMF_APP_I2S_MODE_PDM,      /*!< PDM mode */
    ESP_GMF_APP_I2S_MODE_NONE,     /*!< None mode */
} esp_gmf_app_i2s_mode_t;

/**
 * @brief  I2S driver information structure
 */
typedef struct {
    uint32_t                sample_rate;      /*!< The audio sample rate */
    uint8_t                 channel;          /*!< The audio channel number */
    uint8_t                 bits_per_sample;  /*!< The audio bits per sample */
    esp_gmf_app_i2s_mode_t  mode;             /*!< The I2S mode */
} esp_gmf_app_i2s_cfg_t;

/**
 * @brief  Default audio information settings
 */
#define ESP_GMF_APP_I2S_INFO_DEFAULT() {          \
    .sample_rate     = 48000,                     \
    .channel         = 2,                         \
    .bits_per_sample = 16,                        \
    .mode            = ESP_GMF_APP_I2S_MODE_STD,  \
}

/**
 * @brief  Codec information structure
 */
typedef struct {
    void                  *i2c_handle;   /*!< User-initialized I2C handle; otherwise, set to NULL to let the `esp_gmf_app_setup_codec_dev` initialize it */
    esp_gmf_app_i2s_cfg_t  play_info;    /*!< Audio information for playback. Currently,
                                              only ESP_GMF_APP_I2S_MODE_STD I2S mode is supported */
    esp_gmf_app_i2s_cfg_t  record_info;  /*!< Audio information for recording */
} esp_gmf_app_codec_info_t;

/**
 * @brief  Default codec information settings
 */
#define ESP_GMF_APP_CODEC_INFO_DEFAULT() {          \
    .i2c_handle  = NULL,                            \
    .play_info   = ESP_GMF_APP_I2S_INFO_DEFAULT(),  \
    .record_info = ESP_GMF_APP_I2S_INFO_DEFAULT(),  \
}

/**
 * @brief  Setup SD card
 *
 * @note  If SD card handle can be NULL
 *
 * @param[out]  sdcard_handle  The SD card handle
 */
void esp_gmf_app_setup_sdcard(void **sdcard_handle);

/**
 * @brief  Teardown SD card
 *
 * @param[in]  sdcard_handle  The SD card handle
 */
void esp_gmf_app_teardown_sdcard(void *sdcard_handle);

/**
 * @brief  Setup codec with configuration
 *
 * @note  It will initialize I2S and I2C. And open I2S
 *        If codec_info is NULL, it will use default codec configuration(ESP_GMF_APP_CODEC_INFO_DEFAULT())
 *
 * @param[in]  codec_info  The codec information
 */
void esp_gmf_app_setup_codec_dev(esp_gmf_app_codec_info_t *codec_info);

/**
 * @brief  Get the I2C handle after `esp_gmf_app_setup_codec_dev`
 *
 * @note  If user want to use the I2C handle after `esp_gmf_app_setup_codec_dev`, user can call this function
 *
 * @return
 *       - The I2C handle
 */
void *esp_gmf_app_get_i2c_handle(void);

/**
 * @brief  Get the playback handle after `esp_gmf_app_setup_codec_dev`
 *
 * @return
 *       - The playback handle
 */
void *esp_gmf_app_get_playback_handle(void);

/**
 * @brief  Get the record handle after `esp_gmf_app_setup_codec_dev`
 *
 * @return
 *       - The record handle
 */
void *esp_gmf_app_get_record_handle(void);

/**
 * @brief  Teardown codec
 *
 * @note  This function will teardown the codec configuration. And if I2C handle is from user, the I2C handle will not be teardown
 */
void esp_gmf_app_teardown_codec_dev(void);

/**
 * @brief  Connects to Wi-Fi using the settings configured in the Example Connection Configuration section of menuconfig
 *         This function first initializes NVS flash, `esp_netif_init`, and `esp_event_loop_create_default`,
 *         then calls `example_connect` to attempt to connect to the configured access point
 *
 * @note  To monitor Wi-Fi events (e.g., GOT_IP, DISCONNECT), users can register their own event callback using
 *        esp_event_handler_instance_register, which should be called after esp_event_loop_create_default. See the example below:
 *
 * @code{c}
 *           // Register for IP_EVENT_STA_GOT_IP event
 *           esp_err_t ret = esp_event_handler_instance_register(IP_EVENT,
 *           IP_EVENT_STA_GOT_IP,
 *           user_event_handler,
 *           NULL,
 *           NULL);
 *@endcode
 *
 */
void esp_gmf_app_wifi_connect(void);

/**
 * @brief  Disconnect from current WiFi network
 *         This function performs a clean disconnection from the current WiFi access point by:
 *         1. Terminating the WiFi connection and releasing resources
 *         2. Deinitializing the network interface via `esp_netif_deinit`
 *
 * @note  The function will log disconnection status
 */
void esp_gmf_app_wifi_disconnect(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
