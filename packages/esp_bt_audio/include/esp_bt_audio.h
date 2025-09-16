/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */
#pragma once

#include "esp_err.h"

#include "esp_bt_audio_defs.h"
#include "esp_bt_audio_event.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Structure for Bluetooth configuration
 */
typedef struct {
    void                       *host_config;     /*!< Host configuration (e.g. esp_bt_audio_host_bluedroid_cfg_t or esp_bt_audio_host_nimble_cfg_t)
                                                      If NULL, the host will not be initialized */
    esp_bt_audio_event_cb_t     event_cb;        /*!< Event callback function */
    void                       *event_user_ctx;  /*!< User context for the event callback */
    esp_bt_audio_classic_cfg_t  classic;         /*!< Classic Bluetooth configuration (optional, only available when classic Bluetooth is enabled) */
} esp_bt_audio_config_t;

/**
 * @brief  Initialize Bluetooth module
 *
 *         Profiles are initialized automatically when the corresponding role is enabled
 *
 * @param[in]  bt_config  Pointer to Bluetooth configuration
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_init(esp_bt_audio_config_t *bt_config);

/**
 * @brief  Deinitialize Bluetooth module
 */
void esp_bt_audio_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
