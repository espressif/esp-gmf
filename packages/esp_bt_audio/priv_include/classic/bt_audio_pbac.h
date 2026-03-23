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
 * @brief  Initialize phonebook access profile client
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_NO_MEM         No memory available
 *       - ESP_ERR_INVALID_STATE  If already initialized
 */
esp_err_t bt_audio_pbac_init(void);

/**
 * @brief  Deinitialize phonebook access profile client
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_pbac_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
