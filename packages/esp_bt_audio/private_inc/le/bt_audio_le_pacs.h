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
 * @brief  Register Published Audio Capabilities (PACS) for LE Audio.
 *
 * @note  Single-instance module. Call @ref bt_audio_le_pacs_unregister before re-registering with different cfg.
 *
 * @param[in]  cfg  Sink/source PAC and context configuration.
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  If cfg is NULL
 *       - Other                non-zero codes from esp_ble_audio_pacs_* APIs
 */
esp_err_t bt_audio_le_pacs_register(const esp_bt_audio_le_pacs_cfg_t *cfg);

/**
 * @brief  Unregister PACS capabilities and service state registered by this module.
 */
void bt_audio_le_pacs_unregister(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
