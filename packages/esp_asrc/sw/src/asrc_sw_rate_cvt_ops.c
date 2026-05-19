/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdint.h>
#include "esp_ae_rate_cvt.h"
#include "asrc_sw_rate_cvt_ops.h"

static esp_asrc_err_t asrc_sw_rate_cvt_open(esp_asrc_cfg_t *cfg, esp_asrc_handle_t *handle)
{
    esp_ae_rate_cvt_cfg_t ae_cfg = {0};
    ae_cfg.src_rate = cfg->src_info.sample_rate;
    ae_cfg.dest_rate = cfg->dest_info.sample_rate;
    ae_cfg.bits_per_sample = cfg->src_info.bits_per_sample;
    ae_cfg.channel = cfg->src_info.channel;
    ae_cfg.complexity = cfg->complexity;
    ae_cfg.perf_type = cfg->perf_type == ESP_ASRC_PERF_TYPE_SW_MEMORY ? ESP_AE_RATE_CVT_PERF_TYPE_MEMORY : ESP_AE_RATE_CVT_PERF_TYPE_SPEED;
    esp_ae_err_t ae_ret = esp_ae_rate_cvt_open(&ae_cfg, handle);
    return (ae_ret == ESP_AE_ERR_OK ? ESP_ASRC_ERR_OK : ESP_ASRC_ERR_FAIL);
}

static esp_asrc_err_t asrc_sw_rate_cvt_process(esp_asrc_handle_t handle, uint8_t *in_samples, uint32_t in_samples_num,
                                               uint8_t *out_samples, uint32_t *out_samples_num)
{
    esp_ae_err_t ae_ret = esp_ae_rate_cvt_process((esp_ae_rate_cvt_handle_t)handle, in_samples, in_samples_num, out_samples, out_samples_num);
    return (ae_ret == ESP_AE_ERR_OK ? ESP_ASRC_ERR_OK : ESP_ASRC_ERR_FAIL);
}

static esp_asrc_err_t asrc_sw_rate_cvt_get_out_sample_num(esp_asrc_handle_t handle, uint32_t in_samples_num, uint32_t *out_samples_num)
{
    esp_ae_err_t ret = esp_ae_rate_cvt_get_max_out_sample_num((esp_ae_rate_cvt_handle_t)handle, in_samples_num, out_samples_num);
    return (ret == ESP_AE_ERR_OK ? ESP_ASRC_ERR_OK : ESP_ASRC_ERR_FAIL);
}

const esp_asrc_ops_t *asrc_sw_get_rate_cvt_ops()
{
    static const esp_asrc_ops_t asrc_ops = {
        .open = asrc_sw_rate_cvt_open,
        .get_out_sample_num = asrc_sw_rate_cvt_get_out_sample_num,
        .process = asrc_sw_rate_cvt_process,
        .close = esp_ae_rate_cvt_close,
    };
    return &asrc_ops;
}
