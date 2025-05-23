/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_element.h"
#include "esp_gmf_video_types.h"
#include "esp_imgfx_crop.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_ESP_GMF_CROP_CONFIG() {            \
    .in_res = {                                    \
        .width  = 320,                             \
        .height = 240,                             \
    },                                             \
    .in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE, \
    .cropped_res  = {                              \
         .width  = 160,                            \
         .height = 120,                            \
    },                                             \
    .x_pos = 0,                                    \
    .y_pos = 0,                                    \
}

/**
 * @brief  Initialize the video crop element with the specified configuration
 *
 * @param[in]   config  Pointer to a `esp_imgfx_crop_cfg_t` structure that contains the
 *                      configuration settings for the video crop
 * @param[out]  handle  Pointer to a `esp_gmf_element_handle_t` where the initialized video crop
 *                      handle will be stored
 *
 * @return
 *       - ESP_GMF_ERR_OK           Initialization successful
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_GMF_ERR_MEMORY_LACK  No memory for video crop element
 */
esp_gmf_err_t esp_gmf_video_crop_init(esp_imgfx_crop_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set video cropped region
 *
 * @note  This API have no special timing request, user can call it freely
 *
 * @param[in]  handle  Video video crop handle
 * @param[in]  rgn     Region to be kept in original video frame
 *
 * @return
 *       - ESP_GMF_ERR_OK           Successful
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid arguments
 */
esp_gmf_err_t esp_gmf_video_crop_rgn(esp_gmf_element_handle_t handle, esp_gmf_video_rgn_t *rgn);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
