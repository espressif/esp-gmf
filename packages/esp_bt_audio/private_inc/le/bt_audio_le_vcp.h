/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include "esp_bt_audio_defs.h"
#include "bt_audio_le_adv_builder.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize Volume Control Profile renderer (VCS/VCP) and optional AICS/VOCS.
 *
 * @param[in]  cfg          Volume step, mute, and initial volume (may be NULL for defaults).
 * @param[in]  adv_builder  Optional builder to announce VCS (and AICS when enabled).
 *
 * @return
 *       - ESP_OK          On success
 *       - ESP_ERR_NO_MEM  If VOCS/AICS allocation fails
 *       - Other           non-zero codes from VCP registration or bt_audio_ops integration
 */
esp_err_t bt_audio_le_vcp_init(const esp_bt_audio_le_vcp_cfg_t *cfg, bt_audio_le_adv_builder_t adv_builder);

/**
 * @brief  Deinitialize VCP renderer and free VOCS/AICS parameter blocks.
 */
void bt_audio_le_vcp_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
