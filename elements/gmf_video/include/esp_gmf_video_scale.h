/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_element.h"
#include "esp_gmf_video_types.h"
#include "esp_imgfx_scale.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_ESP_GMF_SCALE_CONFIG() {                     \
    .in_res = {                                              \
        .width  = 320,                                       \
        .height = 240,                                       \
    },                                                       \
    .in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE,           \
    .scale_res    = {                                        \
        .width  = 160,                                       \
        .height = 120,                                       \
    },                                                       \
    .filter_type = ESP_IMGFX_SCALE_FILTER_TYPE_DOWN_RESAMPLE \
}

/**
 * @brief  Initialize the video scale element with the specified configuration
 *
 * @param[in]   config  Pointer to a `esp_imgfx_scale_cfg_t` structure that contains the
 *                      configuration settings for the video scale
 * @param[out]  handle  Pointer to a `esp_gmf_element_handle_t` where the initialized video scale
 *                      handle will be stored
 *
 * @return
 *       - ESP_GMF_ERR_OK           Initialization successful
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_GMF_ERR_MEMORY_LACK  No memory for video scale element
 */
esp_gmf_err_t esp_gmf_video_scale_init(esp_imgfx_scale_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set video scale destination resolution
 *
 * @note  This API have no special timing request, user can call it freely
 *
 * @param[in]  handle  Video video scale handle
 * @param[in]  res     Video video scale output resolution
 *
 * @return
 *       - ESP_GMF_ERR_OK           Successful
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid arguments
 */
esp_gmf_err_t esp_gmf_video_scale_dst_resolution(esp_gmf_element_handle_t handle, esp_gmf_video_resolution_t *res);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
