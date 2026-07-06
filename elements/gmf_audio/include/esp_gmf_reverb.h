/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_ae_reverb.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_ESP_GMF_REVERB_CONFIG()  {  \
    .sample_rate     = 48000,               \
    .channel         = 2,                   \
    .bits_per_sample = 16,                  \
    .reverb_para     = {                    \
        .room_size    = 0.5f,               \
        .damping      = 0.5f,               \
        .wet_level    = -6.0f,              \
        .dry_level    = 0.0f,               \
        .pre_delay_ms = 20,                 \
    },                                      \
}

/**
 * @brief  Initializes the GMF reverb element with the provided configuration
 *
 * @param[in]   config  Pointer to the reverb configuration. May be NULL for defaults.
 * @param[out]  handle  Pointer to the element handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_reverb_init(esp_ae_reverb_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set the room size factor
 *
 * @param[in]  handle     The reverb element handle
 * @param[in]  room_size  Room size factor, range: [0.0, 1.0]
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_reverb_set_room_size(esp_gmf_element_handle_t handle, float room_size);

/**
 * @brief  Get the room size factor
 *
 * @param[in]   handle     The reverb element handle
 * @param[out]  room_size  Room size factor
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_reverb_get_room_size(esp_gmf_element_handle_t handle, float *room_size);

/**
 * @brief  Set the damping factor
 *
 * @param[in]  handle   The reverb element handle
 * @param[in]  damping  Damping factor, range: [0.0, 1.0]
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_reverb_set_damping(esp_gmf_element_handle_t handle, float damping);

/**
 * @brief  Get the damping factor
 *
 * @param[in]   handle   The reverb element handle
 * @param[out]  damping  Damping factor
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_reverb_get_damping(esp_gmf_element_handle_t handle, float *damping);

/**
 * @brief  Set the wet signal level
 *
 * @param[in]  handle     The reverb element handle
 * @param[in]  wet_level  Wet level in dB, range: [-96.0, 0.0]
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_reverb_set_wet_level(esp_gmf_element_handle_t handle, float wet_level);

/**
 * @brief  Get the wet signal level
 *
 * @param[in]   handle     The reverb element handle
 * @param[out]  wet_level  Wet level in dB
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_reverb_get_wet_level(esp_gmf_element_handle_t handle, float *wet_level);

/**
 * @brief  Set the dry signal level
 *
 * @param[in]  handle     The reverb element handle
 * @param[in]  dry_level  Dry level in dB, range: [-96.0, 0.0]
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_reverb_set_dry_level(esp_gmf_element_handle_t handle, float dry_level);

/**
 * @brief  Get the dry signal level
 *
 * @param[in]   handle     The reverb element handle
 * @param[out]  dry_level  Dry level in dB
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_reverb_get_dry_level(esp_gmf_element_handle_t handle, float *dry_level);

/**
 * @brief  Reset the internal processing state of the reverb element
 *
 * @param[in]  handle  The reverb element handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           Operation succeeded
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_reverb_reset(esp_gmf_element_handle_t handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
