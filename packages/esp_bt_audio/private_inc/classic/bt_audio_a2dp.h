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
 * @brief  Initialize A2DP Sink
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_NO_MEM         No memory available
 *       - ESP_ERR_INVALID_STATE  If already initialized
 */
esp_err_t bt_audio_a2dp_sink_init(void);

/**
 * @brief  Deinitialize A2DP Sink
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_a2dp_sink_deinit(void);

/**
 * @brief  Initialize A2DP Source
 *
 * @param[in]  classic_cfg  Classic Bluetooth configuration
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    Invalid arguments
 *       - ESP_ERR_NO_MEM         No memory available
 *       - ESP_ERR_INVALID_STATE  If already initialized
 */
esp_err_t bt_audio_a2dp_src_init(const esp_bt_audio_classic_cfg_t *classic_cfg);

/**
 * @brief  Deinitialize A2DP Source
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If not initialized
 */
esp_err_t bt_audio_a2dp_src_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
