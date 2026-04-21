/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Video render FourCC definition
 */
#define ESP_VIDEO_RENDER_4CC(a, b, c, d)  ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

/**
 * @brief  Video render error code
 */
typedef enum {
    ESP_VIDEO_RENDER_ERR_OK            = 0,   /*!< No error */
    ESP_VIDEO_RENDER_ERR_FAIL          = -1,  /*!< General failure error */
    ESP_VIDEO_RENDER_ERR_INVALID_ARG   = -2,  /*!< Invalid argument error */
    ESP_VIDEO_RENDER_ERR_NO_MEM        = -3,  /*!< Not enough memory error */
    ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED = -4,  /*!< Not supported error */
    ESP_VIDEO_RENDER_ERR_NOT_FOUND     = -5,  /*!< Not found error */
    ESP_VIDEO_RENDER_ERR_TIMEOUT       = -6,  /*!< Run timeout error */
    ESP_VIDEO_RENDER_ERR_INVALID_STATE = -7,  /*!< Invalid state error */
    ESP_VIDEO_RENDER_ERR_NO_RESOURCE   = -8,  /*!< No resource error */
} esp_video_render_err_t;

/**
 * @brief  Video render format
 */
typedef enum {
    ESP_VIDEO_RENDER_FORMAT_NONE        = 0,                                         /*!< Invalid video format */
    ESP_VIDEO_RENDER_FORMAT_H264        = ESP_VIDEO_RENDER_4CC('H', '2', '6', '4'),  /*!< Video H264 format */
    ESP_VIDEO_RENDER_FORMAT_MJPEG       = ESP_VIDEO_RENDER_4CC('M', 'J', 'P', 'G'),  /*!< Video JPEG format */
    ESP_VIDEO_RENDER_FORMAT_RGB565      = ESP_VIDEO_RENDER_4CC('R', 'G', 'B', 'L'),  /*!< Video RGB565 format */
    ESP_VIDEO_RENDER_FORMAT_RGB565_BE   = ESP_VIDEO_RENDER_4CC('R', 'G', 'B', 'B'),  /*!< Video RGB565 bigendian format */
    ESP_VIDEO_RENDER_FORMAT_RGB888      = ESP_VIDEO_RENDER_4CC('R', 'G', 'B', '3'),  /*!< Video RGB888 format */
    ESP_VIDEO_RENDER_FORMAT_BGR888      = ESP_VIDEO_RENDER_4CC('B', 'G', 'R', '3'),  /*!< Video BGR888 format */
    ESP_VIDEO_RENDER_FORMAT_YUV420P     = ESP_VIDEO_RENDER_4CC('Y', 'U', '1', '2'),  /*!< Video YUV420 progressive format */
    ESP_VIDEO_RENDER_FORMAT_YUV422P     = ESP_VIDEO_RENDER_4CC('4', '2', '2', 'P'),  /*!< Video YUV422 progressive format */
    ESP_VIDEO_RENDER_FORMAT_UYVY        = ESP_VIDEO_RENDER_4CC('U', 'Y', 'V', 'Y'),  /*!< Video UYVY format */
    ESP_VIDEO_RENDER_FORMAT_YUV422      = ESP_VIDEO_RENDER_4CC('Y', 'U', 'Y', 'V'),  /*!< Video YUV422 format */
    ESP_VIDEO_RENDER_FORMAT_O_UYY_E_VYY = ESP_VIDEO_RENDER_4CC('O', 'U', 'E', 'V'),  /*!< Video format for repeat pattern */
} esp_video_render_format_t;

/**
 * @brief  Video render frame information
 */
typedef struct {
    esp_video_render_format_t  format;  /*!< Video format */
    uint16_t                   width;   /*!< Frame width */
    uint16_t                   height;  /*!< Frame height */
    uint8_t                    fps;     /*!< Frame rate (frames per second) */
} esp_video_render_frame_info_t;

/**
 * @brief  Video render display information
 */
typedef struct {
    esp_video_render_format_t  format;  /*!< Display format */
    uint16_t                   width;   /*!< Display width */
    uint16_t                   height;  /*!< Display height */
} esp_video_render_disp_info_t;

/**
 * @brief  Video render position
 */
typedef struct {
    uint16_t  x;  /*!< X coordinate */
    uint16_t  y;  /*!< Y coordinate */
} esp_video_render_pos_t;

/**
 * @brief  Video render rectangle
 */
typedef struct {
    uint16_t  x;       /*!< X coordinate */
    uint16_t  y;       /*!< Y coordinate */
    uint16_t  width;   /*!< Rectangle width */
    uint16_t  height;  /*!< Rectangle height */
} esp_video_render_rect_t;

/**
 * @brief  Dirty rectangle with opacity information
 *         Tracing opaque information to avoid unnecessary background refillment
 */
typedef struct {
    esp_video_render_rect_t  rect;    /*!< Dirty rectangle */
    bool                     opaque;  /*!< Whether the region is fully opaque after composition */
} esp_video_render_dirty_rect_t;

/**
 * @brief  Video render frame buffer
 */
typedef struct {
    esp_video_render_frame_info_t  info;  /*!< Frame information */
    uint8_t                       *data;  /*!< Frame buffer data */
    uint32_t                       size;  /*!< Data size in bytes */
} esp_video_render_fb_t;

/**
 * @brief  Video render frame
 */
typedef struct {
    esp_video_render_format_t  format;  /*!< Video format */
    uint16_t                   width;   /*!< Frame width */
    uint16_t                   height;  /*!< Frame height */
    uint8_t                   *data;    /*!< Frame data */
    uint32_t                   size;    /*!< Data size in bytes */
} esp_video_render_frame_t;

/**
 * @brief  Video render image
 */
typedef struct {
    esp_video_render_frame_info_t  info;  /*!< Image information */
    uint8_t                       *data;  /*!< Image data */
    uint32_t                       size;  /*!< Data size in bytes */
} esp_video_render_img_t;

/**
 * @brief  Video render color
 */
typedef struct {
    uint8_t  r;  /*!< Red component (0-255) */
    uint8_t  g;  /*!< Green component (0-255) */
    uint8_t  b;  /*!< Blue component (0-255) */
} esp_video_render_clr_t;

/**
 * @brief  Video render task configuration
 */
typedef struct {
    uint32_t  stack_size;  /*!< Task stack size */
    uint8_t   priority;    /*!< Task priority */
    uint8_t   core_id;     /*!< Task core id */
} esp_video_render_task_cfg_t;

/**
 * @brief  Video render overlay handle
 */
typedef void *esp_vui_overlay_handle_t;

/**
 * @brief  Video render container handle
 */
typedef void *esp_vui_container_handle_t;

/**
 * @brief  Video render blender handle
 */
typedef void *esp_video_render_blend_handle_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
