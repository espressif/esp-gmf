/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_gmf_element.h"
#include "esp_ae_howl.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_ESP_GMF_HOWL_CONFIG() {  \
    .sample_rate     = 48000,            \
    .channel         = 2,                \
    .bits_per_sample = 16,               \
    .papr_th         = 10.0f,            \
    .phpr_th         = 45.0f,            \
    .pnpr_th         = 45.0f,            \
    .imsd_th         = 10.0f,            \
    .enable_imsd     = true,             \
}

/**
 * @brief  Initializes the GMF howling-suppression element with the provided configuration
 *
 * @param[in]   config  Pointer to the HOWL configuration (see esp_ae_howl.h). May be NULL for defaults.
 * @param[out]  handle  Pointer to the element handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_howl_init(esp_ae_howl_cfg_t *config, esp_gmf_element_handle_t *handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
