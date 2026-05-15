/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include "bt_audio_le_adv_builder.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize MICP microphone device registration and advertising UUIDs.
 *
 * @param[in]  adv_builder  Extended advertising builder for MIC service data.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - Other                  non-zero codes from MIC device registration APIs
 */
esp_err_t bt_audio_le_micp_init(bt_audio_le_adv_builder_t adv_builder);

/**
 * @brief  Deinitialize MICP microphone device state.
 */
void bt_audio_le_micp_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
