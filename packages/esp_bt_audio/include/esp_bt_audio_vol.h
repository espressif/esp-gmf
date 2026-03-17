/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Set the absolute volume level to the Bluetooth target device
 *
 * @note  This functions will be available when device initializes as controller device, such as AVRCP Controller
 *
 * @param[in]  vol  Absolute volume level
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_vol_set_absolute(uint32_t vol);

/**
 * @brief  Set the relative volume level to the Bluetooth target device
 *
 * @note  This functions will be available when device initializes as controller device, such as AVRCP Controller
 *
 * @param[in]  up_down  Relative volume level, true: up, false: down
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_vol_set_relative(bool up_down);

/**
 * @brief  Notify the Bluetooth target device about the volume change
 *
 * @note  This functions will be available when device initializes as target device, such as AVRCP Target
 *
 * @param[in]  vol  New volume level
 *
 * @return
 *       - ESP_OK  On success
 *       - Others  On failure
 */
esp_err_t esp_bt_audio_vol_notify(uint32_t vol);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
