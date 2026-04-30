/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "asrc_caps_check.h"

#define IS_VALID_SR(sr)          ((sr) == 8000 || (sr) == 16000 || (sr) == 24000 || (sr) == 32000 || (sr) == 44100 || (sr) == 48000)
#define IS_VALID_CH(ch)          ((ch) == 1 || (ch) == 2)
#define IS_VALID_BITS(b)         ((b) == 16)
#define IS_VALID_SRC_INFO(info)  (IS_VALID_BITS((info)->bits_per_sample) && IS_VALID_CH((info)->channel) && IS_VALID_SR((info)->sample_rate))

uint8_t asrc_hw_rate_cvt_caps(esp_asrc_aud_info_t *src_info, uint32_t dest_rate)
{
    return (IS_VALID_SRC_INFO(src_info) && IS_VALID_SR(dest_rate));
}

uint8_t asrc_hw_ch_cvt_caps(esp_asrc_aud_info_t *src_info, uint8_t dest_ch, float *weight, uint8_t weight_len)
{
    if (IS_VALID_SRC_INFO(src_info) && IS_VALID_CH(dest_ch)) {
        if (src_info->channel == dest_ch) {
            return 1;
        }
        if (weight != NULL && weight_len == 2) {
            if (src_info->channel == 1 && dest_ch == 2) {
                return (weight[0] == 1.0f && weight[1] == 1.0f);
            }
            if (src_info->channel == 2 && dest_ch == 1) {
                return ((weight[0] == 1.0f && weight[1] == 0.0f) || (weight[0] == 0.0f && weight[1] == 1.0f));
            }
        }
    }
    return 0;
}

uint8_t asrc_hw_bit_cvt_caps(esp_asrc_aud_info_t *src_info, uint8_t dest_bits)
{
    return (IS_VALID_SRC_INFO(src_info) && IS_VALID_BITS(dest_bits));
}
