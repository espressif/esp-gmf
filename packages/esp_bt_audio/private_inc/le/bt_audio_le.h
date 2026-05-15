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
 * @brief  Initialize LE Audio support.
 *
 * @param[in]  cfg  LE Audio configuration.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If cfg is NULL or inconsistent
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - ESP_ERR_NO_MEM         If a submodule allocation fails
 *       - Other                  non-zero codes from ESP-BLE-Audio or NimBLE APIs
 */
esp_err_t bt_audio_le_init(const esp_bt_audio_le_cfg_t *cfg);

/**
 * @brief  Deinitialize LE Audio support.
 */
void bt_audio_le_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
