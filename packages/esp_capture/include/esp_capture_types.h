/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
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
 * @brief  Capture FourCC definition
 */
#define ESP_CAPTURE_4CC(a, b, c, d) ((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

/**
 * @brief  Capture error code
 */
typedef enum {
    ESP_CAPTURE_ERR_OK            = 0,   /*!< No error */
    ESP_CAPTURE_ERR_INVALID_ARG   = -1,  /*!< Invalid argument error */
    ESP_CAPTURE_ERR_NO_MEM        = -2,  /*!< Not enough memory error */
    ESP_CAPTURE_ERR_NOT_SUPPORTED = -3,  /*!< Not supported error */
    ESP_CAPTURE_ERR_NOT_FOUND     = -4,  /*!< Not found error */
    ESP_CAPTURE_ERR_NOT_ENOUGH    = -5,  /*!< Not enough error */
    ESP_CAPTURE_ERR_TIMEOUT       = -6,  /*!< Run timeout error */
    ESP_CAPTURE_ERR_INVALID_STATE = -7,  /*!< Invalid state error */
    ESP_CAPTURE_ERR_INTERNAL      = -8,  /*!< Internal error */
    ESP_CAPTURE_ERR_NO_RESOURCES  = -9,  /*!< No resource any more error */
} esp_capture_err_t;

/**
 * @brief  Capture format identification definition
 *
 * @note  Aligned with GMF FourCC definition for audio video codecs and formats
 *        Detailed info refer to `https://github.com/espressif/esp-gmf/blob/main/gmf_core/helpers/include/esp_fourcc.h`
 */
typedef enum {
    ESP_CAPTURE_FMT_ID_NONE        = 0,  /*!< Invalid format */
    /*!< Audio codecs */
    ESP_CAPTURE_FMT_ID_PCM         = ESP_CAPTURE_4CC('P', 'C', 'M', ' '),  /*!< Audio PCM format */
    ESP_CAPTURE_FMT_ID_G711A       = ESP_CAPTURE_4CC('A', 'L', 'A', 'W'),  /*!< Audio G711-ALaw format */
    ESP_CAPTURE_FMT_ID_G711U       = ESP_CAPTURE_4CC('U', 'L', 'A', 'W'),  /*!< Audio G711-ULaw format */
    ESP_CAPTURE_FMT_ID_OPUS        = ESP_CAPTURE_4CC('O', 'P', 'U', 'S'),  /*!< Audio OPUS format */
    ESP_CAPTURE_FMT_ID_AAC         = ESP_CAPTURE_4CC('A', 'A', 'C', ' '),  /*!< Audio AAC format */
    /*!< Video codecs */
    ESP_CAPTURE_FMT_ID_H264        = ESP_CAPTURE_4CC('H', '2', '6', '4'),  /*!< Video H264 format */
    ESP_CAPTURE_FMT_ID_MJPEG       = ESP_CAPTURE_4CC('M', 'J', 'P', 'G'),  /*!< Video JPEG format */
    ESP_CAPTURE_FMT_ID_RGB565      = ESP_CAPTURE_4CC('R', 'G', 'B', 'L'),  /*!< Video RGB565 format */
    ESP_CAPTURE_FMT_ID_RGB565_BE   = ESP_CAPTURE_4CC('R', 'G', 'B', 'B'),  /*!< Video RGB565 format */
    ESP_CAPTURE_FMT_ID_RGB888      = ESP_CAPTURE_4CC('R', 'G', 'B', '3'),  /*!< Video RGB888 format */
    ESP_CAPTURE_FMT_ID_BGR888      = ESP_CAPTURE_4CC('B', 'G', 'R', '3'),  /*!< Video BGR888 format */
    ESP_CAPTURE_FMT_ID_YUV420      = ESP_CAPTURE_4CC('Y', 'U', '1', '2'),  /*!< Video YUV420 progressive format */
    ESP_CAPTURE_FMT_ID_YUV422P     = ESP_CAPTURE_4CC('4', '2', '2', 'P'),  /*!< Video YUV422 progressive format */
    ESP_CAPTURE_FMT_ID_YUV422      = ESP_CAPTURE_4CC('Y', 'U', 'Y', 'V'),  /*!< Video YUV422 format */
    ESP_CAPTURE_FMT_ID_O_UYY_E_VYY = ESP_CAPTURE_4CC('O', 'U', 'E', 'V'),  /*!< Video format for repeat pattern
                                                                                odd line uyyuyy... even line vyyvyy... */
    ESP_CAPTURE_FMT_ID_ANY         = 0xFFFF,                               /*!< Any video or audio format
                                                                                Used as a fallback when format negotiation fails
                                                                                to try any supported format as a last resort */
} esp_capture_format_id_t;

/**
 * @brief  Capture stream type
 */
typedef enum {
    ESP_CAPTURE_STREAM_TYPE_NONE  = 0,  /*!< None stream type */
    ESP_CAPTURE_STREAM_TYPE_AUDIO = 1,  /*!< Audio stream type */
    ESP_CAPTURE_STREAM_TYPE_VIDEO = 2,  /*!< Video stream type */
    ESP_CAPTURE_STREAM_TYPE_MUXER = 3,  /*!< Mux stream type */
} esp_capture_stream_type_t;

/**
 * @brief  Capture sync mode
 */
typedef enum {
    ESP_CAPTURE_SYNC_MODE_NONE,    /*!< Audio and video without sync */
    ESP_CAPTURE_SYNC_MODE_SYSTEM,  /*!< Audio and video synced with system time */
    ESP_CAPTURE_SYNC_MODE_AUDIO,   /*!< Video sync follow audio */
} esp_capture_sync_mode_t;

/**
 * @brief  Capture stream frame information
 */
typedef struct {
    esp_capture_stream_type_t  stream_type;  /*!< Capture stream type */
    uint32_t                   pts;          /*!< Stream frame presentation timestamp (unit ms) */
    uint8_t                   *data;         /*!< Stream frame data pointer */
    int                        size;         /*!< Stream frame data size */
} esp_capture_stream_frame_t;

/**
 * @brief  Capture audio information
 */
typedef struct {
    esp_capture_format_id_t  format_id;        /*!< Audio format */
    uint32_t                 sample_rate;      /*!< Audio sample rate */
    uint8_t                  channel;          /*!< Audio channel */
    uint8_t                  bits_per_sample;  /*!< Audio bits per sample */
} esp_capture_audio_info_t;

/**
 * @brief  Capture video information
 */
typedef struct {
    esp_capture_format_id_t  format_id;  /*!< Video format */
    uint16_t                 width;      /*!< Video width */
    uint16_t                 height;     /*!< Video height */
    uint8_t                  fps;        /*!< Video frames per second */
} esp_capture_video_info_t;

/**
 * @brief  Capture stream information
 */
typedef union {
    esp_capture_audio_info_t  audio_info;  /*!< Audio stream information */
    esp_capture_video_info_t  video_info;  /*!< Video stream information */
} esp_capture_stream_info_t;

/**
 * @brief  Capture region definition
 */
typedef struct {
    uint16_t  x;      /*!< X position of the region (in pixels) */
    uint16_t  y;      /*!< Y position of the region (in pixels) */
    uint16_t  width;  /*!< Region width (in pixels) */
    uint16_t  height; /*!< Region height (in pixels) */
} esp_capture_rgn_t;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
