/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_gmf_video_types.h"
#include "esp_video_render_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define GET_RGB565_R(x)       (((((x) >> 11) & 0x1F) << 3) | (((x) >> 8) & 0x07))
#define GET_RGB565_G(x)       (((((x) >> 5) & 0x3F) << 2) | (((x) >> 3) & 0x03))
#define GET_RGB565_B(x)       ((((x) & 0x1F) << 3) | (((x) & 0x1C) >> 2))
#define SWAP_EDIAN(x)         (((x) << 8) | ((x) >> 8))
#define CLAMP(x)              ((x) < 0 ? 0 : ((x) > 255 ? 255 : (x)))
#define RGB565_PACK(r, g, b)  ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

/**
 * @brief  Pattern info
 */
typedef struct {
    uint32_t                    format_id;  /*!< Format ID */
    esp_gmf_video_resolution_t  res;        /*!< Resolution */
    uint8_t                    *pixel;      /*!< Pixel buffer */
    uint32_t                    data_size;  /*!< Data size */
    bool                        vertical;   /*!< Vertical */
    uint8_t                     bar_count;  /*!< Bar count */
} pattern_info_t;

/**
 * @brief  Draw context
 */
typedef struct {
    uint8_t  *buffer;  /*!< Pixel buffer */
    uint32_t  pitch;   /*!< Pitch */
    uint32_t  format;  /*!< Format */
    uint16_t  width;   /*!< Width */
    uint16_t  height;  /*!< Height */
} draw_ctx_t;

/**
 * @brief  Generate a color bar pattern
 *
 * @param  info  Pattern info with format, resolution, and pixel buffer
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int gen_pattern_color_bar(pattern_info_t *info);

/**
 * @brief  Generate a circle pattern with background and foreground colors
 *
 * @param  ctx       Draw context
 * @param  bg_color  Background color (RGB888)
 * @param  fg_color  Foreground color (RGB888) for the circle
 * @param  radius    Circle radius (uses min(width, height)/2 - 1 if 0)
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int draw_circle(draw_ctx_t *ctx, esp_video_render_clr_t *bg_color, esp_video_render_clr_t *fg_color, int radius);

/**
 * @brief  Draw a line with specified width and pitch support
 *
 * @param  ctx         Draw context
 * @param  x1          Start X coordinate
 * @param  y1          Start Y coordinate
 * @param  x2          End X coordinate
 * @param  y2          End Y coordinate
 * @param  line_width  Line width in pixels (1 = single pixel line)
 * @param  color       RGB888 color for the line
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int draw_line(draw_ctx_t *ctx,
              int x1, int y1, int x2, int y2, int line_width, esp_video_render_clr_t *color);

/**
 * @brief  Generate a raw frame
 *
 * @param  image      Image buffer
 * @param  vertical   Vertical
 * @param  bar_count  Bar count
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int gen_raw_frame(esp_video_render_img_t *image, bool vertical, int bar_count);

/**
 * @brief  Generate a circle image
 *
 * @param  image     Image buffer
 * @param  bg_color  Background color
 * @param  fg_color  Foreground color
 * @param  radius    Circle radius
 *
 * @return
 *       - 0  On success
 */
int gen_circle_image(esp_video_render_img_t *image, esp_video_render_clr_t *bg_color,
                     esp_video_render_clr_t *fg_color, int radius);
/**
 * @brief  Generate a image with pattern
 *
 * @note  If the image is encoded, the image will be generated as raw data
 *
 * @param  image      Image buffer
 * @param  vertical   Vertical
 * @param  bar_count  Bar count
 *
 * @return
 *       - 0   On success
 *       - -1  On failure
 */
int gen_image(esp_video_render_img_t *image, bool vertical, int bar_count);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
