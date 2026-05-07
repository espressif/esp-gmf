/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "video_render_utils.h"

#define DEFAULT_ALIGNMENT  64

bool video_render_is_encoded(esp_video_render_format_t format)
{
    return (format == ESP_VIDEO_RENDER_FORMAT_H264 || format == ESP_VIDEO_RENDER_FORMAT_MJPEG);
}

uint8_t video_render_get_pixel_bits(esp_video_render_format_t format)
{
    switch (format) {
        case ESP_VIDEO_RENDER_FORMAT_RGB565:
        case ESP_VIDEO_RENDER_FORMAT_RGB565_BE:
            return 16;
        case ESP_VIDEO_RENDER_FORMAT_RGB888:
        case ESP_VIDEO_RENDER_FORMAT_BGR888:
            return 24;
        case ESP_VIDEO_RENDER_FORMAT_YUV420P:
            return 12;  // Y=8, U/V=2 each
        case ESP_VIDEO_RENDER_FORMAT_YUV422P:
        case ESP_VIDEO_RENDER_FORMAT_YUV422:
        case ESP_VIDEO_RENDER_FORMAT_O_UYY_E_VYY:
            return 16;
        default:
            return 0;  // Unknown or compressed formats
    }
}

uint32_t video_render_get_image_size(const esp_video_render_frame_info_t *info)
{
    uint8_t pixel_bits = video_render_get_pixel_bits(info->format);
    if (pixel_bits == 0) {
        return 0;
    }
    return info->width * info->height * pixel_bits / 8;
}

uint8_t video_render_get_default_alignment(void)
{
    return DEFAULT_ALIGNMENT;
}
