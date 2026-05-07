/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_vui_widget.h"
#include "esp_vui_container.h"
#include "esp_video_render_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Initialize text widget
 *
 * @param[in]  container   Container handle
 * @param[in]  frame_info  Frame information
 * @param[in]  pos         Widget position
 * @param[in]  width       Widget width
 * @param[in]  height      Widget height
 *
 * @return
 *       - NULL    Not enough memory or invalid argument
 *       - Others  Widget pointer
 */
esp_vui_widget_t *esp_vui_text_widget_init(esp_vui_container_handle_t container,
                                           esp_video_render_frame_info_t *frame_info,
                                           esp_video_render_pos_t *pos,
                                           int width,
                                           int height);

/**
 * @brief  Set text content for text widget
 *
 * @param[in]  widget  Widget handle
 * @param[in]  text    Text string
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_set_text(esp_vui_widget_t *widget, const char *text);

/**
 * @brief  Set font renderer for text widget
 *
 * @param[in]  widget     Widget handle
 * @param[in]  renderer   Font renderer handle
 * @param[in]  font_size  Font size
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_set_font_renderer(esp_vui_widget_t *widget, void *renderer, int font_size);

/**
 * @brief  Set font from file path for text widget
 *
 * @param[in]  widget     Widget handle
 * @param[in]  font_path  Font file path
 * @param[in]  font_size  Font size
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NOT_FOUND    Font file not found
 */
esp_video_render_err_t esp_vui_text_widget_set_font(esp_vui_widget_t *widget, const char *font_path, int font_size);

/**
 * @brief  Set font from memory for text widget
 *
 * @param[in]  widget         Widget handle
 * @param[in]  font_name      Font name
 * @param[in]  font_mem       Font data in memory
 * @param[in]  font_mem_size  Font data size
 * @param[in]  font_size      Font size
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t esp_vui_text_widget_set_font_from_mem(esp_vui_widget_t *widget, const char *font_name,
                                                             const uint8_t *font_mem, int font_mem_size, int font_size);

/**
 * @brief  Set font resource for text widget
 *
 * @param[in]  widget         Widget handle
 * @param[in]  font_resource  Font resource handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_set_font_resource(esp_vui_widget_t *widget, void *font_resource);

/**
 * @brief  Set emoji font from file path for text widget
 *
 * @param[in]  widget     Widget handle
 * @param[in]  font_path  Emoji font file path
 * @param[in]  font_size  Font size
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NOT_FOUND    Font file not found
 */
esp_video_render_err_t esp_vui_text_widget_set_emoji_font(esp_vui_widget_t *widget, const char *font_path, int font_size);

/**
 * @brief  Set emoji font from memory for text widget
 *
 * @param[in]  widget         Widget handle
 * @param[in]  font_name      Emoji font name
 * @param[in]  font_mem       Emoji font data in memory
 * @param[in]  font_mem_size  Emoji font data size
 * @param[in]  font_size      Font size
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t esp_vui_text_widget_set_emoji_font_from_mem(esp_vui_widget_t *widget, const char *font_name,
                                                                   const uint8_t *font_mem, int font_mem_size, int font_size);

/**
 * @brief  Set emoji font resource for text widget
 *
 * @param[in]  widget         Widget handle
 * @param[in]  font_resource  Emoji font resource handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_set_emoji_font_resource(esp_vui_widget_t *widget, void *font_resource);

/**
 * @brief  Set text color for text widget
 *
 * @param[in]  widget  Widget handle
 * @param[in]  color   Text color
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_set_text_color(esp_vui_widget_t *widget, esp_video_render_clr_t *color);

/**
 * @brief  Set background color for text widget
 *
 * @param[in]  widget       Widget handle
 * @param[in]  color        Background color
 * @param[in]  transparent  True for transparent background, false for opaque
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_set_bg_color(esp_vui_widget_t *widget, esp_video_render_clr_t *color, bool transparent);

/**
 * @brief  Set text alignment for text widget
 *
 * @param[in]  widget   Widget handle
 * @param[in]  align_h  Horizontal alignment
 * @param[in]  align_v  Vertical alignment
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_set_align(esp_vui_widget_t *widget, int align_h, int align_v);

/**
 * @brief  Set text scrolling for text widget
 *
 * @param[in]  widget  Widget handle
 * @param[in]  enable  True to enable scrolling, false to disable
 * @param[in]  mode    Scroll mode
 * @param[in]  speed   Scroll speed
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_set_scroll(esp_vui_widget_t *widget, bool enable, int mode, int speed);

/**
 * @brief  Pause or resume text scrolling
 *
 * @param[in]  widget  Widget handle
 * @param[in]  pause   True to pause, false to resume
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_scroll_pause(esp_vui_widget_t *widget, bool pause);

/**
 * @brief  Set text shadow for text widget
 *
 * @param[in]  widget    Widget handle
 * @param[in]  enable    True to enable shadow, false to disable
 * @param[in]  color     Shadow color
 * @param[in]  offset_x  Shadow X offset
 * @param[in]  offset_y  Shadow Y offset
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_set_shadow(esp_vui_widget_t *widget, bool enable,
                                                      esp_video_render_clr_t *color, int offset_x, int offset_y);

/**
 * @brief  Set text overflow mode for text widget
 *
 * @param[in]  widget         Widget handle
 * @param[in]  overflow_mode  Overflow mode
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_set_overflow(esp_vui_widget_t *widget, int overflow_mode);

/**
 * @brief  Update text scrolling for text widget
 *
 * @param[in]  widget  Widget handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t esp_vui_text_widget_scroll_update(esp_vui_widget_t *widget);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
