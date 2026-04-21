/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_video_render_backend.h"
#include "esp_video_render_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  SDL backend configuration
 *
 * @note  This configuration will be used when initializing the SDL backend
 */
typedef struct {
    esp_video_render_format_t  format;  /*!< Output pixel format */
    uint16_t                   width;   /*!< Display width */
    uint16_t                   height;  /*!< Display height */
} sdl_backend_cfg_t;

/**
 * @brief  Get SDL backend operations
 *
 * @return
 *       - NULL    On failure
 *       - Others  SDL backend operations
 */
const esp_video_render_backend_ops_t *esp_video_render_get_sdl_backend(void);

/**
 * @brief  Dump current SDL backend framebuffer to a JPEG file.
 *
 * @note  Requires `gst-launch-1.0` in PATH. This is emulation-only helper.
 *
 * @param[in]  jpg_path  output JPEG path
 *
 * @return
 *       - 0       On success
 *       - Others  On failure
 */
int sdl_backend_dump_jpg(const char *jpg_path);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
