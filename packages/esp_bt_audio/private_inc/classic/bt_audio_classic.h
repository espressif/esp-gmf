/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include "esp_bt_audio_defs.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize Classic Bluetooth audio
 *
 * @param[in]  classic_config  Pointer to Classic Bluetooth configuration
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If already initialized
 */
esp_err_t bt_audio_classic_init(esp_bt_audio_classic_cfg_t *classic_config);

/**
 * @brief  Deinitialize Classic Bluetooth audio
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_classic_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
