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
 * @brief  Hardware capability callback table for ASRC conversions
 */
typedef struct {
    uint8_t (*rate_cvt_caps)(esp_asrc_aud_info_t *src_info, uint32_t  dest_rate);
    uint8_t (*ch_cvt_caps)(esp_asrc_aud_info_t *src_info, uint8_t dest_ch, float *weight, uint8_t  weight_len);
    uint8_t (*bit_cvt_caps)(esp_asrc_aud_info_t *src_info, uint8_t  dest_bits);
} asrc_hw_caps_t;

/**
 * @brief  Get hardware capability callback table
 *
 * @return
 *       - Pointer  to the hardware capability operations table
 */
const asrc_hw_caps_t *asrc_hw_get_caps();

/**
 * @brief  Get hardware ASRC runtime operations
 *
 * @return
 *       - Pointer  to hardware ASRC operations
 */
const esp_asrc_ops_t *asrc_hw_get_ops();

#ifdef __cplusplus
}
#endif  /* __cplusplus */
