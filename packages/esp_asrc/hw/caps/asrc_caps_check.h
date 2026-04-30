/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include "esp_asrc_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Check sample rate conversion capability
 *
 * @param[in]  src_info   Pointer to source audio information
 * @param[in]  dest_rate  Destination sample rate
 *
 * @return
 *       - 1  Supported
 *       - 0  Not supported
 */
uint8_t asrc_hw_rate_cvt_caps(esp_asrc_aud_info_t *src_info, uint32_t dest_rate);

/**
 * @brief  Check channel conversion capability
 *
 * @param[in]  src_info    Pointer to source audio information
 * @param[in]  dest_ch     Destination channel number
 * @param[in]  weight      Channel mixing weight array
 * @param[in]  weight_len  Weight array length
 *
 * @return
 *       - 1  Supported
 *       - 0  Not supported
 */
uint8_t asrc_hw_ch_cvt_caps(esp_asrc_aud_info_t *src_info, uint8_t dest_ch, float *weight, uint8_t weight_len);

/**
 * @brief  Check bit depth conversion capability
 *
 * @param[in]  src_info   Pointer to source audio information
 * @param[in]  dest_bits  Destination bits per sample
 *
 * @return
 *       - 1  Supported
 *       - 0  Not supported
 */
uint8_t asrc_hw_bit_cvt_caps(esp_asrc_aud_info_t *src_info, uint8_t dest_bits);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
