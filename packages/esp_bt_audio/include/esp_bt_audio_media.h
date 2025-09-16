/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * This module provides the media transport control interface for Bluetooth audio
 * (e.g. A2DP). It is used to start or stop transmitting local audio over Bluetooth to
 * remote devices (e.g. headsets, speakers).
 */

/**
 * @brief  Start transmitting local audio media over Bluetooth
 *
 * @param[in]  role    Bluetooth role, now support ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC
 * @param[in]  config  Configuration for media start, NULL for a2dp source
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_media_start(uint32_t role, void *config);

/**
 * @brief  Stop transmitting local audio media over Bluetooth
 *
 * @param[in]  role  Bluetooth role, now support ESP_BT_AUDIO_CLASSIC_ROLE_A2DP_SRC
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_media_stop(uint32_t role);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
