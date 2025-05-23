/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_element.h"
#include "esp_imgfx_rotate.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define DEFAULT_ESP_GMF_ROTATE_CONFIG() {          \
    .in_res = {                                    \
        .width  = 320,                             \
        .height = 240,                             \
    },                                             \
    .in_pixel_fmt = ESP_IMGFX_PIXEL_FMT_RGB565_LE, \
    .degree       = 0,                             \
}

/**
 * @brief  Initialize the video rotate element with the specified configuration
 *
 * @param[in]   config  Pointer to a `esp_imgfx_rotate_cfg_t` structure that contains the
 *                      configuration settings for the video rotate
 * @param[out]  handle  Pointer to a `esp_gmf_element_handle_t` where the initialized video rotate
 *                      handle will be stored
 *
 * @return
 *       - ESP_GMF_ERR_OK           Initialization successful
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid arguments
 *       - ESP_GMF_ERR_MEMORY_LACK  No memory for video rotate element
 */
esp_gmf_err_t esp_gmf_video_rotate_init(esp_imgfx_rotate_cfg_t *config, esp_gmf_element_handle_t *handle);

/**
 * @brief  Set video rotate rotation degree
 *
 * @note  This API have no special timing request, user can call it freely
 *
 * @param[in]  handle  Video video rotate handle
 * @param[in]  degree  Any angle, unit(one degree)
 *
 * @return
 *       - ESP_GMF_ERR_OK           Successful
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid arguments
 */
esp_gmf_err_t esp_gmf_video_rotate_set_rotation(esp_gmf_element_handle_t handle, uint16_t degree);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
