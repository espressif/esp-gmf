/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "text_render.h"
#include "text_font_resource.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include FT_BITMAP_H

#define TAG  "TEXT_RENDER"

// Helper macros for RGB565
#define RGB565_PACK(r, g, b)  ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define SWAP_EDIAN(x)         (((x) << 8) | ((x) >> 8))

/**
 * @brief  Text render context
 */
typedef struct {
    text_font_resource_handle_t  font_resource;   /*!< Shared font resource (or NULL if using direct loading) */
    FT_Face                      face;            /*!< FreeType face (from resource or direct) */
    FT_Library                   library;         /*!< FreeType library (from resource or direct) */
    int                          font_size;       /*!< Font size in pixels */
    esp_video_render_clr_t       text_color;      /*!< Text color */
    esp_video_render_clr_t       bg_color;        /*!< Background color */
    bool                         bg_fill;         /*!< Whether to fill background */
    bool                         antialiasing;    /*!< Whether to enable antialiasing */
    bool                         initialized;     /*!< Whether the renderer is initialized */
    bool                         using_resource;  /*!< True if using shared resource, false if direct loading */
} text_render_ctx_t;

static uint32_t utf8_to_codepoint(const unsigned char **s)
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

esp_video_render_err_t text_render_init(const text_render_cfg_t *cfg, text_render_handle_t *handle)
{
    if (cfg == NULL || handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    text_render_ctx_t *ctx = (text_render_ctx_t *)calloc(1, sizeof(text_render_ctx_t));
    if (ctx == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }

    // Try to use shared font resource first (if font_name is provided)
    if (cfg->font_name != NULL && strlen(cfg->font_name) > 0) {
        text_font_resource_handle_t resource = NULL;
        esp_video_render_err_t ret = text_font_resource_get(cfg->font_name, cfg->font_size, &resource);
        if (ret == ESP_VIDEO_RENDER_ERR_OK && resource != NULL) {
            // Use existing shared resource
            ctx->font_resource = resource;
            ctx->face = text_font_resource_get_face(resource);
            ctx->library = text_font_resource_get_library();
            ctx->font_size = text_font_resource_get_size(resource);
            ctx->using_resource = true;
            ctx->initialized = true;

            ctx->text_color = cfg->text_color;
            ctx->bg_color = cfg->bg_color;
            ctx->bg_fill = cfg->bg_fill;
            ctx->antialiasing = cfg->antialiasing;

            *handle = (text_render_handle_t)ctx;
            ESP_LOGI(TAG, "Text renderer using shared font resource: %s, size: %d", cfg->font_name, ctx->font_size);
            if (ctx->face && ((ctx->face->face_flags & FT_FACE_FLAG_COLOR) == 0)) {
                ESP_LOGI(TAG, "Font '%s' is NOT a color font (no FT_FACE_FLAG_COLOR). Color emoji will render as monochrome.", cfg->font_name);
            }
            return ESP_VIDEO_RENDER_ERR_OK;
        }

        // Resource doesn't exist, create new one
        text_font_resource_cfg_t resource_cfg = {
            .font_path = cfg->font_path,
            .font_mem = cfg->font_mem,
            .font_mem_size = cfg->font_mem_size,
            .font_name = cfg->font_name,
            .font_size = cfg->font_size};

        ret = text_font_resource_create(&resource_cfg, &resource);
        if (ret == ESP_VIDEO_RENDER_ERR_OK) {
            // Use newly created shared resource
            ctx->font_resource = resource;
            ctx->face = text_font_resource_get_face(resource);
            ctx->library = text_font_resource_get_library();
            ctx->font_size = text_font_resource_get_size(resource);
            ctx->using_resource = true;
            ctx->initialized = true;

            ctx->text_color = cfg->text_color;
            ctx->bg_color = cfg->bg_color;
            ctx->bg_fill = cfg->bg_fill;
            ctx->antialiasing = cfg->antialiasing;

            *handle = (text_render_handle_t)ctx;
            ESP_LOGI(TAG, "Text renderer created and using shared font resource: %s, size: %d", cfg->font_name, ctx->font_size);
            if (ctx->face && cfg->font_name && ((ctx->face->face_flags & FT_FACE_FLAG_COLOR) == 0)) {
                ESP_LOGI(TAG, "Font '%s' is NOT a color font (no FT_FACE_FLAG_COLOR). Color emoji will render as monochrome.", cfg->font_name);
            }
            return ESP_VIDEO_RENDER_ERR_OK;
        }
        // Fall through to direct loading if resource creation fails
    }

    // Fallback: Direct font loading (legacy mode, no sharing)
    // Initialize FreeType
    FT_Error error = FT_Init_FreeType(&ctx->library);
    if (error) {
        ESP_LOGE(TAG, "Failed to initialize FreeType: %d", error);
        free(ctx);
        return ESP_VIDEO_RENDER_ERR_FAIL;
    }

    // Load font face - support both file path and memory-based loading
    if (cfg->font_mem != NULL && cfg->font_mem_size > 0) {
        // Load font from memory (like esp_emote_gfx pattern)
        error = FT_New_Memory_Face(ctx->library, cfg->font_mem, cfg->font_mem_size, 0, &ctx->face);
        if (error) {
            const char *name = cfg->font_name ? cfg->font_name : "memory";
            ESP_LOGE(TAG, "Failed to load font from memory (%s): %d", name, error);
            FT_Done_FreeType(ctx->library);
            free(ctx);
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
        ESP_LOGI(TAG, "Loaded font from memory (direct): %s, size: %d",
                 cfg->font_name ? cfg->font_name : "unknown", cfg->font_mem_size);
    } else if (cfg->font_path != NULL && strlen(cfg->font_path) > 0) {
        // Load font from file path
        error = FT_New_Face(ctx->library, cfg->font_path, 0, &ctx->face);
        if (error) {
            ESP_LOGE(TAG, "Failed to load font from %s: %d", cfg->font_path, error);
            FT_Done_FreeType(ctx->library);
            free(ctx);
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
        ESP_LOGI(TAG, "Loaded font from file (direct): %s", cfg->font_path);
    } else {
        ESP_LOGE(TAG, "Either font_path or font_mem must be provided");
        FT_Done_FreeType(ctx->library);
        free(ctx);
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    // Set font size
    ctx->font_size = cfg->font_size > 0 ? cfg->font_size : 16;
    error = FT_Set_Pixel_Sizes(ctx->face, 0, ctx->font_size);
    if (error) {
        // Bitmap-only emoji fonts may only provide fixed strike sizes.
        if (ctx->face && ctx->face->num_fixed_sizes > 0 && ctx->face->available_sizes) {
            int best_idx = 0;
            int best_px = 0;
            int best_diff = 0x7fffffff;
            for (int i = 0; i < ctx->face->num_fixed_sizes; i++) {
                int px = (int)(ctx->face->available_sizes[i].y_ppem >> 6);
                if (px <= 0) {
                    px = (int)ctx->face->available_sizes[i].height;
                }
                int diff = px - ctx->font_size;
                if (diff < 0) {
                    diff = -diff;
                }
                if (diff < best_diff) {
                    best_diff = diff;
                    best_idx = i;
                    best_px = px;
                }
            }
            if (FT_Select_Size(ctx->face, best_idx) == 0) {
                ESP_LOGW(TAG, "Font '%s' does not support pixel size=%d; selected fixed strike=%d instead",
                         cfg->font_name ? cfg->font_name : "memory", ctx->font_size, best_px);
                ctx->font_size = best_px;
                error = 0;
            }
        }
        if (error) {
            ESP_LOGE(TAG, "Failed to set font size: %d", error);
            FT_Done_Face(ctx->face);
            FT_Done_FreeType(ctx->library);
            free(ctx);
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
    }

    ctx->text_color = cfg->text_color;
    ctx->bg_color = cfg->bg_color;
    ctx->bg_fill = cfg->bg_fill;
    ctx->antialiasing = cfg->antialiasing;
    ctx->using_resource = false;
    ctx->initialized = true;

    *handle = (text_render_handle_t)ctx;
    const char *font_name = cfg->font_name ? cfg->font_name : (cfg->font_path ? cfg->font_path : "memory");
    ESP_LOGI(TAG, "Text renderer initialized with direct font: %s, size: %d", font_name, ctx->font_size);
    if (ctx->face && font_name && ((ctx->face->face_flags & FT_FACE_FLAG_COLOR) == 0)) {
        ESP_LOGI(TAG, "Font '%s' is NOT a color font (no FT_FACE_FLAG_COLOR). Color emoji will render as monochrome.", font_name);
    }

    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t text_render_init_with_resource(void *font_resource, const text_render_cfg_t *cfg, text_render_handle_t *handle)
{
    if (font_resource == NULL || cfg == NULL || handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    text_render_ctx_t *ctx = (text_render_ctx_t *)calloc(1, sizeof(text_render_ctx_t));
    if (ctx == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }

    // Use shared font resource
    ctx->font_resource = (text_font_resource_handle_t)font_resource;
    ctx->face = text_font_resource_get_face(ctx->font_resource);
    ctx->library = text_font_resource_get_library();
    ctx->font_size = text_font_resource_get_size(ctx->font_resource);
    ctx->using_resource = true;

    ctx->text_color = cfg->text_color;
    ctx->bg_color = cfg->bg_color;
    ctx->bg_fill = cfg->bg_fill;
    ctx->antialiasing = cfg->antialiasing;
    ctx->initialized = true;

    // Increase reference count
    text_font_resource_add_ref(ctx->font_resource);

    *handle = (text_render_handle_t)ctx;
    ESP_LOGI(TAG, "Text renderer initialized with shared font resource, size: %d", ctx->font_size);

    return ESP_VIDEO_RENDER_ERR_OK;
}

void text_render_deinit(text_render_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    text_render_ctx_t *ctx = (text_render_ctx_t *)handle;
    if (ctx->initialized) {
        if (ctx->using_resource && ctx->font_resource != NULL) {
            // Release shared font resource (decrements ref count)
            text_font_resource_release(ctx->font_resource);
        } else {
            // Direct loading - cleanup face and library
            FT_Done_Face(ctx->face);
            FT_Done_FreeType(ctx->library);
        }
    }
    free(ctx);
}

esp_video_render_err_t text_render_set_font_size(text_render_handle_t handle, int size)
{
    if (handle == NULL || size <= 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    text_render_ctx_t *ctx = (text_render_ctx_t *)handle;
    if (!ctx->initialized) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }

    if (ctx->using_resource && ctx->font_resource != NULL) {
        // Update shared resource size (affects all widgets using this resource)
        esp_video_render_err_t ret = text_font_resource_set_size(ctx->font_resource, size);
        if (ret == ESP_VIDEO_RENDER_ERR_OK) {
            ctx->font_size = size;
            ctx->face = text_font_resource_get_face(ctx->font_resource);  // Refresh face pointer
        }
        return ret;
    } else {
        // Direct loading - update size locally
        ctx->font_size = size;
        FT_Error error = FT_Set_Pixel_Sizes(ctx->face, 0, size);
        if (error) {
            ESP_LOGE(TAG, "Failed to set font size: %d", error);
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
        return ESP_VIDEO_RENDER_ERR_OK;
    }
}

void text_render_set_text_color(text_render_handle_t handle, esp_video_render_clr_t *color)
{
    if (handle == NULL || color == NULL) {
        return;
    }

    text_render_ctx_t *ctx = (text_render_ctx_t *)handle;
    ctx->text_color = *color;
}

void text_render_set_bg_color(text_render_handle_t handle, esp_video_render_clr_t *color, bool fill)
{
    if (handle == NULL || color == NULL) {
        return;
    }

    text_render_ctx_t *ctx = (text_render_ctx_t *)handle;
    ctx->bg_color = *color;
    ctx->bg_fill = fill;
}

esp_video_render_err_t text_render_get_metrics(text_render_handle_t handle, const char *text, text_render_metrics_t *metrics)
{
    if (handle == NULL || text == NULL || metrics == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    text_render_ctx_t *ctx = (text_render_ctx_t *)handle;
    if (!ctx->initialized) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }

    memset(metrics, 0, sizeof(text_render_metrics_t));

    int min_y = 0;
    int max_y = 0;
    int advance_x = 0;

    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        uint32_t codepoint = utf8_to_codepoint(&p);
        FT_UInt glyph_index = FT_Get_Char_Index(ctx->face, codepoint);

        if (glyph_index == 0 && codepoint != 0) {
            continue;  // Skip missing characters
        }

        FT_Error error = FT_Load_Glyph(ctx->face, glyph_index, FT_LOAD_COLOR);
        if (error) {
            continue;
        }

        FT_GlyphSlot slot = ctx->face->glyph;
        advance_x += slot->advance.x >> 6;  // Convert from 26.6 fixed point

        int glyph_height = slot->metrics.height >> 6;
        int glyph_top = slot->metrics.horiBearingY >> 6;
        int glyph_bottom = glyph_top - glyph_height;

        if (glyph_top > max_y) {
            max_y = glyph_top;
        }
        if (glyph_bottom < min_y) {
            min_y = glyph_bottom;
        }
    }

    metrics->width = advance_x;
    metrics->height = max_y - min_y;
    metrics->baseline = max_y;
    metrics->advance_x = advance_x;
    metrics->advance_y = 0;

    return ESP_VIDEO_RENDER_ERR_OK;
}

static inline void normalize_viewport(const esp_video_render_rect_t *vp,
                                      int w, int h,
                                      int *x0, int *y0, int *x1, int *y1)
{
    if (vp == NULL) {
        *x0 = 0;
        *y0 = 0;
        *x1 = w > 0 ? (w - 1) : 0;
        *y1 = h > 0 ? (h - 1) : 0;
        return;
    }
    if (vp->width == 0 || vp->height == 0) {
        *x0 = 0;
        *y0 = 0;
        *x1 = -1;
        *y1 = -1;
        return;
    }
    int lx0 = vp->x;
    int ly0 = vp->y;
    int lx1 = (int)vp->x + vp->width - 1;
    int ly1 = (int)vp->y + vp->height - 1;
    if (lx0 < 0) {
        lx0 = 0;
    }
    if (ly0 < 0) {
        ly0 = 0;
    }
    if (lx1 >= w) {
        lx1 = w - 1;
    }
    if (ly1 >= h) {
        ly1 = h - 1;
    }
    if (lx1 < lx0) {
        lx1 = lx0;
    }
    if (ly1 < ly0) {
        ly1 = ly0;
    }
    *x0 = lx0;
    *y0 = ly0;
    *x1 = lx1;
    *y1 = ly1;
}

static esp_video_render_err_t render_glyph_loaded_viewport(text_render_ctx_t *ctx,
                                                           uint8_t *buffer,
                                                           esp_video_render_format_t format,
                                                           int buffer_width,
                                                           int buffer_height,
                                                           int pitch,
                                                           int base_x,
                                                           int base_y,
                                                           FT_GlyphSlot slot,
                                                           const FT_Bitmap *bitmap,
                                                           esp_video_render_clr_t *color,
                                                           int vp_x0, int vp_y0, int vp_x1, int vp_y1)
{
    if (ctx == NULL || buffer == NULL || color == NULL || slot == NULL || bitmap == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    if (bitmap->width == 0 || bitmap->rows == 0) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    int bytes_per_pixel = (format == ESP_VIDEO_RENDER_FORMAT_RGB565 || format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) ? 2 : 3;
    if (pitch == 0) {
        pitch = buffer_width * bytes_per_pixel;
    }

    uint16_t rgb565 = 0;
    uint16_t rgb565_be = 0;
    if (format == ESP_VIDEO_RENDER_FORMAT_RGB565 || format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
        rgb565 = RGB565_PACK(color->r, color->g, color->b);
        if (format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
            rgb565_be = SWAP_EDIAN(rgb565);
        }
    }

    // Glyph top-left in buffer coords
    const int glyph_x0 = base_x + slot->bitmap_left;
    const int glyph_y0 = base_y - slot->bitmap_top;
    const int glyph_x1 = glyph_x0 + (int)bitmap->width - 1;
    const int glyph_y1 = glyph_y0 + (int)bitmap->rows - 1;

    // Intersect glyph bbox with viewport and buffer bounds
    int draw_x0 = glyph_x0;
    int draw_y0 = glyph_y0;
    int draw_x1 = glyph_x1;
    int draw_y1 = glyph_y1;
    if (draw_x0 < 0) {
        draw_x0 = 0;
    }
    if (draw_y0 < 0) {
        draw_y0 = 0;
    }
    if (draw_x1 >= buffer_width) {
        draw_x1 = buffer_width - 1;
    }
    if (draw_y1 >= buffer_height) {
        draw_y1 = buffer_height - 1;
    }

    if (draw_x0 < vp_x0) {
        draw_x0 = vp_x0;
    }
    if (draw_y0 < vp_y0) {
        draw_y0 = vp_y0;
    }
    if (draw_x1 > vp_x1) {
        draw_x1 = vp_x1;
    }
    if (draw_y1 > vp_y1) {
        draw_y1 = vp_y1;
    }

    if (draw_x1 < draw_x0 || draw_y1 < draw_y0) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    // FreeType can return bitmaps with a negative pitch (rows stored bottom-to-top).
    // Always compute a safe row pointer using pitch sign.
    const int bm_pitch = bitmap->pitch;
    const int bm_pitch_abs = bm_pitch >= 0 ? bm_pitch : -bm_pitch;
    const int bm_rows = (int)bitmap->rows;
    const uint8_t *bm_buf = bitmap->buffer;

    for (int py = draw_y0; py <= draw_y1; py++) {
        const int row = py - glyph_y0;
        const int bm_row = (bm_pitch >= 0) ? row : (bm_rows - 1 - row);
        const uint8_t *row_ptr = bm_buf + (size_t)bm_row * (size_t)bm_pitch_abs;
        for (int px = draw_x0; px <= draw_x1; px++) {
            const int col = px - glyph_x0;

            uint8_t alpha = 0;
            if (bitmap->pixel_mode == FT_PIXEL_MODE_GRAY) {
                alpha = row_ptr[col];
            } else if (bitmap->pixel_mode == FT_PIXEL_MODE_MONO) {
                uint8_t byte = row_ptr[col / 8];
                alpha = (byte & (0x80 >> (col % 8))) ? 255 : 0;
            } else if (bitmap->pixel_mode == FT_PIXEL_MODE_BGRA) {
                // Color glyph (e.g. Noto Emoji). Buffer is BGRA (4 bytes per pixel).
                const uint8_t *src = row_ptr + col * 4;
                uint8_t b = src[0];
                uint8_t g = src[1];
                uint8_t r = src[2];
                alpha = src[3];
                if (alpha == 0) {
                    continue;
                }

                uint8_t *pixel_ptr = buffer + py * pitch + px * bytes_per_pixel;
                // Alpha blend into destination
                if (format == ESP_VIDEO_RENDER_FORMAT_RGB565 || format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
                    uint16_t dst565 = *((uint16_t *)pixel_ptr);
                    if (format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
                        dst565 = SWAP_EDIAN(dst565);
                    }
                    // Unpack RGB565 to 8-bit
                    uint8_t dr = (uint8_t)(((dst565 >> 11) & 0x1F) * 255 / 31);
                    uint8_t dg = (uint8_t)(((dst565 >> 5) & 0x3F) * 255 / 63);
                    uint8_t db = (uint8_t)((dst565 & 0x1F) * 255 / 31);
                    uint8_t oa = alpha;
                    uint8_t nr = (uint8_t)((dr * (255 - oa) + r * oa) / 255);
                    uint8_t ng = (uint8_t)((dg * (255 - oa) + g * oa) / 255);
                    uint8_t nb = (uint8_t)((db * (255 - oa) + b * oa) / 255);
                    uint16_t out565 = RGB565_PACK(nr, ng, nb);
                    if (format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
                        out565 = SWAP_EDIAN(out565);
                    }
                    *((uint16_t *)pixel_ptr) = out565;
                } else if (format == ESP_VIDEO_RENDER_FORMAT_RGB888) {
                    uint8_t dr = pixel_ptr[0];
                    uint8_t dg = pixel_ptr[1];
                    uint8_t db = pixel_ptr[2];
                    uint8_t oa = alpha;
                    pixel_ptr[0] = (uint8_t)((dr * (255 - oa) + r * oa) / 255);
                    pixel_ptr[1] = (uint8_t)((dg * (255 - oa) + g * oa) / 255);
                    pixel_ptr[2] = (uint8_t)((db * (255 - oa) + b * oa) / 255);
                } else if (format == ESP_VIDEO_RENDER_FORMAT_BGR888) {
                    uint8_t db = pixel_ptr[0];
                    uint8_t dg = pixel_ptr[1];
                    uint8_t dr = pixel_ptr[2];
                    uint8_t oa = alpha;
                    pixel_ptr[0] = (uint8_t)((db * (255 - oa) + b * oa) / 255);
                    pixel_ptr[1] = (uint8_t)((dg * (255 - oa) + g * oa) / 255);
                    pixel_ptr[2] = (uint8_t)((dr * (255 - oa) + r * oa) / 255);
                }
                continue;
            }
            if (alpha == 0) {
                continue;
            }

            uint8_t *pixel_ptr = buffer + py * pitch + px * bytes_per_pixel;
            if (alpha == 255 || alpha > 128) {
                switch (format) {
                    case ESP_VIDEO_RENDER_FORMAT_RGB565:
                        *((uint16_t *)pixel_ptr) = rgb565;
                        break;
                    case ESP_VIDEO_RENDER_FORMAT_RGB565_BE:
                        *((uint16_t *)pixel_ptr) = rgb565_be;
                        break;
                    case ESP_VIDEO_RENDER_FORMAT_RGB888:
                        pixel_ptr[0] = color->r;
                        pixel_ptr[1] = color->g;
                        pixel_ptr[2] = color->b;
                        break;
                    case ESP_VIDEO_RENDER_FORMAT_BGR888:
                        pixel_ptr[0] = color->b;
                        pixel_ptr[1] = color->g;
                        pixel_ptr[2] = color->r;
                        break;
                    default:
                        break;
                }
            }
        }
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t text_render_draw_internal(text_render_handle_t handle,
                                                        const char *text,
                                                        uint8_t *buffer,
                                                        esp_video_render_format_t format,
                                                        int width,
                                                        int height,
                                                        int pitch,
                                                        int x,
                                                        int y,
                                                        bool with_shadow,
                                                        esp_video_render_clr_t *shadow_color,
                                                        int shadow_offset_x,
                                                        int shadow_offset_y,
                                                        const esp_video_render_rect_t *viewport)
{
    if (handle == NULL || text == NULL || buffer == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_render_ctx_t *ctx = (text_render_ctx_t *)handle;
    if (!ctx->initialized) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }
    int vp_x0, vp_y0, vp_x1, vp_y1;
    normalize_viewport(viewport, width, height, &vp_x0, &vp_y0, &vp_x1, &vp_y1);

    int current_x = x;
    int current_y = y;
    const unsigned char *p = (const unsigned char *)text;
    int line_height = ctx->font_size + (ctx->font_size / 4);
    while (*p) {
        uint32_t codepoint = utf8_to_codepoint(&p);
        if (codepoint == '\n') {
            current_x = x;
            current_y += line_height;
            if (current_y >= y + height) {
                break;
            }
            continue;
        }
        if (current_x >= x + width) {
            current_x = x;
            current_y += line_height;
            if (current_y >= y + height) {
                break;
            }
        }

        FT_UInt glyph_index = FT_Get_Char_Index(ctx->face, codepoint);
        if (glyph_index == 0 && codepoint != 0) {
            current_x += ctx->font_size / 2;
            continue;
        }

        // Color fonts (COLR/CPAL, SVG, CBDT/CBLC) can behave differently depending on FreeType build
        // options. We try a robust sequence:
        // - First try FT_LOAD_RENDER|FT_LOAD_COLOR: this is required for many CBDT/CBLC bitmap strikes
        //   to actually materialize a bitmap (often BGRA).
        // - If not a bitmap, fall back to FT_Load_Glyph(FT_LOAD_DEFAULT|FT_LOAD_COLOR) + FT_Render_Glyph.
        int load_flags = FT_LOAD_COLOR | FT_LOAD_RENDER;
        FT_Error error = FT_Load_Glyph(ctx->face, glyph_index, load_flags);
        if (error) {
            // Skip glyph but still advance using a reasonable fallback
            current_x += ctx->font_size / 2;
            continue;
        }

        FT_GlyphSlot slot = ctx->face->glyph;
        // Ensure we have a bitmap in slot->bitmap.
        // For color glyphs we always use NORMAL (MONO would destroy color layers/alpha).
        if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
            if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
                // Retry with FT_LOAD_RENDER|FT_LOAD_COLOR (some builds/fonts need this).
                error = FT_Load_Glyph(ctx->face, glyph_index, FT_LOAD_DEFAULT | FT_LOAD_RENDER | FT_LOAD_COLOR);
                if (error == 0) {
                    slot = ctx->face->glyph;
                }
            }
        }
        if (error) {
            current_x += ctx->font_size / 2;
            continue;
        }
        FT_Bitmap *bitmap = &slot->bitmap;
        if (bitmap->width == 0 || bitmap->rows == 0) {
            // Nothing to draw (can happen if COLR/SVG renderer is not available in this FreeType build)
            current_x += ctx->font_size / 2;
            continue;
        }
        // Quick reject: glyph bbox vs viewport (use baseline position)
        int gx0 = current_x + slot->bitmap_left;
        int gy0 = current_y - slot->bitmap_top;
        int gx1 = gx0 + (int)bitmap->width - 1;
        int gy1 = gy0 + (int)bitmap->rows - 1;

        bool intersects = !(gx1 < vp_x0 || gx0 > vp_x1 || gy1 < vp_y0 || gy0 > vp_y1);

        if (intersects) {
            if (with_shadow && shadow_color) {
                // Shadow bbox might differ due to offsets
                int sx0 = gx0 + shadow_offset_x;
                int sy0 = gy0 + shadow_offset_y;
                int sx1 = sx0 + (int)bitmap->width - 1;
                int sy1 = sy0 + (int)bitmap->rows - 1;
                bool s_intersects = !(sx1 < vp_x0 || sx0 > vp_x1 || sy1 < vp_y0 || sy0 > vp_y1);
                if (s_intersects) {
                    (void)render_glyph_loaded_viewport(ctx, buffer, format, width, height, pitch,
                                                       current_x + shadow_offset_x, current_y + shadow_offset_y,
                                                       slot, bitmap, shadow_color, vp_x0, vp_y0, vp_x1, vp_y1);
                }
            }
            (void)render_glyph_loaded_viewport(ctx, buffer, format, width, height, pitch,
                                               current_x, current_y,
                                               slot, bitmap, &ctx->text_color, vp_x0, vp_y0, vp_x1, vp_y1);
        }

        // Advance to next character
        current_x += slot->advance.x >> 6;
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t text_render_draw_codepoint_internal(text_render_handle_t handle,
                                                                  uint32_t codepoint,
                                                                  uint8_t *buffer,
                                                                  esp_video_render_format_t format,
                                                                  int width,
                                                                  int height,
                                                                  int pitch,
                                                                  int x,
                                                                  int y,
                                                                  bool with_shadow,
                                                                  esp_video_render_clr_t *shadow_color,
                                                                  int shadow_offset_x,
                                                                  int shadow_offset_y,
                                                                  const esp_video_render_rect_t *viewport)
{
    if (handle == NULL || buffer == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    text_render_ctx_t *ctx = (text_render_ctx_t *)handle;
    if (!ctx->initialized) {
        return ESP_VIDEO_RENDER_ERR_INVALID_STATE;
    }

    int vp_x0, vp_y0, vp_x1, vp_y1;
    normalize_viewport(viewport, width, height, &vp_x0, &vp_y0, &vp_x1, &vp_y1);

    FT_UInt glyph_index = FT_Get_Char_Index(ctx->face, codepoint);
    if (glyph_index == 0 && codepoint != 0) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    // Robust color/bitmap glyph load
    int load_flags = FT_LOAD_COLOR | FT_LOAD_RENDER;
    FT_Error error = FT_Load_Glyph(ctx->face, glyph_index, load_flags);
    if (error) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    FT_GlyphSlot slot = ctx->face->glyph;
    if (slot->format != FT_GLYPH_FORMAT_BITMAP) {
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) {
            error = FT_Load_Glyph(ctx->face, glyph_index, FT_LOAD_DEFAULT | FT_LOAD_RENDER | FT_LOAD_COLOR);
            if (error == 0) {
                slot = ctx->face->glyph;
            }
        }
    }
    if (error) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    FT_Bitmap *bitmap = &slot->bitmap;
    if (bitmap->width == 0 || bitmap->rows == 0) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    // Quick reject
    int gx0 = x + slot->bitmap_left;
    int gy0 = y - slot->bitmap_top;
    int gx1 = gx0 + (int)bitmap->width - 1;
    int gy1 = gy0 + (int)bitmap->rows - 1;
    bool intersects = !(gx1 < vp_x0 || gx0 > vp_x1 || gy1 < vp_y0 || gy0 > vp_y1);
    if (!intersects) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    if (with_shadow && shadow_color) {
        int sx0 = gx0 + shadow_offset_x;
        int sy0 = gy0 + shadow_offset_y;
        int sx1 = sx0 + (int)bitmap->width - 1;
        int sy1 = sy0 + (int)bitmap->rows - 1;
        bool s_intersects = !(sx1 < vp_x0 || sx0 > vp_x1 || sy1 < vp_y0 || sy0 > vp_y1);
        if (s_intersects) {
            (void)render_glyph_loaded_viewport(ctx, buffer, format, width, height, pitch,
                                               x + shadow_offset_x, y + shadow_offset_y,
                                               slot, bitmap, shadow_color, vp_x0, vp_y0, vp_x1, vp_y1);
        }
    }

    (void)render_glyph_loaded_viewport(ctx, buffer, format, width, height, pitch,
                                       x, y, slot, bitmap, &ctx->text_color,
                                       vp_x0, vp_y0, vp_x1, vp_y1);
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t text_render_draw(text_render_handle_t handle,
                                        const char *text,
                                        uint8_t *buffer,
                                        esp_video_render_format_t format,
                                        int width,
                                        int height,
                                        int pitch,
                                        int x,
                                        int y)
{
    return text_render_draw_internal(handle, text, buffer, format, width, height, pitch, x, y,
                                     false, NULL, 0, 0, NULL);
}

esp_video_render_err_t text_render_draw_viewport(text_render_handle_t handle,
                                                 const char *text,
                                                 uint8_t *buffer,
                                                 esp_video_render_format_t format,
                                                 int width,
                                                 int height,
                                                 int pitch,
                                                 int x,
                                                 int y,
                                                 const esp_video_render_rect_t *viewport)
{
    return text_render_draw_internal(handle, text, buffer, format, width, height, pitch, x, y,
                                     false, NULL, 0, 0, viewport);
}

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
                                                    int shadow_offset_y)
{
    if (shadow_color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    return text_render_draw_internal(handle, text, buffer, format, width, height, pitch, x, y,
                                     true, shadow_color, shadow_offset_x, shadow_offset_y, NULL);
}

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
                                                             const esp_video_render_rect_t *viewport)
{
    if (shadow_color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    return text_render_draw_internal(handle, text, buffer, format, width, height, pitch, x, y,
                                     true, shadow_color, shadow_offset_x, shadow_offset_y, viewport);
}

esp_video_render_err_t text_render_draw_codepoint_viewport(text_render_handle_t handle,
                                                           uint32_t codepoint,
                                                           uint8_t *buffer,
                                                           esp_video_render_format_t format,
                                                           int width,
                                                           int height,
                                                           int pitch,
                                                           int x,
                                                           int y,
                                                           const esp_video_render_rect_t *viewport)
{
    return text_render_draw_codepoint_internal(handle, codepoint, buffer, format, width, height, pitch,
                                               x, y, false, NULL, 0, 0, viewport);
}

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
                                                                       const esp_video_render_rect_t *viewport)
{
    if (shadow_color == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    return text_render_draw_codepoint_internal(handle, codepoint, buffer, format, width, height, pitch,
                                               x, y, true, shadow_color, shadow_offset_x, shadow_offset_y, viewport);
}

// Get character advance width (safe version for size calculation - uses approximation)
int text_render_get_char_width(text_render_handle_t handle, uint32_t codepoint)
{
    if (handle == NULL) {
        return 0;
    }

    text_render_ctx_t *ctx = (text_render_ctx_t *)handle;
    if (!ctx->initialized) {
        return 0;
    }

    // Avoid FT_Load_Glyph to prevent heap corruption during frequent size calculations
    // Use approximate width calculation based on character properties
    FT_UInt glyph_index = FT_Get_Char_Index(ctx->face, codepoint);
    if (glyph_index == 0) {
        // Character not in font - use approximate width
        // Wide characters (CJK, emojis) are typically wider
        if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
            // CJK Unified Ideographs - typically square
            return ctx->font_size;
        } else if ((codepoint >= 0x1F300 && codepoint <= 0x1F9FF) ||
                   (codepoint >= 0x2600 && codepoint <= 0x26FF) ||
                   (codepoint >= 0x2700 && codepoint <= 0x27BF)) {
            // Emojis and symbols - typically square or wider
            return ctx->font_size;
        }
        return ctx->font_size / 2;  // Fallback for other missing characters
    }

    // Use approximate width based on ASCII vs wide characters
    // This avoids FT_Load_Glyph which causes heap corruption
    if (codepoint < 0x80) {
        // ASCII characters - typically narrower
        // Most ASCII letters are about 60% of font size
        return (ctx->font_size * 6) / 10;
    } else if (codepoint >= 0x4E00 && codepoint <= 0x9FFF) {
        // CJK characters - typically square
        return ctx->font_size;
    } else if ((codepoint >= 0x1F300 && codepoint <= 0x1F9FF) ||
               (codepoint >= 0x2600 && codepoint <= 0x26FF) ||
               (codepoint >= 0x2700 && codepoint <= 0x27BF)) {
        // Emojis and symbols - typically square or wider
        return ctx->font_size;
    } else {
        // Other Unicode characters - approximate as 70% of font size
        return (ctx->font_size * 7) / 10;
    }
}

// Get character advance width with real FreeType metrics (for rendering - can use FT_Load_Glyph)
int text_render_get_char_advance_real(text_render_handle_t handle, uint32_t codepoint)
{
    if (handle == NULL) {
        return 0;
    }

    text_render_ctx_t *ctx = (text_render_ctx_t *)handle;
    if (!ctx->initialized) {
        return 0;
    }

    FT_UInt glyph_index = FT_Get_Char_Index(ctx->face, codepoint);
    if (glyph_index == 0) {
        return ctx->font_size / 2;  // Fallback width
    }

    FT_Error error = FT_Load_Glyph(ctx->face, glyph_index, FT_LOAD_COLOR);
    if (error) {
        return ctx->font_size / 2;  // Fallback width
    }

    FT_GlyphSlot slot = ctx->face->glyph;
    return slot->advance.x >> 6;  // Convert from 26.6 fixed point
}

int text_render_get_font_size(text_render_handle_t handle)
{
    if (handle == NULL) {
        return 0;
    }
    text_render_ctx_t *ctx = (text_render_ctx_t *)handle;
    if (!ctx->initialized) {
        return 0;
    }
    return ctx->font_size;
}

bool text_render_is_available(void)
{
    return true;
}
