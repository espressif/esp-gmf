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

/**
 * @brief  Set the PAPR (Peak to Average Power Ratio) threshold
 *
 * @param[in]  handle   The HOWL element handle
 * @param[in]  papr_th  PAPR threshold in dB, range: [-10.0, 20.0]
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_howl_set_papr_th(esp_gmf_element_handle_t handle, float papr_th);

/**
 * @brief  Get the PAPR (Peak to Average Power Ratio) threshold
 *
 * @param[in]   handle   The HOWL element handle
 * @param[out]  papr_th  PAPR threshold in dB
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_howl_get_papr_th(esp_gmf_element_handle_t handle, float *papr_th);

/**
 * @brief  Set the PHPR (Peak to Harmonic Power Ratio) threshold
 *
 * @param[in]  handle   The HOWL element handle
 * @param[in]  phpr_th  PHPR threshold in dB, range: [0.0, 100.0]
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_howl_set_phpr_th(esp_gmf_element_handle_t handle, float phpr_th);

/**
 * @brief  Get the PHPR (Peak to Harmonic Power Ratio) threshold
 *
 * @param[in]   handle   The HOWL element handle
 * @param[out]  phpr_th  PHPR threshold in dB
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_howl_get_phpr_th(esp_gmf_element_handle_t handle, float *phpr_th);

/**
 * @brief  Set the PNPR (Peak to Noise Power Ratio) threshold
 *
 * @param[in]  handle   The HOWL element handle
 * @param[in]  pnpr_th  PNPR threshold in dB, range: [0.0, 100.0]
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_howl_set_pnpr_th(esp_gmf_element_handle_t handle, float pnpr_th);

/**
 * @brief  Get the PNPR (Peak to Noise Power Ratio) threshold
 *
 * @param[in]   handle   The HOWL element handle
 * @param[out]  pnpr_th  PNPR threshold in dB
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_howl_get_pnpr_th(esp_gmf_element_handle_t handle, float *pnpr_th);

/**
 * @brief  Set the IMSD (Inter-Frame Magnitude Spectral Deviation) threshold
 *
 * @note  This only takes effect when IMSD is enabled (enable_imsd is true in cfg)
 *
 * @param[in]  handle   The HOWL element handle
 * @param[in]  imsd_th  IMSD threshold, range: [0.0, 20.0]
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_howl_set_imsd_th(esp_gmf_element_handle_t handle, float imsd_th);

/**
 * @brief  Get the IMSD (Inter-Frame Magnitude Spectral Deviation) threshold
 *
 * @param[in]   handle   The HOWL element handle
 * @param[out]  imsd_th  IMSD threshold
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_howl_get_imsd_th(esp_gmf_element_handle_t handle, float *imsd_th);

/**
 * @brief  Enable or disable IMSD (Inter-Frame Magnitude Spectral Deviation)
 *
 * @note  Changing this parameter requires reopening the underlying HOWL handle
 *        because it affects internal buffer allocation. The reopen happens on
 *        the next process call when the value differs from the current cfg
 *
 * @param[in]  handle        The HOWL element handle
 * @param[in]  enable_imsd   true to enable IMSD, false to disable
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_howl_set_enable_imsd(esp_gmf_element_handle_t handle, bool enable_imsd);

/**
 * @brief  Get whether IMSD is enabled
 *
 * @param[in]   handle        The HOWL element handle
 * @param[out]  enable_imsd   Pointer to store the enable flag
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_howl_get_enable_imsd(esp_gmf_element_handle_t handle, bool *enable_imsd);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
