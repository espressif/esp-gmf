/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <string.h>
#include "video_render_sys.h"
#include "esp_video_render_types.h"

/**
 * @brief  Get number of elements in array
 */
#define ELEMS(m)  (sizeof(m) / sizeof(m[0]))

/**
 * @brief  Align up according to alignment
 */
#define ALIGN_UP(a, align)  (((a) + ((align) - 1)) & ~((align) - 1))

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Check if video format is encoded
 *
 * @param[in]  format  Video format
 *
 * @return
 *       - true   Format is encoded (e.g., H264, MJPEG)
 *       - false  Format is raw (e.g., RGB565, YUV420)
 */
bool video_render_is_encoded(esp_video_render_format_t format);

/**
 * @brief  Get pixel bits for video format
 *
 * @param[in]  format  Video format
 *
 * @return
 *       - Number  of bits per pixel
 *       - 0       if format is invalid
 */
uint8_t video_render_get_pixel_bits(esp_video_render_format_t format);

/**
 * @brief  Get image size in bytes
 *
 * @param[in]  info  Frame information
 *
 * @return
 *       - Image  size in bytes
 *       - 0      if format is invalid
 */
uint32_t video_render_get_image_size(const esp_video_render_frame_info_t *info);

/**
 * @brief  Get format string
 *
 * @param[in]  format  Format
 *
 * @return
 *       - String  representation of format
 */
const char *esp_gmf_video_get_format_string(uint32_t format);

/**
 * @brief  Convert format to string
 *
 * @param[in]  format  Frame information
 *
 * @return
 *       - String  representation of format
 */
#define video_render_format_to_string(format)  esp_gmf_video_get_format_string(format)

/**
 * @brief  Get default alignment
 *
 * @return
 *       - Default  alignment
 */
uint8_t video_render_get_default_alignment(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
