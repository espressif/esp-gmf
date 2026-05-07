/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdlib.h>
#include "esp_vui_widget_default.h"
#include "video_render_utils.h"
#include "esp_video_render_blender.h"
#include "text_render.h"
#include "text_font_resource.h"
#include "esp_log.h"
#include "esp_video_render_types.h"
#include "esp_timer.h"
#include "video_render_compose.h"

#define TAG  "TEXT_WIDGET"

// Helper macros for RGB565
#define RGB565_PACK(r, g, b)  ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define SWAP_EDIAN(x)         (((x) << 8) | ((x) >> 8))

// Text alignment constants
#define TEXT_ALIGN_LEFT    0
#define TEXT_ALIGN_CENTER  1
#define TEXT_ALIGN_RIGHT   2
#define TEXT_ALIGN_TOP     0
#define TEXT_ALIGN_MIDDLE  1
#define TEXT_ALIGN_BOTTOM  2

// Scroll mode constants
#define SCROLL_MODE_NONE        0
#define SCROLL_MODE_HORIZONTAL  1
#define SCROLL_MODE_VERTICAL    2
#define SCROLL_MODE_CIRCULAR    3

// Overflow mode constants
#define OVERFLOW_CLIP    0
#define OVERFLOW_WRAP    1
#define OVERFLOW_SCROLL  2

typedef struct text_widget_ctx {
    esp_vui_widget_t       widget;  /*!< Widget handle */
    esp_video_render_fb_t  fb;      /*!< Widget framebuffer */
    bool                   gen_fb;  /*!< Whether to free fb */
    bool                   fresh;   /*!< Full redraw flag */

    // Text content
    char *text;           /*!< UTF-8 text string */
    int   text_len;       /*!< Text length in bytes */
    int   text_capacity;  /*!< Allocated capacity */

    // Font rendering
    text_render_handle_t  text_renderer;   /*!< Primary text renderer handle (for regular text) */
    text_render_handle_t  emoji_renderer;  /*!< Emoji renderer handle (for emoji characters, optional) */
    int                   font_size;       /*!< Font size */
    int                   line_height;     /*!< Line height */
    const char           *font_path;       /*!< Font file path */

    // Colors
    esp_video_render_clr_t  text_color;              /*!< Text color */
    esp_video_render_clr_t  bg_color;                /*!< Background color */
    bool                    bg_transparent;          /*!< Background transparency */
    esp_video_render_clr_t  applied_text_color;      /*!< Last applied renderer text color */
    esp_video_render_clr_t  applied_bg_color;        /*!< Last applied renderer bg color */
    bool                    applied_bg_transparent;  /*!< Last applied bg transparency */
    bool                    render_style_dirty;      /*!< Need sync style to renderer */

    // Alignment
    int  align_h;  /*!< Horizontal: left/center/right */
    int  align_v;  /*!< Vertical: top/middle/bottom */

    // Scrolling
    bool     scroll_enable;     /*!< Enable scrolling */
    int      scroll_mode;       /*!< horizontal/vertical/circular */
    int      scroll_pos;        /*!< Current scroll position (pixels) */
    int      scroll_speed;      /*!< Pixels per update */
    bool     scroll_paused;     /*!< Scroll pause state */
    int      scroll_total;      /*!< Total scrollable distance */
    int64_t  last_scroll_time;  /*!< Last scroll update time */

    // Text overflow
    int  overflow_mode;  /*!< clip/wrap/scroll */

    // Effects
    bool                    shadow_enable;    /*!< Text shadow */
    esp_video_render_clr_t  shadow_color;     /*!< Shadow color */
    int                     shadow_offset_x;  /*!< Shadow X offset */
    int                     shadow_offset_y;  /*!< Shadow Y offset */

    // Internal state
    int   text_width;     /*!< Calculated text width (pixels) */
    int   text_height;    /*!< Calculated text height (pixels) */
    bool  needs_refresh;  /*!< Text changed, needs refresh */
    int   wrap_lines;     /*!< Number of wrapped lines */
} text_widget_ctx_t;

// Helper function to fill background
static void fill_background(uint8_t *buffer, esp_video_render_format_t format,
                            int width, int height, int pitch,
                            esp_video_render_clr_t *color)
{
    if (buffer == NULL || color == NULL) {
        return;
    }
    uint8_t bytes_per_pixel = video_render_get_pixel_bits(format) >> 3;
    if (pitch == 0) {
        pitch = width * bytes_per_pixel;
    }

    uint16_t rgb565 = 0;
    if (format == ESP_VIDEO_RENDER_FORMAT_RGB565 || format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
        rgb565 = RGB565_PACK(color->r, color->g, color->b);
        if (format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
            rgb565 = SWAP_EDIAN(rgb565);
        }
    }
    for (int y = 0; y < height; y++) {
        uint8_t *row = buffer + y * pitch;
        for (int x = 0; x < width; x++) {
            uint8_t *pixel_ptr = row + x * bytes_per_pixel;
            switch (format) {
                case ESP_VIDEO_RENDER_FORMAT_RGB565:
                case ESP_VIDEO_RENDER_FORMAT_RGB565_BE: {
                    *((uint16_t *)pixel_ptr) = rgb565;
                } break;
                case ESP_VIDEO_RENDER_FORMAT_RGB888: {
                    pixel_ptr[0] = color->r;
                    pixel_ptr[1] = color->g;
                    pixel_ptr[2] = color->b;
                } break;
                case ESP_VIDEO_RENDER_FORMAT_BGR888: {
                    pixel_ptr[0] = color->b;
                    pixel_ptr[1] = color->g;
                    pixel_ptr[2] = color->r;
                } break;
                default:
                    break;
            }
        }
    }
}

// Helper: Calculate UTF-8 character width in bytes
static int utf8_char_width(const unsigned char *s)
{
    if ((*s & 0x80) == 0) {
        return 1;  // ASCII
    }
    if ((*s & 0xE0) == 0xC0) {
        return 2;  // 2-byte UTF-8
    }
    if ((*s & 0xF0) == 0xE0) {
        return 3;  // 3-byte UTF-8
    }
    if ((*s & 0xF8) == 0xF0) {
        return 4;  // 4-byte UTF-8 (emoji)
    }
    return 1;  // Invalid, treat as single byte
}

// Helper: Decode UTF-8 to codepoint
static uint32_t utf8_decode(const unsigned char **s)
{
    const unsigned char *p = *s;
    uint32_t codepoint = 0;
    uint8_t width = utf8_char_width(p);
    switch (width) {
        case 1:
            codepoint = *p;
            break;
        case 2:
            codepoint = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
            break;
        case 3:
            codepoint = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
            break;
        case 4:
            codepoint = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
            break;
    }
    *s += width;
    return codepoint;
}

// Forward declaration
static bool is_emoji_codepoint(uint32_t codepoint);

static inline bool text_widget_is_word_break(uint32_t codepoint)
{
    return (codepoint == ' ' || codepoint == '\t' || codepoint == '\r' || codepoint == '\n');
}

static int text_widget_get_codepoint_advance(text_widget_ctx_t *text, uint32_t codepoint)
{
    text_render_handle_t renderer = text->text_renderer;
    if (text->emoji_renderer != NULL && is_emoji_codepoint(codepoint)) {
        renderer = text->emoji_renderer;
    }
    if (renderer == NULL && text->emoji_renderer != NULL) {
        renderer = text->emoji_renderer;
    }
    // Use char width API for wrap/measurement path to avoid heavy glyph-load path churn.
    int adv = renderer ? text_render_get_char_width(renderer, codepoint) : text->font_size / 2;
    return adv > 0 ? adv : text->font_size / 2;
}

static int text_widget_measure_word_advance(text_widget_ctx_t *text,
                                            const unsigned char *p,
                                            const unsigned char *text_end)
{
    int width = 0;
    const unsigned char *iter = p;
    while (iter < text_end && *iter) {
        const unsigned char *cur = iter;
        uint32_t cp = utf8_decode(&iter);
        if (text_widget_is_word_break(cp)) {
            iter = cur;
            break;
        }
        width += text_widget_get_codepoint_advance(text, cp);
    }
    return width;
}

// Helper: Check if a Unicode codepoint is an emoji
// Emoji ranges based on Unicode standard
static bool is_emoji_codepoint(uint32_t codepoint)
{
    // Emoticons: U+1F600-U+1F64F
    if (codepoint >= 0x1F600 && codepoint <= 0x1F64F) {
        return true;
    }
    // Miscellaneous Symbols and Pictographs: U+1F300-U+1F5FF
    if (codepoint >= 0x1F300 && codepoint <= 0x1F5FF) {
        return true;
    }
    // Transport and Map Symbols: U+1F680-U+1F6FF
    if (codepoint >= 0x1F680 && codepoint <= 0x1F6FF) {
        return true;
    }
    // Supplemental Symbols and Pictographs: U+1F900-U+1F9FF
    if (codepoint >= 0x1F900 && codepoint <= 0x1F9FF) {
        return true;
    }
    // Symbols and Pictographs Extended-A: U+1FA00-U+1FAFF
    if (codepoint >= 0x1FA00 && codepoint <= 0x1FAFF) {
        return true;
    }
    // Dingbats: U+2700-U+27BF (some emoji-like symbols)
    if (codepoint >= 0x2700 && codepoint <= 0x27BF) {
        return true;
    }
    // Miscellaneous Symbols: U+2600-U+26FF (some emoji-like symbols)
    if (codepoint >= 0x2600 && codepoint <= 0x26FF) {
        return true;
    }
    // Enclosed characters: U+1F100-U+1F1FF (regional indicator symbols)
    if (codepoint >= 0x1F100 && codepoint <= 0x1F1FF) {
        return true;
    }

    return false;
}

// Calculate text dimensions using FreeType metrics (supports multi-font mode)
static void calculate_text_size(text_widget_ctx_t *text, int *width, int *height)
{
    if (text == NULL || text->text == NULL || text->text_len == 0) {
        *width = 0;
        *height = 0;
        return;
    }

    // Check if we have at least one renderer (text_renderer or emoji_renderer)
    if (text->text_renderer == NULL && text->emoji_renderer == NULL) {
        // Fallback if no renderer initialized
        *width = 0;
        *height = text->line_height;
        return;
    }

    bool multi_font_mode = (text->emoji_renderer != NULL);

    if (text->overflow_mode == OVERFLOW_WRAP) {
        // Calculate wrapped text dimensions using word boundary first strategy.
        int max_width = text->widget.rect.width;
        int lines = 1;
        int current_line_width = 0;
        bool prev_is_word_break = true;
        const unsigned char *p = (const unsigned char *)text->text;
        const unsigned char *text_end = (const unsigned char *)(text->text + text->text_len);

        while (p < text_end && *p) {
            const unsigned char *char_start = p;
            uint32_t codepoint = utf8_decode(&p);

            if (codepoint == '\n') {
                lines++;
                current_line_width = 0;
                prev_is_word_break = true;
                continue;
            }

            if (text_widget_is_word_break(codepoint)) {
                // Skip leading spaces on new visual line.
                if ((codepoint == ' ' || codepoint == '\t') && current_line_width == 0) {
                    prev_is_word_break = true;
                    continue;
                }
                current_line_width += text_widget_get_codepoint_advance(text, codepoint);
                prev_is_word_break = true;
                continue;
            }

            // Wrap by whole word when possible.
            if (prev_is_word_break && current_line_width > 0) {
                int word_width = text_widget_measure_word_advance(text, char_start, text_end);
                if (current_line_width + word_width > max_width) {
                    lines++;
                    current_line_width = 0;
                }
            }
            int cp_advance = text_widget_get_codepoint_advance(text, codepoint);
            // Fallback to char-wrap when a word is longer than the available width.
            if (max_width > 0 && current_line_width > 0 && current_line_width + cp_advance > max_width) {
                lines++;
                current_line_width = 0;
            }
            current_line_width += cp_advance;
            prev_is_word_break = false;
        }

        *width = max_width;
        *height = lines * text->line_height;
        text->wrap_lines = lines;
    } else {
        // Calculate unwrapped text dimensions
        // Always use character-by-character calculation to avoid heap corruption
        // from FreeType's internal memory management in text_render_get_metrics
        *width = 0;
        int lines = 1;  // Count lines for multi-line text
        int current_line_width = 0;
        const unsigned char *p = (const unsigned char *)text->text;
        const unsigned char *text_end = (const unsigned char *)(text->text + text->text_len);

        while (p < text_end && *p) {
            uint32_t codepoint = utf8_decode(&p);

            // Handle newline - count lines
            if (codepoint == '\n') {
                lines++;
                if (current_line_width > *width) {
                    *width = current_line_width;  // Track max line width
                }
                current_line_width = 0;
                continue;
            }

            // Use appropriate font for character width
            text_render_handle_t renderer = text->text_renderer;
            if (multi_font_mode && is_emoji_codepoint(codepoint)) {
                renderer = text->emoji_renderer;
            }
            // If no text_renderer but have emoji_renderer, use emoji_renderer for all
            if (renderer == NULL && text->emoji_renderer != NULL) {
                renderer = text->emoji_renderer;
            }

            int char_width = renderer ? text_render_get_char_width(renderer, codepoint) : text->font_size / 2;
            if (char_width <= 0) {
                char_width = text->font_size / 2;  // Fallback
            }
            current_line_width += char_width;
        }

        // Check last line width
        if (current_line_width > *width) {
            *width = current_line_width;
        }

        // Calculate total height based on number of lines
        *height = lines * text->line_height;
        text->wrap_lines = lines;
    }

    text->text_width = *width;
    text->text_height = *height;
}

// Helper: UTF-8 decode (reuse existing logic)
static uint32_t utf8_decode_char(const unsigned char **s)
{
    const unsigned char *p = *s;
    uint32_t codepoint = 0;

    if ((*p & 0x80) == 0) {
        codepoint = *p;
        *s += 1;
    } else if ((*p & 0xE0) == 0xC0) {
        codepoint = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F);
        *s += 2;
    } else if ((*p & 0xF0) == 0xE0) {
        codepoint = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        *s += 3;
    } else if ((*p & 0xF8) == 0xF0) {
        codepoint = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) |
                    ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        *s += 4;
    } else {
        codepoint = *p;
        *s += 1;
    }
    return codepoint;
}

static inline int text_widget_get_baseline_from_renderer(text_render_handle_t renderer, const char *sample)
{
    if (renderer == NULL || sample == NULL || sample[0] == '\0') {
        return 0;
    }
    text_render_metrics_t m = {0};
    if (text_render_get_metrics(renderer, sample, &m) == ESP_VIDEO_RENDER_ERR_OK) {
        return m.baseline;
    }
    return 0;
}

// Render text character by character, switching between regular and emoji fonts
static esp_video_render_err_t render_text_multi_font(text_widget_ctx_t *text, uint8_t *buffer,
                                                     esp_video_render_format_t format,
                                                     int widget_w, int widget_h,
                                                     int buf_w, int buf_h, int pitch,
                                                     int start_x, int start_y, int align_h,
                                                     int origin_x, int origin_y,
                                                     const esp_video_render_rect_t *viewport)
{
    // start_x and start_y represent the top-left corner of the text block
    // For LEFT alignment, start_x is 0 (or scroll offset)
    // For CENTER/RIGHT alignment, we need to align each line independently
    // Calculate baseline for first line.
    //
    // IMPORTANT: FreeType uses baseline coordinates (current_y is the baseline).
    // The old heuristic `font_size * 4/5` works for outline fonts but can crop bitmap emoji
    // strikes (CBDT/CBLC) where `slot->bitmap_top` is often close to the full strike height.
    // Use renderer metrics when possible, otherwise fall back to the heuristic.
    int baseline_off = 0;
    int b1 = text_widget_get_baseline_from_renderer(text->text_renderer, "A");
    int b2 = text_widget_get_baseline_from_renderer(text->emoji_renderer, "😊");
    baseline_off = (b1 > baseline_off) ? b1 : baseline_off;
    baseline_off = (b2 > baseline_off) ? b2 : baseline_off;
    if (baseline_off <= 0) {
        baseline_off = (text->font_size * 4 / 5);
    }
    int baseline_y = start_y + baseline_off;

    // For LEFT alignment with scrolling, current_x represents character position in the text (0, 20, 40...)
    // For CENTER/RIGHT alignment, current_x is already in widget coordinates (0 to width)
    // start_x is the offset of the text block relative to widget (can be negative when scrolling)
    int current_x = 0;           // Start at 0 for LEFT alignment (will be adjusted based on alignment)
    int current_y = baseline_y;  // Start at baseline for first line
    const unsigned char *p = (const unsigned char *)text->text;
    const unsigned char *text_end = (const unsigned char *)(text->text + text->text_len);

    // Use line_height for proper line spacing
    int line_height = text->line_height;
    int widget_top = 0;
    const unsigned char *line_start = p;
    int line_width = 0;
    bool is_first_char_in_line = true;
    bool prev_is_word_break = true;
    while (p < text_end && *p) {
        const unsigned char *char_start = p;
        uint32_t codepoint = utf8_decode_char(&p);

        if (codepoint == '\n') {
            current_y += line_height;
            line_start = p;
            line_width = 0;
            is_first_char_in_line = true;
            prev_is_word_break = true;

            int next_line_baseline = current_y;
            if (next_line_baseline >= widget_top + widget_h) {
                break;
            }
            continue;
        }

        // Determine which font to use for width calculation
        bool is_emoji = is_emoji_codepoint(codepoint);
        text_render_handle_t renderer = is_emoji ? text->emoji_renderer : text->text_renderer;
        if (renderer == NULL) {
            renderer = text->text_renderer ? text->text_renderer : text->emoji_renderer;
        }

        // Calculate character advance for this character
        int char_advance = text_render_get_char_advance_real(renderer, codepoint);
        if (char_advance <= 0) {
            char_advance = text->font_size / 2;
        }

        // If this is the first character of a line, calculate line alignment
        if (is_first_char_in_line) {
            // Calculate the full width of this line first
            const unsigned char *line_p = line_start;
            int temp_line_width = 0;
            while (line_p < text_end && *line_p && *line_p != '\n') {
                uint32_t temp_codepoint = utf8_decode_char(&line_p);
                bool temp_is_emoji = is_emoji_codepoint(temp_codepoint);
                text_render_handle_t temp_renderer = temp_is_emoji ? text->emoji_renderer : text->text_renderer;
                if (temp_renderer == NULL) {
                    temp_renderer = text->text_renderer;
                }
                int temp_advance = text_render_get_char_advance_real(temp_renderer, temp_codepoint);
                if (temp_advance <= 0) {
                    temp_advance = text->font_size / 2;
                }
                temp_line_width += temp_advance;
            }
            line_width = temp_line_width;

            // Calculate x position for this line based on alignment
            if (align_h == TEXT_ALIGN_CENTER) {
                // CENTER/RIGHT: current_x is in widget coordinates
                current_x = (widget_w - line_width) / 2;
            } else if (align_h == TEXT_ALIGN_RIGHT) {
                // CENTER/RIGHT: current_x is in widget coordinates
                current_x = widget_w - line_width;
            } else {
                current_x = 0;
            }
            is_first_char_in_line = false;
        }

        bool should_wrap = false;
        if (text->overflow_mode == OVERFLOW_WRAP) {
            int check_base_x = (align_h == TEXT_ALIGN_LEFT) ? (current_x + start_x) : current_x;
            if (!text_widget_is_word_break(codepoint) && prev_is_word_break && current_x > 0) {
                int word_width = text_widget_measure_word_advance(text, char_start, text_end);
                if (check_base_x + word_width > widget_w) {
                    should_wrap = true;
                }
            } else if (current_x > 0 && check_base_x + char_advance > widget_w) {
                should_wrap = true;
            }
        }
        if (should_wrap) {
            current_y += line_height;
            line_start = char_start;
            line_width = 0;
            is_first_char_in_line = true;
            prev_is_word_break = true;

            int next_line_baseline = current_y;
            if (next_line_baseline >= widget_top + widget_h) {
                break;
            }
            if (align_h == TEXT_ALIGN_LEFT) {
                current_x = 0;
            }
            p = char_start;
            continue;
        }
        // renderer and is_emoji are already determined above
        int widget_x = (align_h == TEXT_ALIGN_LEFT) ? (current_x + start_x) : current_x;

        // Even if `widget_x` is negative (e.g. CENTER align and glyph advance > widget width),
        // the glyph can still be partially visible after clipping. Only skip if it is
        // completely outside the widget in X/Y.
        const bool x_intersects = !((widget_x + char_advance) <= 0 || widget_x >= widget_w);
        const bool y_intersects = (current_y >= widget_top && current_y < widget_top + widget_h);

        if (x_intersects && y_intersects) {
            const int render_x = widget_x;
            const int render_y = current_y;
            const int draw_x = origin_x + render_x;
            const int draw_y = origin_y + render_y;

            if (text->shadow_enable) {
                (void)text_render_draw_codepoint_with_shadow_viewport(renderer, codepoint,
                                                                      buffer, format,
                                                                      buf_w, buf_h, pitch,
                                                                      draw_x, draw_y,
                                                                      &text->shadow_color,
                                                                      text->shadow_offset_x, text->shadow_offset_y,
                                                                      viewport);
            } else {
                (void)text_render_draw_codepoint_viewport(renderer, codepoint,
                                                          buffer, format,
                                                          buf_w, buf_h, pitch,
                                                          draw_x, draw_y,
                                                          viewport);
            }
        }

        current_x += char_advance;
        prev_is_word_break = text_widget_is_word_break(codepoint);
    }

    return ESP_VIDEO_RENDER_ERR_OK;
}

// Render text using FreeType with multi-font support
static esp_video_render_err_t render_text(text_widget_ctx_t *text, uint8_t *buffer,
                                          esp_video_render_format_t format,
                                          int widget_w, int widget_h,
                                          int buf_w, int buf_h, int pitch,
                                          int start_x, int start_y,
                                          int origin_x, int origin_y,
                                          const esp_video_render_rect_t *viewport)
{
    // Check if we have text and at least one renderer
    if (text->text == NULL || text->text_len == 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    if (text->text_renderer == NULL && text->emoji_renderer == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    // Sync renderer style only when changed to reduce per-frame overhead.
    if (text->render_style_dirty ||
        memcmp(&text->applied_text_color, &text->text_color, sizeof(text->text_color)) != 0 ||
        memcmp(&text->applied_bg_color, &text->bg_color, sizeof(text->bg_color)) != 0 ||
        text->applied_bg_transparent != text->bg_transparent) {
        if (text->text_renderer != NULL) {
            text_render_set_text_color(text->text_renderer, &text->text_color);
            text_render_set_bg_color(text->text_renderer, &text->bg_color, !text->bg_transparent);
        }
        if (text->emoji_renderer != NULL) {
            text_render_set_text_color(text->emoji_renderer, &text->text_color);
            text_render_set_bg_color(text->emoji_renderer, &text->bg_color, !text->bg_transparent);
        }
        text->applied_text_color = text->text_color;
        text->applied_bg_color = text->bg_color;
        text->applied_bg_transparent = text->bg_transparent;
        text->render_style_dirty = false;
    }
    return render_text_multi_font(text, buffer, format,
                                  widget_w, widget_h,
                                  buf_w, buf_h, pitch,
                                  start_x, start_y, text->align_h,
                                  origin_x, origin_y,
                                  viewport);
}

// -----------------------------
// Redraw helpers (local-only)
// -----------------------------
static inline esp_video_render_rect_t text_widget_clamp_dirty_local(const esp_video_render_rect_t *dirty_local,
                                                                    int widget_w,
                                                                    int widget_h,
                                                                    bool *out_full_widget_dirty)
{
    esp_video_render_rect_t r = {0};
    bool full = false;
    if (dirty_local == NULL) {
        if (widget_w > 0 && widget_h > 0) {
            r.width = widget_w;
            r.height = widget_h;
            full = true;
        }
    } else {
        int x = dirty_local->x;
        int y = dirty_local->y;
        int width = dirty_local->width;
        int height = dirty_local->height;

        if (x < 0) {
            width += x;
            x = 0;
        }
        if (y < 0) {
            height += y;
            y = 0;
        }
        if (widget_w > 0 && widget_h > 0 && width > 0 && height > 0 &&
            x < widget_w && y < widget_h) {
            if (width > widget_w - x) {
                width = widget_w - x;
            }
            if (height > widget_h - y) {
                height = widget_h - y;
            }
            if (width > 0 && height > 0) {
                r.x = x;
                r.y = y;
                r.width = width;
                r.height = height;
                full = (x == 0 && y == 0 && width == widget_w && height == widget_h);
            }
        }
    }
    if (out_full_widget_dirty) {
        *out_full_widget_dirty = full;
    }
    return r;
}

static inline void text_widget_calc_origin_in_dstfb(const esp_vui_widget_t *widget,
                                                    const esp_video_render_fb_t *dst_fb,
                                                    int *out_origin_x,
                                                    int *out_origin_y)
{
    int ox = 0;
    int oy = 0;
    const esp_video_render_rect_t *ctr_disp = NULL;
    esp_vui_container_get_disp_rect(widget->container, &ctr_disp);
    if (ctr_disp) {
        // If dst buffer matches container size => container-local (cached). Else => screen-local (no-cache).
        bool is_container_local = (dst_fb->info.width == (int)ctr_disp->width) && (dst_fb->info.height == (int)ctr_disp->height);
        if (!is_container_local) {
            ox = ctr_disp->x;
            oy = ctr_disp->y;
        }
    }
    ox += widget->rect.x;
    oy += widget->rect.y;
    if (out_origin_x) {
        *out_origin_x = ox;
    }
    if (out_origin_y) {
        *out_origin_y = oy;
    }
}

typedef struct {
    int                      widget_w;
    int                      widget_h;
    int                      pitch;
    int                      bytes_per_pixel;
    int                      origin_x;
    int                      origin_y;
    esp_video_render_rect_t  redraw_rect_local;
    esp_video_render_rect_t  clear_rect_dst;
    bool                     full_widget_dirty;
} text_widget_draw_ctx_t;

static inline bool text_widget_prepare_draw_ctx(text_widget_ctx_t *text,
                                                esp_vui_widget_t *self,
                                                const esp_video_render_fb_t *dst_fb,
                                                const esp_video_render_rect_t *dst_rect,
                                                const esp_video_render_rect_t *dirty,
                                                text_widget_draw_ctx_t *ctx)
{
    if (text == NULL || self == NULL || dst_fb == NULL || ctx == NULL) {
        return false;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->widget_w = text->widget.rect.width;
    ctx->widget_h = text->widget.rect.height;
    ctx->bytes_per_pixel = video_render_get_pixel_bits(dst_fb->info.format) >> 3;
    ctx->pitch = dst_fb->info.width * ctx->bytes_per_pixel;
    text_widget_calc_origin_in_dstfb(self, dst_fb, &ctx->origin_x, &ctx->origin_y);

    const esp_video_render_rect_t *dirty_local = (text->fresh || dirty == NULL || text->scroll_enable) ? NULL : dirty;
    ctx->redraw_rect_local = text_widget_clamp_dirty_local(dirty_local, ctx->widget_w, ctx->widget_h, &ctx->full_widget_dirty);
    if (ctx->redraw_rect_local.width <= 0 || ctx->redraw_rect_local.height <= 0) {
        return false;
    }

    esp_video_render_rect_t widget_dirty_in_dst = {
        .x = ctx->origin_x + ctx->redraw_rect_local.x,
        .y = ctx->origin_y + ctx->redraw_rect_local.y,
        .width = ctx->redraw_rect_local.width,
        .height = ctx->redraw_rect_local.height,
    };
    ctx->clear_rect_dst = widget_dirty_in_dst;
    if (dst_rect) {
        esp_video_render_rect_t clipped = {};
        if (rect_intersect(&widget_dirty_in_dst, dst_rect, &clipped, NULL)) {
            ctx->clear_rect_dst = clipped;
        } else {
            ctx->clear_rect_dst.width = 0;
            ctx->clear_rect_dst.height = 0;
            return false;
        }
    }
    esp_video_render_rect_t fb_rect = {
        .x = 0,
        .y = 0,
        .width = dst_fb->info.width,
        .height = dst_fb->info.height,
    };
    esp_video_render_rect_t clipped_fb = {};
    if (rect_intersect(&ctx->clear_rect_dst, &fb_rect, &clipped_fb, NULL)) {
        ctx->clear_rect_dst = clipped_fb;
        return (ctx->clear_rect_dst.width > 0 && ctx->clear_rect_dst.height > 0);
    }
    return false;
}

static inline void text_widget_clear_bg_if_needed(text_widget_ctx_t *text,
                                                  uint8_t *buffer,
                                                  const esp_video_render_fb_t *dst_fb,
                                                  const esp_video_render_rect_t *clear_rect,
                                                  int pitch,
                                                  int bytes_per_pixel)
{
    // Background clearing policy:
    // - Transparent background: keep underlying content, except scroll uses a clear-to-zero fast path
    //   when not forced to repaint with bg color.
    // - Opaque background: always fill exactly dst_rect (the dirty region in destination coords).
    if (clear_rect == NULL || clear_rect->width <= 0 || clear_rect->height <= 0) {
        return;
    }
    if (text->scroll_enable) {
        uint8_t *dirty_buf = buffer + clear_rect->y * pitch + clear_rect->x * bytes_per_pixel;
        if (!text->bg_transparent || text->needs_refresh) {
            fill_background(dirty_buf, dst_fb->info.format,
                            clear_rect->width, clear_rect->height, pitch, &text->bg_color);
        } else {
            // Clear row by row (pitch-aware) to avoid corrupting rows below widget area.
            size_t row_bytes = (size_t)clear_rect->width * (size_t)bytes_per_pixel;
            for (int y = 0; y < clear_rect->height; y++) {
                memset(dirty_buf + y * pitch, 0, row_bytes);
            }
        }
        return;
    }

    if (!text->bg_transparent) {
        uint8_t *dirty_buf = buffer + clear_rect->y * pitch + clear_rect->x * bytes_per_pixel;
        fill_background(dirty_buf, dst_fb->info.format,
                        clear_rect->width, clear_rect->height, pitch, &text->bg_color);
    }
}

static inline bool text_widget_ensure_text_metrics(text_widget_ctx_t *text)
{
    if (text->text == NULL || text->text_len == 0) {
        return false;
    }
    if (text->text_renderer == NULL && text->emoji_renderer == NULL) {
        return false;
    }
    if (text->needs_refresh || text->text_width == 0 || text->text_height == 0) {
        calculate_text_size(text, &text->text_width, &text->text_height);
        if (text->text_width == 0 || text->text_height == 0) {
            return false;
        }
        text->needs_refresh = false;
    }
    return true;
}

static inline void text_widget_calc_text_pos(const text_widget_ctx_t *text,
                                             int widget_w, int widget_h,
                                             int *out_x, int *out_y)
{
    int x = 0;
    int y = 0;
    if (text->align_h == TEXT_ALIGN_CENTER) {
        x = (widget_w - text->text_width) / 2;
    } else if (text->align_h == TEXT_ALIGN_RIGHT) {
        x = widget_w - text->text_width;
    }
    if (text->align_v == TEXT_ALIGN_MIDDLE) {
        y = (widget_h - text->text_height) / 2;
    } else if (text->align_v == TEXT_ALIGN_BOTTOM) {
        y = widget_h - text->text_height;
    }
    if (text->scroll_enable) {
        if (text->scroll_mode == SCROLL_MODE_HORIZONTAL || text->scroll_mode == SCROLL_MODE_CIRCULAR) {
            x -= text->scroll_pos;
        } else if (text->scroll_mode == SCROLL_MODE_VERTICAL) {
            y -= text->scroll_pos;
        }
    }
    if (out_x) {
        *out_x = x;
    }
    if (out_y) {
        *out_y = y;
    }
}

static inline bool text_widget_should_render(const text_widget_ctx_t *text,
                                             int widget_w, int widget_h,
                                             const esp_video_render_rect_t *widget_dirty_local,
                                             int text_x, int text_y,
                                             bool full_widget_dirty)
{
    if (text->fresh) {
        return true;
    }
    if (text->scroll_enable && !text->scroll_paused) {
        return true;
    }
    if (full_widget_dirty) {
        return true;
    }
    if (text->text_width <= 0 || text->text_height <= 0 || widget_dirty_local == NULL) {
        return true;
    }
    // Visible portion of the text bbox within the widget (clipped to widget bounds).
    int visible_x = text_x;
    int visible_y = text_y;
    int visible_w = text->text_width;
    int visible_h = text->text_height;
    if (visible_x < 0) {
        visible_w += visible_x;
        visible_x = 0;
    }
    if (visible_y < 0) {
        visible_h += visible_y;
        visible_y = 0;
    }
    if (widget_w <= 0 || widget_h <= 0 || visible_w <= 0 || visible_h <= 0 ||
        visible_x >= widget_w || visible_y >= widget_h) {
        return false;
    }
    if (visible_w > widget_w - visible_x) {
        visible_w = widget_w - visible_x;
    }
    if (visible_h > widget_h - visible_y) {
        visible_h = widget_h - visible_y;
    }
    if (visible_w <= 0 || visible_h <= 0) {
        return false;
    }
    esp_video_render_rect_t text_visible_area = {
        .x = visible_x,
        .y = visible_y,
        .width = visible_w,
        .height = visible_h,
    };

    // Redraw only when dirty intersects the visible text area.
    // (full-widget redraw / fresh / scrolling paths are handled above.)
    esp_video_render_rect_t tmp;
    bool dirty_hits_text = rect_intersect(widget_dirty_local, &text_visible_area, &tmp, NULL);
    return dirty_hits_text;
}

static inline void text_widget_render_circular_wrap(text_widget_ctx_t *text,
                                                    uint8_t *buffer,
                                                    const esp_video_render_fb_t *dst_fb,
                                                    int pitch,
                                                    int widget_w, int widget_h,
                                                    int origin_x, int origin_y,
                                                    int text_x, int text_y,
                                                    const esp_video_render_rect_t *viewport)
{
    if (!(text->scroll_mode == SCROLL_MODE_CIRCULAR && text->scroll_enable && !text->scroll_paused)) {
        return;
    }
    const int circular_gap = text->font_size > 0 ? text->font_size : 8;
    int wrap_threshold = text->scroll_total + text->widget.rect.width + circular_gap;
    if (wrap_threshold <= 0) {
        return;
    }

    int wrapped_x = text_x + wrap_threshold;
    if (wrapped_x < widget_w && wrapped_x + text->text_width > 0) {
        render_text(text, buffer, dst_fb->info.format,
                    widget_w, widget_h,
                    dst_fb->info.width, dst_fb->info.height, pitch,
                    wrapped_x, text_y,
                    origin_x, origin_y,
                    viewport);
    }
}

static esp_video_render_err_t text_widget_redraw(esp_vui_widget_t *self,
                                                 esp_video_render_fb_t *dst_fb,
                                                 const esp_video_render_rect_t *dst_rect,
                                                 const esp_video_render_rect_t *dirty)
{
    text_widget_ctx_t *text = (text_widget_ctx_t *)self;
    if (self == NULL || dst_fb == NULL || dst_fb->data == NULL || dst_rect == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_widget_draw_ctx_t draw_ctx = {};
    if (!text_widget_prepare_draw_ctx(text, self, dst_fb, dst_rect, dirty, &draw_ctx)) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    uint8_t *buffer = (uint8_t *)dst_fb->data;
    text_widget_clear_bg_if_needed(text, buffer, dst_fb,
                                   &draw_ctx.clear_rect_dst,
                                   draw_ctx.pitch, draw_ctx.bytes_per_pixel);

    if (text_widget_ensure_text_metrics(text)) {
        int text_x = 0;
        int text_y = 0;
        text_widget_calc_text_pos(text, draw_ctx.widget_w, draw_ctx.widget_h, &text_x, &text_y);

        // Decide whether we need to render glyphs for this dirty region.
        bool should_render = text_widget_should_render(text, draw_ctx.widget_w, draw_ctx.widget_h,
                                                       &draw_ctx.redraw_rect_local,
                                                       text_x, text_y,
                                                       draw_ctx.full_widget_dirty);
        if (should_render) {
            const esp_video_render_rect_t *viewport = dst_rect;  // destination coords clip
            render_text(text, buffer, dst_fb->info.format,
                        draw_ctx.widget_w, draw_ctx.widget_h,
                        dst_fb->info.width, dst_fb->info.height, draw_ctx.pitch,
                        text_x, text_y,
                        draw_ctx.origin_x, draw_ctx.origin_y,
                        viewport);
            text_widget_render_circular_wrap(text, buffer, dst_fb, draw_ctx.pitch,
                                             draw_ctx.widget_w, draw_ctx.widget_h,
                                             draw_ctx.origin_x, draw_ctx.origin_y,
                                             text_x, text_y,
                                             viewport);
        }
    }

    text->fresh = false;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static void text_widget_destroy(esp_vui_widget_t *self)
{
    if (self == NULL) {
        return;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)self;
    self->container = NULL;

    if (text->text) {
        video_render_free(text->text);
        text->text = NULL;
    }

    // No framebuffer to free - we draw directly to dst_fb

    // Deinitialize text renderers
    if (text->text_renderer) {
        text_render_deinit(text->text_renderer);
        text->text_renderer = NULL;
    }
    if (text->emoji_renderer) {
        text_render_deinit(text->emoji_renderer);
        text->emoji_renderer = NULL;
    }

    video_render_free(text);
}

esp_vui_widget_t *esp_vui_text_widget_init(esp_vui_container_handle_t container,
                                           esp_video_render_frame_info_t *frame_info,
                                           esp_video_render_pos_t *pos,
                                           int width,
                                           int height)
{
    if (container == NULL || frame_info == NULL || pos == NULL || width <= 0 || height <= 0) {
        return NULL;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)video_render_calloc(1, sizeof(text_widget_ctx_t));
    if (text == NULL) {
        return NULL;
    }
    static int text_id = 0;
    static const esp_vui_widget_ops_t text_ops = {
        .redraw = text_widget_redraw,
        .destroy = text_widget_destroy,
    };
    text->widget.id = text_id++;
    // Set widget size
    text->widget.rect.x = pos->x;
    text->widget.rect.y = pos->y;
    text->widget.rect.width = width;
    text->widget.rect.height = height;
    text->widget.dirty = text->widget.rect;
    // Store format info for reference (but don't allocate framebuffer - draw directly to dst_fb)
    text->fb.info = *frame_info;
    text->fb.info.width = width;
    text->fb.info.height = height;
    text->fb.data = NULL;  // No intermediate framebuffer - draw directly to dst_fb
    text->fb.size = 0;
    text->gen_fb = false;  // Don't need to free framebuffer
    text->fresh = true;

    // Initialize default values
    text->text = NULL;
    text->text_len = 0;
    text->text_capacity = 0;
    text->text_renderer = NULL;
    text->font_size = 16;
    text->line_height = 18;
    text->font_path = NULL;  // Will need to be set by user

    // Default colors
    text->text_color.r = 255;
    text->text_color.g = 255;
    text->text_color.b = 255;
    text->bg_color.r = 0;
    text->bg_color.g = 0;
    text->bg_color.b = 0;
    text->bg_transparent = false;
    text->applied_bg_transparent = !text->bg_transparent;
    text->render_style_dirty = true;

    // Default alignment
    text->align_h = TEXT_ALIGN_LEFT;
    text->align_v = TEXT_ALIGN_TOP;

    // Default scrolling
    text->scroll_enable = false;
    text->scroll_mode = SCROLL_MODE_NONE;
    text->scroll_pos = 0;
    text->scroll_speed = 1;
    text->scroll_paused = false;
    text->scroll_total = 0;
    text->last_scroll_time = 0;

    // Default overflow
    text->overflow_mode = OVERFLOW_CLIP;

    // Default effects
    text->shadow_enable = false;
    text->shadow_color.r = 0;
    text->shadow_color.g = 0;
    text->shadow_color.b = 0;
    text->shadow_offset_x = 1;
    text->shadow_offset_y = 1;

    // Internal state
    text->text_width = 0;
    text->text_height = 0;
    text->needs_refresh = true;
    text->wrap_lines = 0;

    text->widget.ops = &text_ops;
    if (esp_vui_container_add_widget(container, &text->widget) == ESP_VIDEO_RENDER_ERR_OK) {
        return &text->widget;
    }
    // Cleanup on error
    if (text->text_renderer) {
        text_render_deinit(text->text_renderer);
    }
    // No framebuffer to free - we draw directly to dst_fb
    video_render_free(text);
    return NULL;
}

static void text_widget_mark_dirty(text_widget_ctx_t *text, bool need_refresh)
{
    esp_vui_widget_t *widget = &text->widget;
    if (widget->container == NULL) {
        // Widget not yet added to container, just mark for refresh
        text->needs_refresh = need_refresh;
        widget->dirty = widget->rect;
        return;
    }
    esp_vui_container_compose_lock(widget->container);
    text->needs_refresh = need_refresh;
    widget->dirty = widget->rect;
    esp_vui_container_notify_compose_changed(widget->container, &widget->dirty, true);
    esp_vui_container_compose_unlock(widget->container);
}

static void text_widget_mark_dirty_locked(text_widget_ctx_t *text, bool need_refresh)
{
    esp_vui_widget_t *widget = &text->widget;
    text->needs_refresh = need_refresh;
    widget->dirty = widget->rect;
    if (widget->container) {
        esp_vui_container_notify_compose_changed(widget->container, &widget->dirty, true);
    }
}

static inline bool text_widget_clip_local_rect(const text_widget_ctx_t *text, esp_video_render_rect_t *r)
{
    if (r == NULL) {
        return false;
    }
    const int widget_w = text->widget.rect.width;
    const int widget_h = text->widget.rect.height;
    if (r->x >= widget_w || r->y >= widget_h) {
        return false;
    }
    if (r->x + r->width > widget_w) {
        r->width = widget_w - r->x;
    }
    if (r->y + r->height > widget_h) {
        r->height = widget_h - r->y;
    }
    return (r->width > 0 && r->height > 0);
}

static inline void text_widget_mark_local_dirty(text_widget_ctx_t *text,
                                                const esp_video_render_rect_t *local_dirty,
                                                bool need_refresh)
{
    if (local_dirty == NULL) {
        text_widget_mark_dirty(text, need_refresh);
        return;
    }
    esp_vui_widget_t *widget = &text->widget;
    if (widget->container == NULL) {
        text->needs_refresh = need_refresh;
        widget->dirty = widget->rect;
        return;
    }
    esp_video_render_rect_t local = *local_dirty;
    if (!text_widget_clip_local_rect(text, &local)) {
        return;
    }
    esp_vui_container_compose_lock(widget->container);
    text->needs_refresh = need_refresh;
    widget->dirty.x = widget->rect.x + local.x;
    widget->dirty.y = widget->rect.y + local.y;
    widget->dirty.width = local.width;
    widget->dirty.height = local.height;
    esp_vui_container_notify_compose_changed(widget->container, &widget->dirty, true);
    esp_vui_container_compose_unlock(widget->container);
}

static inline void text_widget_mark_local_dirty_locked(text_widget_ctx_t *text,
                                                       const esp_video_render_rect_t *local_dirty,
                                                       bool need_refresh)
{
    if (local_dirty == NULL) {
        text_widget_mark_dirty_locked(text, need_refresh);
        return;
    }
    esp_vui_widget_t *widget = &text->widget;
    if (widget->container == NULL) {
        text->needs_refresh = need_refresh;
        widget->dirty = widget->rect;
        return;
    }
    esp_video_render_rect_t local = *local_dirty;
    if (!text_widget_clip_local_rect(text, &local)) {
        return;
    }
    text->needs_refresh = need_refresh;
    widget->dirty.x = widget->rect.x + local.x;
    widget->dirty.y = widget->rect.y + local.y;
    widget->dirty.width = local.width;
    widget->dirty.height = local.height;
    esp_vui_container_notify_compose_changed(widget->container, &widget->dirty, true);
}

static void text_widget_apply_font_renderer_locked(text_widget_ctx_t *text,
                                                   text_render_handle_t renderer,
                                                   int font_size)
{
    if (text->text_renderer) {
        text_render_deinit(text->text_renderer);
    }
    text->text_renderer = renderer;
    text->font_size = font_size;
    text->line_height = font_size + (font_size / 4);
    text->font_path = NULL;
    text->render_style_dirty = true;
}

esp_video_render_err_t esp_vui_text_widget_set_font_renderer(esp_vui_widget_t *widget, text_render_handle_t renderer, int font_size)
{
    if (widget == NULL || renderer == NULL || font_size <= 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;
    bool locked = false;
    if (widget->container) {
        esp_vui_container_compose_lock(widget->container);
        locked = true;
    }
    text_widget_apply_font_renderer_locked(text, renderer, font_size);
    if (locked) {
        text_widget_mark_dirty_locked(text, true);
        esp_vui_container_compose_unlock(widget->container);
    } else {
        text_widget_mark_dirty(text, true);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_text_widget_set_font(esp_vui_widget_t *widget, const char *font_path, int font_size)
{
    if (widget == NULL || font_path == NULL || font_size <= 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;
    const char *font_name = strrchr(font_path, '/');
    font_name = font_name ? font_name + 1 : font_path;
    text_render_cfg_t cfg = {
        .font_path = font_path,
        .font_mem = NULL,
        .font_mem_size = 0,
        .font_name = font_name,
        .font_size = font_size,
        .text_color = text->text_color,
        .bg_color = text->bg_color,
        .bg_fill = !text->bg_transparent,
        .antialiasing = true};
    text_render_handle_t renderer = NULL;
    bool locked = false;
    if (widget->container) {
        esp_vui_container_compose_lock(widget->container);
        locked = true;
    }
    esp_video_render_err_t ret = text_render_init(&cfg, &renderer);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        if (locked) {
            esp_vui_container_compose_unlock(widget->container);
        }
        ESP_LOGE(TAG, "Failed to initialize text renderer with font: %s", font_path);
        return ret;
    }
    if (locked) {
        text_widget_apply_font_renderer_locked(text, renderer, font_size);
        text->font_path = font_path;
        text_widget_mark_dirty_locked(text, true);
        esp_vui_container_compose_unlock(widget->container);
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    ret = esp_vui_text_widget_set_font_renderer(widget, renderer, font_size);
    if (ret == ESP_VIDEO_RENDER_ERR_OK) {
        text->font_path = font_path;
    }
    return ret;
}

esp_video_render_err_t esp_vui_text_widget_set_font_from_mem(esp_vui_widget_t *widget, const char *font_name,
                                                             const uint8_t *font_mem, int font_mem_size, int font_size)
{
    if (widget == NULL || font_mem == NULL || font_mem_size == 0 || font_size <= 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;
    text_render_cfg_t cfg = {
        .font_path = NULL,
        .font_mem = font_mem,
        .font_mem_size = font_mem_size,
        .font_name = font_name,
        .font_size = font_size,
        .text_color = text->text_color,
        .bg_color = text->bg_color,
        .bg_fill = !text->bg_transparent,
        .antialiasing = true};
    text_render_handle_t renderer = NULL;
    bool locked = false;
    if (widget->container) {
        esp_vui_container_compose_lock(widget->container);
        locked = true;
    }
    esp_video_render_err_t ret = text_render_init(&cfg, &renderer);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        if (locked) {
            esp_vui_container_compose_unlock(widget->container);
        }
        ESP_LOGE(TAG, "Failed to initialize text renderer with font from memory: %s", font_name ? font_name : "unknown");
        return ret;
    }
    if (locked) {
        text_widget_apply_font_renderer_locked(text, renderer, font_size);
        text_widget_mark_dirty_locked(text, true);
        esp_vui_container_compose_unlock(widget->container);
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    return esp_vui_text_widget_set_font_renderer(widget, renderer, font_size);
}

esp_video_render_err_t esp_vui_text_widget_set_emoji_font(esp_vui_widget_t *widget, const char *font_path, int font_size)
{
    if (widget == NULL || font_path == NULL || font_size <= 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;

    // Extract font name from path for resource sharing
    const char *font_name = strrchr(font_path, '/');
    font_name = font_name ? font_name + 1 : font_path;

    text_render_cfg_t cfg = {
        .font_path = font_path,
        .font_mem = NULL,
        .font_mem_size = 0,
        .font_name = font_name,  // Provide font name for resource sharing
        .font_size = font_size,
        .text_color = text->text_color,
        .bg_color = text->bg_color,
        .bg_fill = !text->bg_transparent,
        .antialiasing = true};

    text_render_handle_t renderer = NULL;
    bool locked = false;
    if (widget->container) {
        esp_vui_container_compose_lock(widget->container);
        locked = true;
    }
    esp_video_render_err_t ret = text_render_init(&cfg, &renderer);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        if (locked) {
            esp_vui_container_compose_unlock(widget->container);
        }
        ESP_LOGE(TAG, "Failed to initialize emoji renderer with font: %s", font_path);
        return ret;
    }
    if (text->emoji_renderer) {
        text_render_deinit(text->emoji_renderer);
    }
    text->emoji_renderer = renderer;
    // If this widget doesn't have a normal text renderer, treat emoji renderer as the primary
    // font for baseline/layout. This is important for bitmap emoji fonts (CBDT/CBLC) where the
    // actual strike size can differ from the requested size.
    if (text->text_renderer == NULL) {
        int actual = text_render_get_font_size(text->emoji_renderer);
        if (actual > 0) {
            text->font_size = actual;
            text->line_height = actual + (actual / 4);
        }
    }
    if (locked) {
        text_widget_mark_dirty_locked(text, true);
        esp_vui_container_compose_unlock(widget->container);
    } else {
        text_widget_mark_dirty(text, true);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_text_widget_set_emoji_font_from_mem(esp_vui_widget_t *widget, const char *font_name,
                                                                   const uint8_t *font_mem, int font_mem_size, int font_size)
{
    if (widget == NULL || font_mem == NULL || font_mem_size == 0 || font_size <= 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;
    // Initialize emoji renderer with font from memory
    text_render_cfg_t cfg = {
        .font_path = NULL,  // Not using file path
        .font_mem = font_mem,
        .font_mem_size = font_mem_size,
        .font_name = font_name,  // Provide font name for resource sharing
        .font_size = font_size,
        .text_color = text->text_color,
        .bg_color = text->bg_color,
        .bg_fill = !text->bg_transparent,
        .antialiasing = true};
    text_render_handle_t renderer = NULL;
    bool locked = false;
    if (widget->container) {
        esp_vui_container_compose_lock(widget->container);
        locked = true;
    }
    esp_video_render_err_t ret = text_render_init(&cfg, &renderer);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        if (locked) {
            esp_vui_container_compose_unlock(widget->container);
        }
        ESP_LOGE(TAG, "Failed to initialize emoji renderer with font from memory: %s", font_name ? font_name : "unknown");
        return ret;
    }
    if (text->emoji_renderer) {
        text_render_deinit(text->emoji_renderer);
    }
    text->emoji_renderer = renderer;
    if (text->text_renderer == NULL) {
        int actual = text_render_get_font_size(text->emoji_renderer);
        if (actual > 0) {
            text->font_size = actual;
            text->line_height = actual + (actual / 4);
        }
    }
    if (locked) {
        text_widget_mark_dirty_locked(text, true);
        esp_vui_container_compose_unlock(widget->container);
    } else {
        text_widget_mark_dirty(text, true);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_text_widget_set_text(esp_vui_widget_t *widget, const char *text_str)
{
    if (widget == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;
    bool locked = false;
    if (widget->container) {
        esp_vui_container_compose_lock(widget->container);
        locked = true;
    }
    int len = text_str ? strlen(text_str) : 0;
    if (text_str == NULL) {
        text->text_len = 0;
    } else {
        if (len + 1 > text->text_capacity) {
            int new_capacity = (len + 1) * 2;
            if (new_capacity < 64) {
                new_capacity = 64;
            }
            char *new_text = (char *)video_render_realloc(text->text, new_capacity);
            if (new_text == NULL) {
                if (locked) {
                    esp_vui_container_compose_unlock(widget->container);
                }
                return ESP_VIDEO_RENDER_ERR_NO_MEM;
            }
            text->text = new_text;
            text->text_capacity = new_capacity;
        }
        memcpy(text->text, text_str, len);
        text->text[len] = '\0';
        text->text_len = len;
    }
    if (locked) {
        text_widget_mark_dirty_locked(text, true);
        esp_vui_container_compose_unlock(widget->container);
    } else {
        text_widget_mark_dirty(text, true);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_text_widget_set_text_color(esp_vui_widget_t *widget, esp_video_render_clr_t *color)
{
    if (widget == NULL || color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;
    bool locked = false;
    if (widget->container) {
        esp_vui_container_compose_lock(widget->container);
        locked = true;
    }
    text->text_color = *color;
    if (text->text_renderer) {
        text_render_set_text_color(text->text_renderer, color);
    }
    text->render_style_dirty = true;
    if (locked) {
        text_widget_mark_dirty_locked(text, false);
        esp_vui_container_compose_unlock(widget->container);
    } else {
        text_widget_mark_dirty(text, false);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_text_widget_set_bg_color(esp_vui_widget_t *widget, esp_video_render_clr_t *color, bool transparent)
{
    if (widget == NULL || color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;
    bool locked = false;
    if (widget->container) {
        esp_vui_container_compose_lock(widget->container);
        locked = true;
    }
    text->bg_color = *color;
    text->bg_transparent = transparent;
    if (text->text_renderer) {
        text_render_set_bg_color(text->text_renderer, color, !transparent);
    }
    text->render_style_dirty = true;
    if (locked) {
        text_widget_mark_dirty_locked(text, false);
        esp_vui_container_compose_unlock(widget->container);
    } else {
        text_widget_mark_dirty(text, false);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_text_widget_set_align(esp_vui_widget_t *widget, int align_h, int align_v)
{
    if (widget == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;
    bool locked = false;
    if (widget->container) {
        esp_vui_container_compose_lock(widget->container);
        locked = true;
    }
    text->align_h = align_h;
    text->align_v = align_v;
    if (locked) {
        text_widget_mark_dirty_locked(text, true);
        esp_vui_container_compose_unlock(widget->container);
    } else {
        text_widget_mark_dirty(text, true);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_text_widget_set_scroll(esp_vui_widget_t *widget, bool enable, int mode, int speed)
{
    if (widget == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;

    text->scroll_enable = enable;
    text->scroll_mode = mode;
    text->scroll_speed = speed > 0 ? speed : 1;
    text->scroll_paused = false;
    text->scroll_pos = 0;        // Reset scroll position when setting scroll
    text->last_scroll_time = 0;  // Reset timer

    // Set overflow mode based on scroll direction
    if (enable) {
        if (mode == SCROLL_MODE_VERTICAL) {
            text->overflow_mode = OVERFLOW_WRAP;
        } else {
            text->overflow_mode = OVERFLOW_CLIP;
        }
        calculate_text_size(text, &text->text_width, &text->text_height);
        if (mode == SCROLL_MODE_HORIZONTAL || mode == SCROLL_MODE_CIRCULAR) {
            // Horizontal or circular scrolling: use horizontal distance
            text->scroll_total = text->text_width - text->widget.rect.width;
        } else if (mode == SCROLL_MODE_VERTICAL) {
            text->scroll_total = text->text_height - text->widget.rect.height;
        } else {
            text->scroll_total = 0;
        }

        if (mode != SCROLL_MODE_CIRCULAR && text->scroll_total <= 0) {
            text->scroll_enable = false;
            text->scroll_total = 0;
            text->scroll_pos = 0;
        } else if (mode == SCROLL_MODE_CIRCULAR && text->scroll_total <= 0) {
            text->scroll_total = text->text_width > text->widget.rect.width                                              ?
                                                                            (text->text_width - text->widget.rect.width) : text->widget.rect.width;
        }
    } else {
        text->scroll_total = 0;
        text->scroll_pos = 0;
        text->last_scroll_time = 0;
    }

    text_widget_mark_dirty(text, true);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_text_widget_scroll_pause(esp_vui_widget_t *widget, bool pause)
{
    if (widget == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;
    text->scroll_paused = pause;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_text_widget_set_shadow(esp_vui_widget_t *widget, bool enable,
                                                      esp_video_render_clr_t *color, int offset_x, int offset_y)
{
    if (widget == NULL || color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;
    text->shadow_enable = enable;
    text->shadow_color = *color;
    text->shadow_offset_x = offset_x;
    text->shadow_offset_y = offset_y;
    text_widget_mark_dirty(text, false);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_text_widget_scroll_update(esp_vui_widget_t *widget)
{
    if (widget == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;

    // Update scroll position if scrolling is enabled
    if (text->scroll_enable && !text->scroll_paused) {
        int64_t now = esp_timer_get_time() / 1000;  // Convert to milliseconds
        if (text->last_scroll_time == 0) {
            text->last_scroll_time = now;
        }

        bool position_changed = false;
        int old_text_x = 0;
        int old_text_y = 0;
        bool can_partial_dirty = false;
        if (text->scroll_mode != SCROLL_MODE_CIRCULAR && text_widget_ensure_text_metrics(text)) {
            text_widget_calc_text_pos(text, text->widget.rect.width, text->widget.rect.height, &old_text_x, &old_text_y);
            can_partial_dirty = true;
        }
        int64_t time_diff = now - text->last_scroll_time;
        // Update scroll position based on time (every ~50ms)
        if (time_diff >= 50) {
            text->scroll_pos += text->scroll_speed;
            text->last_scroll_time = now;
            position_changed = true;

            if (text->scroll_mode == SCROLL_MODE_CIRCULAR) {
                // Circular scrolling: wrap around continuously (scrolls horizontally)
                // Wrap when reaching end + widget width to create seamless loop
                // For circular scroll, scroll_total represents the extra distance needed for seamless wrap
                const int circular_gap = text->font_size > 0 ? text->font_size : 8;
                int wrap_threshold = text->scroll_total + text->widget.rect.width + circular_gap;
                if (wrap_threshold > 0 && text->scroll_pos >= wrap_threshold) {
                    // Wrap by subtracting the threshold, creating seamless loop
                    text->scroll_pos = text->scroll_pos % wrap_threshold;
                }
            } else if (text->scroll_mode == SCROLL_MODE_HORIZONTAL) {
                // One-way horizontal scrolling: reset to start when reaching end
                if (text->scroll_pos >= text->scroll_total) {
                    text->scroll_pos = 0;  // Reset or could pause
                }
            } else if (text->scroll_mode == SCROLL_MODE_VERTICAL) {
                // One-way vertical scrolling: reset to start when reaching end
                if (text->scroll_pos >= text->scroll_total) {
                    text->scroll_pos = 0;  // Reset or could pause
                }
            }
            if (position_changed) {
                if (can_partial_dirty) {
                    int new_text_x = 0;
                    int new_text_y = 0;
                    text_widget_calc_text_pos(text, text->widget.rect.width, text->widget.rect.height, &new_text_x, &new_text_y);

                    esp_video_render_rect_t old_bbox = {
                        .x = old_text_x,
                        .y = old_text_y,
                        .width = text->text_width,
                        .height = text->text_height,
                    };
                    esp_video_render_rect_t new_bbox = {
                        .x = new_text_x,
                        .y = new_text_y,
                        .width = text->text_width,
                        .height = text->text_height,
                    };
                    bool old_valid = text_widget_clip_local_rect(text, &old_bbox);
                    bool new_valid = text_widget_clip_local_rect(text, &new_bbox);
                    if (old_valid || new_valid) {
                        esp_video_render_rect_t dirty_local = old_valid && new_valid ? rect_union(&old_bbox, &new_bbox) :
                                                                                     (old_valid ? old_bbox : new_bbox);
                        text_widget_mark_local_dirty(text, &dirty_local, false);
                    } else {
                        text_widget_mark_dirty(text, false);
                    }
                } else {
                    text_widget_mark_dirty(text, false);
                }
            }
        }
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t esp_vui_text_widget_set_overflow(esp_vui_widget_t *widget, int overflow_mode)
{
    if (widget == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_widget_ctx_t *text = (text_widget_ctx_t *)widget;
    if (text->scroll_enable &&
        (text->scroll_mode == SCROLL_MODE_HORIZONTAL || text->scroll_mode == SCROLL_MODE_CIRCULAR) &&
        overflow_mode == OVERFLOW_WRAP) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    if (text->overflow_mode != overflow_mode) {
        text->overflow_mode = overflow_mode;
        text_widget_mark_dirty(text, true);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}
