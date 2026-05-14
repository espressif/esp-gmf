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
 * @brief  Initialize BAP unicast server and LE stream objects for ASCS.
 *
 * @param[in]  cfg          LE Audio configuration (ASE counts, PACS masks for announcement).
 * @param[in]  adv_builder  Optional builder to append Unicast Announcement service data.
 *
 * @return
 *       - ESP_OK                 On success
 *       - ESP_ERR_INVALID_ARG    If cfg is NULL
 *       - ESP_ERR_INVALID_STATE  If already initialized
 *       - ESP_ERR_NO_MEM         If allocation fails
 *       - Other                  non-zero codes from BAP registration or stream creation
 */
esp_err_t bt_audio_le_unicast_server_init(const esp_bt_audio_le_cfg_t *cfg, bt_audio_le_adv_builder_t adv_builder);

/**
 * @brief  Tear down unicast server registration and destroy stream objects.
 */
void bt_audio_le_unicast_server_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
