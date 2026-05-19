/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_asrc_types.h"
#include "asrc_common.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Get software ASRC operations for channel conversion only
 *
 * @return
 *       - Pointer  to software channel-conversion ops table
 */
const esp_asrc_ops_t *asrc_sw_get_ch_cvt_ops();

#ifdef __cplusplus
}
#endif  /* __cplusplus */
