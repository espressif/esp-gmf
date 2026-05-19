/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "asrc_utils.h"
#include "esp_ae_bit_cvt.h"
#include "asrc_sw_bit_cvt_ops.h"

static const char *TAG = "ASRC_SW_BIT_CVT";

static esp_asrc_err_t asrc_sw_bit_cvt_open(esp_asrc_cfg_t *cfg, esp_asrc_handle_t *handle)
{
    esp_ae_bit_cvt_cfg_t ae_cfg = {0};
    ae_cfg.sample_rate = cfg->src_info.sample_rate;
    ae_cfg.channel = cfg->src_info.channel;
    ae_cfg.src_bits = cfg->src_info.bits_per_sample;
    ae_cfg.dest_bits = cfg->dest_info.bits_per_sample;
    esp_ae_err_t ae_ret = esp_ae_bit_cvt_open(&ae_cfg, handle);
    return (ae_ret == ESP_AE_ERR_OK ? ESP_ASRC_ERR_OK : ESP_ASRC_ERR_FAIL);
}

static esp_asrc_err_t asrc_sw_bit_cvt_process(esp_asrc_handle_t handle, uint8_t *in_samples, uint32_t in_samples_num,
                                              uint8_t *out_samples, uint32_t *out_samples_num)
{
    if (*out_samples_num < in_samples_num) {
        ESP_LOGE(TAG, "Sample num(%" PRIu32 ") for output less than needed(%" PRIu32 ") in bit convert process", *out_samples_num, in_samples_num);
        return ESP_ASRC_ERR_INVALID_PARAMETER;
    }
    esp_ae_err_t ae_ret = esp_ae_bit_cvt_process((esp_ae_bit_cvt_handle_t)handle, in_samples_num, in_samples, out_samples);
    if (ae_ret == ESP_AE_ERR_OK) {
        *out_samples_num = in_samples_num;
        return ESP_ASRC_ERR_OK;
    }
    return ESP_ASRC_ERR_FAIL;
}

static esp_asrc_err_t asrc_sw_bit_cvt_get_out_sample_num(esp_asrc_handle_t handle, uint32_t in_samples_num, uint32_t *out_samples_num)
{
    *out_samples_num = in_samples_num;
    return ESP_ASRC_ERR_OK;
}

const esp_asrc_ops_t *asrc_sw_get_bit_cvt_ops()
{
    static const esp_asrc_ops_t asrc_ops = {
        .open = asrc_sw_bit_cvt_open,
        .get_out_sample_num = asrc_sw_bit_cvt_get_out_sample_num,
        .process = asrc_sw_bit_cvt_process,
        .close = esp_ae_bit_cvt_close,
    };
    return &asrc_ops;
}
