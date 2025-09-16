/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_types.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#ifdef CONFIG_BT_BLUEDROID_ENABLED

/**
 * @brief  Start Classic Bluetooth discovery
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_classic_discovery_start(void);

/**
 * @brief  Stop Classic Bluetooth discovery
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_classic_discovery_stop(void);

/**
 * @brief  Connect to a Classic Bluetooth device
 *
 * @param[in]  role  Classic Bluetooth role
 * @param[in]  bda   Bluetooth device address
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - ESP_ERR_INVALID_ARG    Invalid role
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_classic_connect(uint32_t role, uint8_t *bt_dev_addr);

/**
 * @brief  Disconnect from a Classic Bluetooth device
 *
 * @param[in]  role  Classic Bluetooth role
 * @param[in]  bda   Bluetooth device address
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - ESP_ERR_INVALID_ARG    Invalid role
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_classic_disconnect(uint32_t role, uint8_t *bt_dev_addr);

/**
 * @brief  Set Classic Bluetooth scan mode
 *
 * @param[in]  discoverable  Discoverable mode
 * @param[in]  connectable   Connectable mode
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  Invalid state
 *       - Others                 On failure
 */
esp_err_t esp_bt_audio_classic_set_scan_mode(bool connectable, bool discoverable);

#endif  /* CONFIG_BT_BLUEDROID_ENABLED */

#ifdef __cplusplus
}
#endif  /* __cplusplus */
