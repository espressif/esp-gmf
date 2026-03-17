/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_ae_mbc.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_ESP_GMF_MBC_CONFIG() {                                                                                                         \
    .sample_rate     = 16000,                                                                                                                  \
    .channel         = 2,                                                                                                                      \
    .bits_per_sample = 16,                                                                                                                     \
    .fc              = {400, 2100, 6000},                                                                                                      \
    .mbc_para        = {                                                                                                                       \
        {.makeup_gain = 0.0f, .attack_time = 10, .release_time = 100, .hold_time = 0, .ratio = 1.0f, .knee_width = 0.0f, .threshold = -0.1f},  \
        {.makeup_gain = 6.0f, .attack_time = 1, .release_time = 200, .hold_time = 0, .ratio = 4.0f, .knee_width = 5.0f, .threshold = -18.0f},  \
        {.makeup_gain = 7.0f, .attack_time = 1, .release_time = 200, .hold_time = 0, .ratio = 4.0f, .knee_width = 5.0f, .threshold = -20.0f},  \
        {.makeup_gain = 0.0f, .attack_time = 10, .release_time = 100, .hold_time = 0, .ratio = 1.0f, .knee_width = 0.0f, .threshold = -0.1f},  \
    }                                                                                                                                          \
}

/**
 * @brief  Initializes the GMF multi-band compressor with the provided configuration
 *
 * @param[in]   config  Pointer to the MBC configuration
 * @param[out]  handle  Pointer to the MBC element handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_mbc_init(esp_ae_mbc_config_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set the compressor parameter for a specific band identified by 'band_idx'
 *
 * @param[in]  handle    The MBC element handle
 * @param[in]  band_idx  The index of a specific band for which the parameters are to be set
 *                       eg: 0 refers to the first band (low frequency), 3 refers to the last band (high frequency)
 * @param[in]  para      The compressor setup parameter
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_mbc_set_para(esp_gmf_element_handle_t handle, uint8_t band_idx, esp_ae_mbc_para_t *para);

/**
 * @brief  Get the compressor parameter for a specific band identified by 'band_idx'
 *
 * @param[in]   handle    The MBC element handle
 * @param[in]   band_idx  The index of a specific band for which the parameters are to be retrieved
 *                        eg: 0 refers to the first band (low frequency), 3 refers to the last band (high frequency)
 * @param[out]  para      The compressor setup parameter
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_mbc_get_para(esp_gmf_element_handle_t handle, uint8_t band_idx, esp_ae_mbc_para_t *para);

/**
 * @brief  Set the frequency of crossover point identified by 'fc_idx'
 *
 *         The audio spectrum is divided into four bands by three crossover frequencies: low_fc, mid_fc, and high_fc
 *
 * @param[in]  handle  The MBC element handle
 * @param[in]  fc_idx  The index of a crossover point for which the frequency are to be set
 *                     eg: 0 refers to the first point (low_fc), 2 refers to the last point (high_fc)
 * @param[in]  fc      The frequency of crossover point, unit: Hz
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_mbc_set_fc(esp_gmf_element_handle_t handle, uint8_t fc_idx, uint32_t fc);

/**
 * @brief  Get the frequency of crossover point identified by 'fc_idx'
 *
 * @param[in]   handle  The MBC element handle
 * @param[in]   fc_idx  The index of a crossover point for which the frequency are to be retrieved
 *                      eg: 0 refers to the first point (low_fc), 2 refers to the last point (high_fc)
 * @param[out]  fc      The pointer of frequency of crossover point, unit: Hz
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_mbc_get_fc(esp_gmf_element_handle_t handle, uint8_t fc_idx, uint32_t *fc);

/**
 * @brief  Set whether to turn on the function of solo for a specific band identified by 'band_idx'
 *
 *         Solo allows user to isolate and listen to a specific frequency band while muting all other bands.
 *         This helps in focusing on the sound of that individual band without the influence of other frequency ranges.
 *
 * @param[in]  handle       The MBC element handle
 * @param[in]  band_idx     The index of a specific band for which the solo function is to be set
 *                          eg: 0 refers to the first band (low frequency), 3 refers to the last band (high frequency)
 * @param[in]  enable_solo  The flag to turn on the solo function
 *                          True means turn on the solo for a specific band
 *                          False means turn off the solo for a specific band
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_mbc_set_solo(esp_gmf_element_handle_t handle, uint8_t band_idx, bool enable_solo);

/**
 * @brief  Get the solo state for a specific band identified by 'band_idx'
 *
 * @param[in]   handle       The MBC element handle
 * @param[in]   band_idx     The index of a specific band for which the solo state is to be retrieved
 *                           eg: 0 refers to the first band (low frequency), 3 refers to the last band (high frequency)
 * @param[out]  enable_solo  Pointer to store the solo state
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_mbc_get_solo(esp_gmf_element_handle_t handle, uint8_t band_idx, bool *enable_solo);

/**
 * @brief  Set whether to turn on/off the function of bypass for a specific band identified by 'band_idx'
 *
 *         Bypass lets the signal of a selected frequency band pass through without being processed by the compressor.
 *         In Bypass mode, the band's compression is temporarily disabled, but the audio signal still passes through.
 *
 * @param[in]  handle         The MBC element handle
 * @param[in]  band_idx       The index of a specific band for which the bypass function is to be set
 *                            eg: 0 refers to the first band (low frequency), 3 refers to the last band (high frequency)
 * @param[in]  enable_bypass  The flag to turn on/off the bypass function
 *                            True means turn on the bypass for a specific band
 *                            False means turn off the bypass for a specific band
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_mbc_set_bypass(esp_gmf_element_handle_t handle, uint8_t band_idx, bool enable_bypass);

/**
 * @brief  Get the bypass state for a specific band identified by 'band_idx'
 *
 * @param[in]   handle         The MBC element handle
 * @param[in]   band_idx       The index of a specific band for which the bypass state is to be retrieved
 *                             eg: 0 refers to the first band (low frequency), 3 refers to the last band (high frequency)
 * @param[out]  enable_bypass  Pointer to store the bypass state
 *                             True indicates the bypass is enabled for the specified band
 *                             False indicates the bypass is disabled for the specified band
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_mbc_get_bypass(esp_gmf_element_handle_t handle, uint8_t band_idx, bool *enable_bypass);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
