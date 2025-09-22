/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_audio_simple_player.h"
#include "esp_gmf_io.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  The audio simple player uses fixed Espressif official tags for IO, such as `http` for HTTP IO, `file` for FILE IO, etc.
 *         If you want to use other IOs, please use `esp_audio_simple_player_register_io` to register the IO.
 *         If you want to add new element for pipeline, please use `esp_audio_simple_player_register_el` to register the elements.
 *         Then, call `esp_audio_simple_player_set_pipeline` to set the specific pipeline.
 */

/**
 * @brief  Register an IO handle with the audio simple player
 *
 * @note   It is called after `esp_audio_simple_player_new` and before `esp_audio_simple_player_run`
 *         The registered IO handle is destroyed only by `esp_audio_simple_player_destroy`
 *
 * @param[in]  handle  The handle to the audio simple player instance
 * @param[in]  io      The IO handle to register
 *
 * @return
 *       - ESP_GMF_ERR_OK           Successfully registered the IO handle
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument(s), such as a NULL handle or io
 *       - ESP_GMF_ERR_FAIL         Failed to register the IO handle due to other internal errors
 */
esp_gmf_err_t esp_audio_simple_player_register_io(esp_asp_handle_t handle, esp_gmf_io_handle_t io);

/**
 * @brief  Register an element handle with the audio simple player
 *
 * @note
 *      - It is called after `esp_audio_simple_player_new` and before `esp_audio_simple_player_run`
 *      - The registered element handle is destroyed only by `esp_audio_simple_player_destroy`
 *
 * @param[in]  handle   The handle to the audio simple player instance
 * @param[in]  element  The element handle to register
 *
 * @return
 *       - ESP_GMF_ERR_OK           Successfully registered the element handle
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument(s), such as a NULL handle or element
 *       - ESP_GMF_ERR_FAIL         Failed to register the element handle due to other internal errors
 */
esp_gmf_err_t esp_audio_simple_player_register_el(esp_asp_handle_t handle, esp_gmf_element_handle_t element);

/**
 * @brief  Sets up the pipeline using the input name and element names for the ESP Audio Simple Player to function
 *
 * @note
 *     - This function must be called after initializing the ESP Audio Simple Player but before `esp_audio_simple_player_run`
 *     - Ensure that all specified element names (`in_name`, `el_name`) correspond to valid and properly registered components in the audio pipeline
 *     - After this API is called, the esp_audio_simple_player_run only runs the pipeline; it no longer sets up the pipeline using the URI
 *     - The sets pipeline handle is destroyed only by `esp_audio_simple_player_destroy`
 *
 * @param  handle          The handle to the ESP Audio Simple Player instance
 * @param  in_name         The name of the input element in the pipeline
 * @param  el_name[]       An array of names representing the intermediate elements
 *                         in the pipeline (e.g., audio processors, effects)
 * @param  num_of_el_name  The number of intermediate elements specified in the `el_name` array
 *
 * @return
 *       - ESP_GMF_ERR_OK           Successfully registered the element handle
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument(s), such as a NULL handle or element
 *       - ESP_GMF_ERR_FAIL         Failed to register the element handle due to other internal errors
 */
esp_gmf_err_t esp_audio_simple_player_set_pipeline(esp_asp_handle_t handle, const char *in_name, const char *el_name[], int num_of_el_name);

/**
 * @brief  Gets the pipeline handle from the ESP Audio Simple Player instance
 *
 * @note
 *     - This function can be called after `esp_audio_simple_player_set_pipeline` or `esp_audio_simple_player_run`
 *     - The returned pipeline handle should not be destroyed by the caller, as it is managed by the ESP Audio Simple Player
 *
 * @param[in]   handle  The handle to the ESP Audio Simple Player instance
 * @param[out]  pipe    Pointer to store the pipeline handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           Successfully got the pipeline handle
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument(s), such as a NULL handle or pipe pointer
 *       - ESP_GMF_ERR_FAIL         Failed to get the pipeline handle due to other internal errors
 */
esp_gmf_err_t esp_audio_simple_player_get_pipeline(esp_asp_handle_t handle, esp_gmf_pipeline_handle_t *pipe);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
