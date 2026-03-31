/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_element.h"
#include "esp_gmf_video_types.h"
#include "esp_gmf_video_methods_def.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  This file provides general parameter setting functions for common video processing using GMF method
 *
 *         These functions simplify setting parameters for video processing elements,
 *         handling the underlying method calls. They do not enforce a specific order
 *         or argument count, but apply the basic settings where supported. Refer to
 *         the individual element APIs for details on supported settings and timing.
 */

/**
 * @brief  Set destination format for video element (e.g., color converter, video decoder)
 *
 * @param[in]  handle   Video element handle
 * @param[in]  dst_fmt  Destination format
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_set_dst_format(esp_gmf_element_handle_t handle, uint32_t dst_fmt);

/**
 * @brief  Set frame rate for video element (mainly for FPS converter)
 *
 * @param[in]  handle  Video element handle
 * @param[in]  fps     Destination frame rate
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_set_fps(esp_gmf_element_handle_t handle, uint16_t fps);

/**
 * @brief  Set destination resolution for video element (mainly for video scaler)
 *
 * @param[in]  handle  Video element handle
 * @param[in]  res     Destination resolution
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_set_dst_resolution(esp_gmf_element_handle_t handle, esp_gmf_video_resolution_t *res);

/**
 * @brief  Set destination codec for video element (e.g., video encoder)
 *
 * @param[in]  handle     Video element handle
 * @param[in]  dst_codec  Destination codec
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_set_dst_codec(esp_gmf_element_handle_t handle, uint32_t dst_codec);

/**
 * @brief  Preset source video information and destination codec for video encoder
 *
 * @note  In special cases call this API then query some information from video encoder before element running
 *
 * @param[in]  handle     Video element handle
 * @param[in]  vid_info   Source video information
 * @param[in]  dst_codec  Destination codec
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_venc_preset(esp_gmf_element_handle_t handle, esp_gmf_info_video_t *vid_info,
                                              uint32_t dst_codec);

/**
 * @brief  Get supported source formats by codec for video element (mainly for video encoder)
 *
 * @param[in]  handle        Video element handle
 * @param[in]  dst_codec     Destination codec
 * @param[in]  src_fmts      Pointer to store source formats array
 * @param[in]  src_fmts_num  Pointer to store source formats number
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_get_src_fmts_by_codec(esp_gmf_element_handle_t handle, uint32_t dst_codec,
                                                        const uint32_t **src_fmts, uint8_t *src_fmts_num);

/**
 * @brief  Set source codec for video element (e.g., video decoder)
 *
 * @param[in]  handle     Video element handle
 * @param[in]  src_codec  Source codec
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_set_src_codec(esp_gmf_element_handle_t handle, uint32_t src_codec);

/**
 * @brief  Set the rotation angle for the video element (mainly for video rotator)
 *
 * @param[in]  handle  Video element handle
 * @param[in]  degree  Rotation angle in degrees (e.g., 0, 90, 180, 270)
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_set_rotate_angle(esp_gmf_element_handle_t handle, uint16_t degree);

/**
 * @brief  Set the cropped region for video element (mainly for video cropper)
 *
 * @param[in]  handle  Video element handle
 * @param[in]  rgn     Kept region after crop
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_set_cropped_region(esp_gmf_element_handle_t handle, esp_gmf_video_rgn_t *rgn);

/**
 * @brief  Enable overlay for video element (mainly for video overlay)
 *
 * @param[in]  handle  Video element handle
 * @param[in]  enable  Whether enable video overlay or not
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_overlay_enable(esp_gmf_element_handle_t self, bool enable);

/**
 * @brief  Set overlay port for video element (mainly for video overlay)
 *
 * @param[in]  handle  Video element handle
 * @param[in]  port    Handle to the input GMF port providing overlay data
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_set_overlay_port(esp_gmf_element_handle_t self, void *port);

/**
 * @brief  Set overlay region information for video element (mainly for video overlay)
 *
 * @param[in]  handle  Video element handle
 * @param[in]  rgn     Overlay region information
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_set_overlay_rgn(esp_gmf_element_handle_t self, esp_gmf_overlay_rgn_info_t *rgn);

/**
 * @brief  Get supported destination formats by codec for video element (mainly for video decoder)
 *
 * @param[in]  handle        Video element handle
 * @param[in]  src_codec     Source codec
 * @param[in]  src_fmts      Pointer to store source formats array
 * @param[in]  src_fmts_num  Pointer to store source formats number
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_NOT_FOUND    Not found the method
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 *       - Others                   Failed to apply method
 */
esp_gmf_err_t esp_gmf_video_param_get_dst_fmts_by_codec(esp_gmf_element_handle_t handle, uint32_t src_codec,
                                                        const uint32_t **dst_fmts, uint8_t *dst_fmts_num);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
