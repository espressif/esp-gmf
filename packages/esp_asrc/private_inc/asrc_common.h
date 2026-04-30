/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_asrc_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  ASRC module operation interface
 */
typedef struct {
    esp_asrc_err_t (*open)(esp_asrc_cfg_t *cfg, esp_asrc_handle_t *handle);
    esp_asrc_err_t (*get_out_sample_num)(esp_asrc_handle_t handle, uint32_t in_samples_cnt, uint32_t *out_samples_cnt);
    esp_asrc_err_t (*process)(esp_asrc_handle_t handle, uint8_t *in_samples, uint32_t in_samples_num,
                              uint8_t *out_samples, uint32_t *out_sample_num);
    void (*close)(esp_asrc_handle_t handle);
} esp_asrc_ops_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
