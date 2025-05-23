/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_overlay_if.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Text overlay implementation for ESP Capture
 *
 * @note  This file implements the overlay interface for rendering text overlays on video
 *        streams within the ESP Capture. It provides text rendering capabilities
 *        with support for multiple font sizes and colors.
 *
 *        Key Features:
 *          - Provides overlay interface implementation for text rendering on video streams
 *          - Supports multiple font sizes (12, 16, 20, 24, 28, 32, 36, 40, 44, 48 pixels)
 *          - Handles text drawing with configurable colors and positions
 *          - Manages overlay regions with boundary checking and overflow protection
 *          - Implements thread-safe drawing operations with mutex protection
 *          - Supports formatted text output with variable arguments
 *        Limitations:
 *          - Currently only support RGB565 format
 */

/**
 * @brief  Color definition in RGB565 format
 */
#define COLOR_RGB565_BLACK       (0x0000)
#define COLOR_RGB565_NAVY        (0x000F)
#define COLOR_RGB565_DARKGREEN   (0x03E0)
#define COLOR_RGB565_DARKCYAN    (0x03EF)
#define COLOR_RGB565_MAROON      (0x7800)
#define COLOR_RGB565_PURPLE      (0x780F)
#define COLOR_RGB565_OLIVE       (0x7BE0)
#define COLOR_RGB565_LIGHTGREY   (0xC618)
#define COLOR_RGB565_DARKGREY    (0x7BEF)
#define COLOR_RGB565_BLUE        (0x001F)
#define COLOR_RGB565_GREEN       (0x07E0)
#define COLOR_RGB565_CYAN        (0x07FF)
#define COLOR_RGB565_RED         (0xF800)
#define COLOR_RGB565_MAGENTA     (0xF81F)
#define COLOR_RGB565_YELLOW      (0xFFE0)
#define COLOR_RGB565_WHITE       (0xFFFF)
#define COLOR_RGB565_ORANGE      (0xFD20)
#define COLOR_RGB565_GREENYELLOW (0xAFE5)
#define COLOR_RGB565_PINK        (0xF81F)
#define COLOR_RGB565_SILVER      (0xC618)
#define COLOR_RGB565_GRAY        (0x8410)
#define COLOR_RGB565_LIME        (0x07E0)
#define COLOR_RGB565_TEAL        (0x0410)
#define COLOR_RGB565_FUCHSIA     (0xF81F)
#define COLOR_RGB565_ESP_BKGD    (0xD185)

/**
 * @brief  Capture text overlay draw information
 */
typedef struct {
    uint16_t  color;      /*!< Draw color */
    uint16_t  font_size;  /*!< Draw font size, font for this size must be loaded use `menuconfig` */
    uint16_t  x;          /*!< Draw x position inside of the text overlay */
    uint16_t  y;          /*!< Draw y position inside of the text overlay */
} esp_capture_text_overlay_draw_info_t;

/**
 * @brief  Create text overlay instance
 *
 * @note  The text overlay must be smaller than the main capture frame size
 *        The region's (x, y) position represents the top-left corner of the text overlay
 *        relative to the capture frame
 *        The region's width and height define the size of the text overlay, where the
 *        text is drawn within this region
 *
 * @param[in]  rgn  Text overlay region setting
 *
 * @return
 *       - NULL    No enough memory to hold text overlay instance
 *       - Others  Text overlay instance
 */
esp_capture_overlay_if_t *esp_capture_new_text_overlay(esp_capture_rgn_t *rgn);

/**
 * @brief  Draw text start
 *
 * @note  Drawing should occur between `esp_capture_text_overlay_draw_start` and `esp_capture_text_overlay_draw_finished`,
 *        to ensure the text overlay frame data is fully captured
 *        Multiple draw actions can be performed between these two functions
 *
 * @param[in]  h  Text overlay instance
 *
 * @return
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Not supported action for not open yet
 *       - ESP_CAPTURE_ERR_OK             Draw start success
 */
esp_capture_err_t esp_capture_text_overlay_draw_start(esp_capture_overlay_if_t *h);

/**
 * @brief  Draw text on text overlay
 *
 * @note  Supports drawing multiple lines of text separated by '\n'
 *        Each subsequent line will be aligned to the x position of the first line
 *
 * @param[in]  h     Text overlay instance
 * @param[in]  info  Drawing settings
 * @param[in]  str   String to be drawn
 *
 * @return
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Not supported action for not open yet or will overflow
 *       - ESP_CAPTURE_ERR_OK             Draw text success
 */
esp_capture_err_t esp_capture_text_overlay_draw_text(esp_capture_overlay_if_t *h,
                                                     esp_capture_text_overlay_draw_info_t *info, char *str);

/**
 * @brief  Draw text with a format similar to `sprintf` on the text overlay
 *
 * @note  Behavior is the same as `esp_capture_text_overlay_draw_text`
 *        The formatted string must not exceed CONFIG_ESP_PAINTER_FORMAT_SIZE_MAX bytes
 *
 * @param[in]    h     Text overlay instance
 * @param[in]    info  Drawing settings
 * @param[in]    fmt   String format
 * @param[in]    ...   Arguments to be formatted
 *
 * @return
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Not supported action, either not open yet or will overflow
 *       - ESP_CAPTURE_ERR_OK             Draw with format success
 */
esp_capture_err_t esp_capture_text_overlay_draw_text_fmt(esp_capture_overlay_if_t *h,
                                                         esp_capture_text_overlay_draw_info_t *info,
                                                         const char *fmt, ...);

/**
 * @brief  Indicate draw text finished
 *
 * @param[in]  h  Text overlay instance
 *
 * @return
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Not supported action for not open yet
 *       - ESP_CAPTURE_ERR_OK             Draw finished success
 */
esp_capture_err_t esp_capture_text_overlay_draw_finished(esp_capture_overlay_if_t *h);

/**
 * @brief  Clear a region on the text overlay
 *
 * @param[in]  h      Text overlay instance
 * @param[in]  rgn    Region to be cleared
 * @param[in]  color  Color used to clear the region
 *
 * @return
 *       - ESP_CAPTURE_ERR_NOT_SUPPORTED  Action not supported for not open yet
 *       - ESP_CAPTURE_ERR_INVALID_ARG    Region exceeds text overlay boundaries
 *       - ESP_CAPTURE_ERR_OK             Clear region success
 */
esp_capture_err_t esp_capture_text_overlay_clear(esp_capture_overlay_if_t *h, esp_capture_rgn_t *rgn, uint16_t color);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
