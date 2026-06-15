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
 * @brief  Initialize BAP Scan Delegator support (single-instance).
 *
 * @param[in]  adv_builder  Extended advertising builder used to announce delegator service.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If callbacks are NULL
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - ESP_ERR_NO_MEM         If allocation fails
 */
esp_err_t bt_audio_le_scan_delegator_init(bt_audio_le_adv_builder_t adv_builder);

/**
 * @brief  Deinitialize scan delegator state and callbacks.
 */
void bt_audio_le_scan_delegator_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
