/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_bt_audio_defs.h"
#include "bt_audio_le_adv_builder.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize LE broadcast source (BIG + streams) for the given configuration.
 *
 * @param[in]  cfg          Global LE Audio configuration (broadcast subgroup in cfg->bsrc).
 * @param[in]  adv_builder  Builder used to append BAS broadcast announcement AD.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If cfg or adv_builder is NULL
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - ESP_ERR_NO_MEM         If allocation fails
 *       - Other                  non-zero codes from BAP or advertising builder APIs
 */
esp_err_t bt_audio_le_broadcast_source_init(const esp_bt_audio_le_cfg_t *cfg, bt_audio_le_adv_builder_t adv_builder);

/**
 * @brief  Configure periodic advertising payload (BASE) and start periodic adv on a handle.
 *
 * @param[in]  adv_handle  NimBLE extended advertising set index.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If broadcast source not initialized
 *       - ESP_FAIL               On NimBLE or mbuf errors
 */
esp_err_t bt_audio_le_broadcast_source_start_periodic_adv(uint8_t adv_handle);

/**
 * @brief  Add BIG extended advertising and start the broadcast source audio path.
 *
 * @param[in]  adv_handle  Extended advertising handle carrying the BIG.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_STATE  If not initialized
 *       - Other                  non-zero codes from ISO or BAP broadcast start APIs
 */
esp_err_t bt_audio_le_broadcast_source_start(uint8_t adv_handle);

/**
 * @brief  Stop and destroy broadcast source, streams, and ISO state.
 */
void bt_audio_le_broadcast_source_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
