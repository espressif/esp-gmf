/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_types.h"
#include "esp_capture_video_src_if.h"
#include "esp_gmf_element.h"
#include "esp_capture_sync.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Video source element configuration structure
 */
typedef struct {
    esp_capture_video_src_if_t  *vsrc_if;  /*!< Video source interface to use */
} capture_video_src_el_cfg_t;

/**
 * @brief  Initialize video capture source element
 *
 * @param[in]   cfg     Video capture source element configuration (optional)
 *                      If set to NULL, user need call `capture_video_src_el_set_src_if` before element run
 * @param[out]  handle  Pointer to store GMF element handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_MEMORY_LACK  Not enough memory
 */
esp_gmf_err_t capture_video_src_el_init(capture_video_src_el_cfg_t *cfg, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set capture synchronization handle for video source
 *
 * @note  This function can only be called before the element starts running
 *        The video source will use this handle to perform synchronization actions,
 *        such as frame dropping
 *
 * @param[in]  handle       Video capture source element handle
 * @param[in]  sync_handle  Capture synchronization handle
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t capture_video_src_el_set_sync_handle(esp_gmf_element_handle_t handle, esp_capture_sync_handle_t sync_handle);

/**
 * @brief  Set video source interface
 *
 * @note  This function can only be called before the element starts running
 *
 * @param[in]  handle   Video capture source element handle
 * @param[in]  vsrc_if  Video source interface to set
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t capture_video_src_el_set_src_if(esp_gmf_element_handle_t handle, esp_capture_video_src_if_t *vsrc_if);

/**
 * @brief  Negotiate video source capabilities
 *
 * @note  This function can only be called before the element starts running
 *        The negotiation process first matches the format, then other parameters like width, height, and frame rate
 *        If the requested format is not supported, will try to negotiate with ESP_CAPTURE_FMT_ID_ANY, and the
 *        video source should report its preferred supported format
 *
 * @param[in]   handle     Video capture source element handle
 * @param[in]   nego_info  Video information to negotiate
 * @param[out]  res_info   Pointer to store negotiated result
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - Others                   Failed to negotiate video capabilities
 */
esp_gmf_err_t capture_video_src_el_negotiate(esp_gmf_element_handle_t handle, esp_capture_video_info_t *nego_info,
                                             esp_capture_video_info_t *res_info);

/**
 * @brief  Enable/disable single-frame fetch mode for video source
 *
 * @param[in]  handle  Video capture source element handle
 * @param[in]  enable  Whether fetch only one frame
 *                       true: Capture terminates after one frame
 *                       false: Continuous streaming
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_video_src_set_single_fetch(esp_gmf_element_handle_t handle, bool enable);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
