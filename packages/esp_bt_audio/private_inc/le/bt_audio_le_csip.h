/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include "esp_ble_audio_csip_api.h"
#include "esp_bt_audio_defs.h"
#include "bt_audio_le_adv_builder.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize CSIP set member service and optional advertising data.
 *
 * @param[in]   cfg              CSIP configuration (set size, locks, etc.).
 * @param[out]  inst             Receives registered set member instance pointers.
 * @param[out]  rsi              Receives RSI value for advertising.
 * @param[in]   included_by_cas  Whether CAS includes the set member.
 * @param[in]   adv_builder      Optional builder to append CSIS RSI data.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If required pointers are NULL
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - Other                  non-zero codes From CSIP registration APIs
 */
esp_err_t bt_audio_le_csip_init(const esp_bt_audio_le_csip_cfg_t *cfg,
                                esp_ble_audio_csip_set_member_svc_inst_t **inst,
                                uint8_t *rsi,
                                bool included_by_cas,
                                bt_audio_le_adv_builder_t adv_builder);

/**
 * @brief  Deinitialize CSIP set member state.
 */
void bt_audio_le_csip_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
