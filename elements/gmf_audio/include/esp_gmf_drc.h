/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_err.h"
#include "esp_ae_drc.h"
#include "esp_gmf_element.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_ESP_GMF_DRC_CONFIG() {  \
    .sample_rate     = 48000,           \
    .channel         = 2,               \
    .bits_per_sample = 16,              \
    .drc_para        = {                \
        .point        = NULL,           \
        .point_num    = 0,              \
        .makeup_gain  = 0.0f,           \
        .knee_width   = 1.0f,           \
        .attack_time  = 10,             \
        .release_time = 80,             \
        .hold_time    = 3,              \
    },                                  \
}

/**
 * @brief  Initialize the GMF DRC element with the provided configuration
 *
 * @param[in]   config  Pointer to the DRC configuration (see @ref esp_ae_drc_cfg_t)
 * @param[out]  handle  Pointer to the DRC element handle to be created
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_drc_init(esp_ae_drc_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set the attack time of the DRC
 *
 * @param[in]  handle       DRC element handle
 * @param[in]  attack_time  Attack time, range: [0, 500], unit: ms
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_set_attack_time(esp_gmf_element_handle_t handle, uint16_t attack_time);

/**
 * @brief  Get the attack time of the DRC
 *
 * @param[in]   handle       DRC element handle
 * @param[out]  attack_time  Attack time in milliseconds
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_get_attack_time(esp_gmf_element_handle_t handle, uint16_t *attack_time);

/**
 * @brief  Set the release time of the DRC
 *
 * @param[in]  handle        DRC element handle
 * @param[in]  release_time  Release time, range: [0, 500], unit: ms
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_set_release_time(esp_gmf_element_handle_t handle, uint16_t release_time);

/**
 * @brief  Get the release time of the DRC
 *
 * @param[in]   handle        DRC element handle
 * @param[out]  release_time  Release time in milliseconds
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_get_release_time(esp_gmf_element_handle_t handle, uint16_t *release_time);

/**
 * @brief  Set the hold time of the DRC
 *
 * @param[in]  handle     DRC element handle
 * @param[in]  hold_time  Hold time, range: [0, 100], unit: ms
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_set_hold_time(esp_gmf_element_handle_t handle, uint16_t hold_time);

/**
 * @brief  Get the hold time of the DRC
 *
 * @param[in]   handle     DRC element handle
 * @param[out]  hold_time  Hold time in milliseconds
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_get_hold_time(esp_gmf_element_handle_t handle, uint16_t *hold_time);

/**
 * @brief  Set the makeup gain of the DRC
 *
 * @param[in]  handle       DRC element handle
 * @param[in]  makeup_gain  Makeup gain, range: [-10.0, 10.0], unit: dB
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_set_makeup_gain(esp_gmf_element_handle_t handle, float makeup_gain);

/**
 * @brief  Get the makeup gain of the DRC
 *
 * @param[in]   handle       DRC element handle
 * @param[out]  makeup_gain  Makeup gain in dB
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_get_makeup_gain(esp_gmf_element_handle_t handle, float *makeup_gain);

/**
 * @brief  Set the knee width of the DRC
 *
 * @param[in]  handle      DRC element handle
 * @param[in]  knee_width  Knee width, range: [0.0, 10.0]
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_set_knee_width(esp_gmf_element_handle_t handle, float knee_width);

/**
 * @brief  Get the knee width of the DRC
 *
 * @param[in]   handle      DRC element handle
 * @param[out]  knee_width  Knee width factor (softness)
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_get_knee_width(esp_gmf_element_handle_t handle, float *knee_width);

/**
 * @brief  Set the static curve points of the DRC
 *
 *         The curve describes input level (x) to output level (y) mapping in dB.
 *
 * @note  1. The point_num must be greater than or equal to 2 and less than or equal to 6
 *        2. The x-axis value and y-axis value of the point must be range of [-100.0, 0.0]
 *        3. The x-axis value of the different point must be unique
 *        4. The x-axis value of the point must have value of 0.0 and -100.0
 *
 * @param[in]  handle     DRC element handle
 * @param[in]  points     Array of curve points
 * @param[in]  point_num  Number of points in the array, range: [2, 6]
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_set_points(esp_gmf_element_handle_t handle, esp_ae_drc_curve_point *points, uint8_t point_num);

/**
 * @brief  Get the number of static curve points configured in the DRC
 *
 * @param[in]   handle     DRC element handle
 * @param[out]  point_num  Number of points currently configured
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_get_point_num(esp_gmf_element_handle_t handle, uint8_t *point_num);

/**
 * @brief  Retrieve the static curve points of the DRC
 *
 *         The provided buffer must be large enough to hold all points (see
 *         @ref esp_gmf_drc_get_point_num).
 *
 * @param[in]   handle  DRC element handle
 * @param[out]  points  Output buffer for curve points
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid input parameter
 */
esp_gmf_err_t esp_gmf_drc_get_points(esp_gmf_element_handle_t handle, esp_ae_drc_curve_point *points);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
