/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_element.h"
#include "esp_imgfx_color_convert.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_ESP_GMF_COLOR_CONVERT_CONFIG() {        \
    .in_res = {                                         \
        .width  = 320,                                  \
        .height = 240 ,                                 \
    },                                                  \
    .in_pixel_fmt    = ESP_IMGFX_PIXEL_FMT_YUYV,        \
    .out_pixel_fmt   = ESP_IMGFX_PIXEL_FMT_RGB565_LE,   \
    .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601, \
}

/**
 * @brief  Initialize the video color convert element with the specified configuration
 *
 * @param[in]   config  Pointer to a color convert configuration
 * @param[out]  handle  Pointer to a `esp_gmf_element_handle_t` where the initialized video color convert
 *                      handle will be stored
 *
 * @return
 *       - ESP_GMF_ERR_OK           Initialization successful
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_GMF_ERR_MEMORY_LACK  No memory for video color convert element
 */
esp_gmf_err_t esp_gmf_video_color_convert_init(esp_imgfx_color_convert_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set video color convert destination format
 *
 * @note  This API have no special timing request, user can call it freely
 *
 * @param[in]  handle  Video color convert handle
 * @param[in]  format  Video color convert destination format (GMF FourCC representation of video format)
 *
 * @return
 *       - ESP_GMF_ERR_OK           Successful
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid arguments
 */
esp_gmf_err_t esp_gmf_video_color_convert_dst_format(esp_gmf_element_handle_t handle, uint32_t format);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
