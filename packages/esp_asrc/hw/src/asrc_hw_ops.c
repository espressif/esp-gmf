/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "asrc_hw_ops.h"
#ifdef CONFIG_SOC_ASRC_SUPPORTED
#include "asrc_caps_check.h"
#include "esp_asrc_hw.h"
#endif  /* CONFIG_SOC_ASRC_SUPPORTED */

const esp_asrc_ops_t *asrc_hw_get_ops()
{
#ifdef CONFIG_SOC_ASRC_SUPPORTED
    static const esp_asrc_ops_t asrc_ops = {
        .open = esp_asrc_hw_open,
        .get_out_sample_num = esp_asrc_hw_get_out_sample_num,
        .process = esp_asrc_hw_process,
        .close = esp_asrc_hw_close,
    };
    return &asrc_ops;
#else
    return NULL;
#endif  /* CONFIG_SOC_ASRC_SUPPORTED */
}

const asrc_hw_caps_t *asrc_hw_get_caps()
{
#ifdef CONFIG_SOC_ASRC_SUPPORTED
    static const asrc_hw_caps_t asrc_caps = {
        .rate_cvt_caps = asrc_hw_rate_cvt_caps,
        .ch_cvt_caps = asrc_hw_ch_cvt_caps,
        .bit_cvt_caps = asrc_hw_bit_cvt_caps,
    };
    return &asrc_caps;
#else
    return NULL;
#endif  /* CONFIG_SOC_ASRC_SUPPORTED */
}
