/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Control LCD power
 *
 * @param  enable  true to enable LCD power, false to disable
 * @return
 *       - ESP_OK                 Success
 *       - ESP_ERR_INVALID_STATE  Power control not initialized
 *       - ESP_FAIL               Operation failed
 */
esp_err_t test_dev_pwr_lcd_ctrl(bool enable);

/**
 * @brief  Control audio codec power
 *
 * @param  enable  true to enable audio power, false to disable
 * @return
 *       - ESP_OK                 Success
 *       - ESP_ERR_INVALID_STATE  Power control not initialized
 *       - ESP_FAIL               Operation failed
 */
esp_err_t test_dev_pwr_audio_ctrl(bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
