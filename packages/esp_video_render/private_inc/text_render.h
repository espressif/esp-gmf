/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_video_render_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Text render context handle
 */
typedef void *text_render_handle_t;

/**
 * @brief  Text render configuration
 */
typedef struct {
    const char             *font_path;      /*!< Path to font file (NULL if using font_mem) */
    const uint8_t          *font_mem;       /*!< Font data in memory (NULL if using font_path) */
    int                     font_mem_size;  /*!< Size of font data in memory (0 if using font_path) */
    const char             *font_name;      /*!< Font name (for logging/identification) */
    int                     font_size;      /*!< Font size in pixels */
    esp_video_render_clr_t  text_color;     /*!< Text color */
    esp_video_render_clr_t  bg_color;       /*!< Background color (if bg_fill is true) */
    bool                    bg_fill;        /*!< Whether to fill background */
    bool                    antialiasing;   /*!< Enable antialiasing (default: true) */
} text_render_cfg_t;

/**
 * @brief  Text metrics
 */
typedef struct {
    int  width;      /*!< Text width in pixels */
    int  height;     /*!< Text height in pixels */
    int  baseline;   /*!< Baseline position */
    int  advance_x;  /*!< X advance */
    int  advance_y;  /*!< Y advance */
} text_render_metrics_t;

/**
 * @brief  Initialize text renderer
 *
 * @param[in]   cfg     Configuration for text renderer
 * @param[out]  handle  Text renderer handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 *
 * @note  If font_name is provided, will use shared font resource (reusable across widgets).
 *        Otherwise, creates a direct font instance (legacy mode).
 */
esp_video_render_err_t text_render_init(const text_render_cfg_t *cfg, text_render_handle_t *handle);

/**
 * @brief  Initialize text renderer with shared font resource
 *
 * @param[in]   font_resource  Shared font resource handle (from text_font_resource_create)
 * @param[in]   cfg            Configuration for text renderer (colors, etc.)
 * @param[out]  handle         Text renderer handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t text_render_init_with_resource(void *font_resource, const text_render_cfg_t *cfg, text_render_handle_t *handle);

/**
 * @brief  Deinitialize text renderer
 *
 * @param[in]  handle  Text renderer handle
 */
void text_render_deinit(text_render_handle_t handle);

/**
 * @brief  Set font size
 *
 * @param[in]  handle  Text renderer handle
 * @param[in]  size    Font size in pixels
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t text_render_set_font_size(text_render_handle_t handle, int size);

/**
 * @brief  Set text color
 *
 * @param[in]  handle  Text renderer handle
 * @param[in]  color   Text color
 */
void text_render_set_text_color(text_render_handle_t handle, esp_video_render_clr_t *color);

/**
 * @brief  Set background color
 *
 * @param[in]  handle  Text renderer handle
 * @param[in]  color   Background color
 * @param[in]  fill    Whether to fill background
 */
void text_render_set_bg_color(text_render_handle_t handle, esp_video_render_clr_t *color, bool fill);

/**
 * @brief  Calculate text metrics for a string
 *
 * @param[in]   handle   Text renderer handle
 * @param[in]   text     UTF-8 text string
 * @param[out]  metrics  Text metrics to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t text_render_get_metrics(text_render_handle_t handle, const char *text, text_render_metrics_t *metrics);

/**
 * @brief  Render text to framebuffer
 *
 * @param[in]  handle  Text renderer handle
 * @param[in]  text    UTF-8 text string
 * @param[in]  buffer  Framebuffer buffer
 * @param[in]  format  Framebuffer format
 * @param[in]  width   Buffer width
 * @param[in]  height  Buffer height
 * @param[in]  pitch   Buffer pitch (0 = auto-calculate)
 * @param[in]  x       X position
 * @param[in]  y       Y position (baseline)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t text_render_draw(text_render_handle_t handle,
                                        const char *text,
                                        uint8_t *buffer,
                                        esp_video_render_format_t format,
                                        int width,
                                        int height,
                                        int pitch,
                                        int x,
                                        int y);

/**
 * @brief  Render text to framebuffer with a viewport (clip rect)
 *
 * @note  The viewport is expressed in the same coordinate space as the destination buffer
 *        (0..width-1, 0..height-1). Only pixels inside the viewport will be drawn.
 *
 * @param[in]  handle    Text renderer handle
 * @param[in]  text      UTF-8 text string
 * @param[in]  buffer    Framebuffer buffer
 * @param[in]  format    Framebuffer format
 * @param[in]  width     Buffer width
 * @param[in]  height    Buffer height
 * @param[in]  pitch     Buffer pitch (0 = auto-calculate)
 * @param[in]  x         X position
 * @param[in]  y         Y position (baseline)
 * @param[in]  viewport  Clip rectangle (NULL = full buffer)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t text_render_draw_viewport(text_render_handle_t handle,
                                                 const char *text,
                                                 uint8_t *buffer,
                                                 esp_video_render_format_t format,
                                                 int width,
                                                 int height,
                                                 int pitch,
                                                 int x,
                                                 int y,
                                                 const esp_video_render_rect_t *viewport);

/**
 * @brief  Render text with shadow
 *
 * @param[in]  handle           Text renderer handle
 * @param[in]  text             UTF-8 text string
 * @param[in]  buffer           Framebuffer buffer
 * @param[in]  format           Framebuffer format
 * @param[in]  width            Buffer width
 * @param[in]  height           Buffer height
 * @param[in]  pitch            Buffer pitch (0 = auto-calculate)
 * @param[in]  x                X position
 * @param[in]  y                Y position (baseline)
 * @param[in]  shadow_color     Shadow color
 * @param[in]  shadow_offset_x  Shadow X offset
 * @param[in]  shadow_offset_y  Shadow Y offset
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t text_render_draw_with_shadow(text_render_handle_t handle,
                                                    const char *text,
                                                    uint8_t *buffer,
                                                    esp_video_render_format_t format,
                                                    int width,
                                                    int height,
                                                    int pitch,
                                                    int x,
                                                    int y,
                                                    esp_video_render_clr_t *shadow_color,
                                                    int shadow_offset_x,
                                                    int shadow_offset_y);

/**
 * @brief  Render text with shadow, clipped to viewport
 *
 * @note  The viewport is expressed in the same coordinate space as the destination buffer.
 *
 * @param[in]  viewport  Clip rectangle (NULL = full buffer)
 */
esp_video_render_err_t text_render_draw_with_shadow_viewport(text_render_handle_t handle,
                                                             const char *text,
                                                             uint8_t *buffer,
                                                             esp_video_render_format_t format,
                                                             int width,
                                                             int height,
                                                             int pitch,
                                                             int x,
                                                             int y,
                                                             esp_video_render_clr_t *shadow_color,
                                                             int shadow_offset_x,
                                                             int shadow_offset_y,
                                                             const esp_video_render_rect_t *viewport);

/**
 * @brief  Draw a single Unicode codepoint (fast path, avoids UTF-8 decode/memcpy in hot loops)
 *
 * @note  x/y are baseline coordinates (same convention as text_render_draw_*).
 */
esp_video_render_err_t text_render_draw_codepoint_viewport(text_render_handle_t handle,
                                                           uint32_t codepoint,
                                                           uint8_t *buffer,
                                                           esp_video_render_format_t format,
                                                           int width,
                                                           int height,
                                                           int pitch,
                                                           int x,
                                                           int y,
                                                           const esp_video_render_rect_t *viewport);

/**
 * @brief  Draw a single Unicode codepoint with shadow (fast path)
 */
esp_video_render_err_t text_render_draw_codepoint_with_shadow_viewport(text_render_handle_t handle,
                                                                       uint32_t codepoint,
                                                                       uint8_t *buffer,
                                                                       esp_video_render_format_t format,
                                                                       int width,
                                                                       int height,
                                                                       int pitch,
                                                                       int x,
                                                                       int y,
                                                                       esp_video_render_clr_t *shadow_color,
                                                                       int shadow_offset_x,
                                                                       int shadow_offset_y,
                                                                       const esp_video_render_rect_t *viewport);

/**
 * @brief  Get character width (advance) - approximate version for size calculation
 *
 * @param[in]  handle     Text renderer handle
 * @param[in]  codepoint  Unicode codepoint
 *
 * @return
 *       - Character  width in pixels (approximate, safe for frequent calls)
 */
int text_render_get_char_width(text_render_handle_t handle, uint32_t codepoint);

/**
 * @brief  Get character advance width with real FreeType metrics (for rendering)
 *
 * @note  This function loads glyphs and may cause heap issues if called too frequently.
 *        Use only during rendering, not during size calculations.
 *
 * @param[in]  handle     Text renderer handle
 * @param[in]  codepoint  Unicode codepoint
 *
 * @return
 *       - Character  advance width in pixels
 */
int text_render_get_char_advance_real(text_render_handle_t handle, uint32_t codepoint);

/**
 * @brief  Get the actual font size in pixels used by this renderer
 *
 * @note  For bitmap-only fonts (CBDT/CBLC, sbix), the requested size may be
 *        adjusted to the closest fixed strike size.
 *
 * @param[in]  handle  Text renderer handle
 *
 * @return
 *       - Font  size in pixels (0 if invalid)
 */
int text_render_get_font_size(text_render_handle_t handle);

/**
 * @brief  Check if FreeType is available
 *
 * @return
 *       - true   FreeType is available
 *       - false  FreeType is not available
 */
bool text_render_is_available(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
