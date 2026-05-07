/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "text_font_resource.h"
#include "text_render.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>
#include "video_render_sys.h"

#define TAG  \
    "FONT_RESOURCE"

#include <ft2build.h>
#include FT_FREETYPE_H

// Choose the closest available fixed strike for bitmap-only fonts (CBDT/CBLC, sbix).
// Returns true if a fixed size was selected successfully and outputs the selected pixel height.
static bool select_best_fixed_size(FT_Face face, int requested_px, int *out_selected_px)
{
    if (face == NULL || face->num_fixed_sizes <= 0 || face->available_sizes == NULL) {
        return false;
    }
    int best_idx = 0;
    int best_px = 0;
    int best_diff = 0x7fffffff;
    for (int i = 0; i < face->num_fixed_sizes; i++) {
        // available_sizes[i].y_ppem is 26.6 fixed-point.
        int px = (int)(face->available_sizes[i].y_ppem >> 6);
        if (px <= 0) {
            px = (int)face->available_sizes[i].height;
        }
        int diff = px - requested_px;
        if (diff < 0) {
            diff = -diff;
        }
        if (diff < best_diff) {
            best_diff = diff;
            best_idx = i;
            best_px = px;
        }
    }
    if (FT_Select_Size(face, best_idx) != 0) {
        return false;
    }
    if (out_selected_px) {
        *out_selected_px = best_px;
    }
    return true;
}

// Font resource entry
typedef struct font_resource_entry {
    char                        name[64];       // Font name (cache key)
    int                         font_size;      // Font size
    FT_Library                  library;        // FreeType library (shared)
    FT_Face                     face;           // FreeType face
    const uint8_t              *font_mem;       // Font memory (if loaded from memory)
    size_t                      font_mem_size;  // Font memory size
    const char                 *font_path;      // Font path (if loaded from file)
    int                         ref_count;      // Reference count
    struct font_resource_entry *next;           // Linked list for cache
} font_resource_entry_t;

// Global font resource cache
static font_resource_entry_t *g_font_cache = NULL;
static bool g_freetype_initialized = false;
static FT_Library g_shared_library = NULL;

// Initialize shared FreeType library
static esp_video_render_err_t init_shared_freetype(void)
{
    if (g_freetype_initialized) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    FT_Error error = FT_Init_FreeType(&g_shared_library);
    if (error) {
        ESP_LOGE(TAG, "Failed to initialize shared FreeType library: %d", error);
        return ESP_VIDEO_RENDER_ERR_FAIL;
    }

    g_freetype_initialized = true;
    ESP_LOGI(TAG, "Shared FreeType library initialized");
    return ESP_VIDEO_RENDER_ERR_OK;
}

// Generate cache key from font name and size
static void generate_cache_key(char *key, size_t key_size, const char *font_name, uint8_t font_size)
{
    if (font_name) {
        size_t max_name_len = key_size > 20 ? key_size - 20 : 0;
        if (max_name_len > 0) {
            snprintf(key, key_size, "%.*s_%d", (int)max_name_len, font_name, font_size);
        } else {
            snprintf(key, key_size, "%d", font_size);
        }
    } else {
        snprintf(key, key_size, "font_def_%d", font_size);
    }
}

// Find font resource in cache
static font_resource_entry_t *find_font_resource(const char *font_name, uint8_t font_size)
{
    char cache_key[64];
    generate_cache_key(cache_key, sizeof(cache_key), font_name, font_size);

    font_resource_entry_t *entry = g_font_cache;
    while (entry != NULL) {
        char entry_key[64];
        generate_cache_key(entry_key, sizeof(entry_key), entry->name, entry->font_size);
        if (strcmp(entry_key, cache_key) == 0) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

// Load font face
static esp_video_render_err_t load_font_face(font_resource_entry_t *entry, const text_font_resource_cfg_t *cfg)
{
    FT_Error error;

    if (cfg->font_mem != NULL && cfg->font_mem_size > 0) {
        // Load from memory
        error = FT_New_Memory_Face(g_shared_library, cfg->font_mem, cfg->font_mem_size, 0, &entry->face);
        if (error) {
            ESP_LOGE(TAG, "Failed to load font from memory (%s): %d", cfg->font_name ? cfg->font_name : "unknown", error);
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
        entry->font_mem = cfg->font_mem;
        entry->font_mem_size = cfg->font_mem_size;
        entry->font_path = NULL;
        ESP_LOGI(TAG, "Loaded font from memory: %s, size: %zu bytes",
                 cfg->font_name ? cfg->font_name : "unknown", (size_t)cfg->font_mem_size);
    } else if (cfg->font_path != NULL && strlen(cfg->font_path) > 0) {
        // Load from file
        error = FT_New_Face(g_shared_library, cfg->font_path, 0, &entry->face);
        if (error) {
            ESP_LOGE(TAG, "Failed to load font from %s: %d", cfg->font_path, error);
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
        entry->font_path = cfg->font_path;
        entry->font_mem = NULL;
        entry->font_mem_size = 0;
        ESP_LOGI(TAG, "Loaded font from file: %s", cfg->font_path);
    } else {
        ESP_LOGE(TAG, "Either font_path or font_mem must be provided");
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    // Set font size
    entry->font_size = cfg->font_size > 0 ? cfg->font_size : 16;
    error = FT_Set_Pixel_Sizes(entry->face, 0, entry->font_size);
    if (error) {
        // Bitmap-only emoji fonts often only support fixed strike sizes; select the closest.
        int selected_px = 0;
        if (select_best_fixed_size(entry->face, entry->font_size, &selected_px)) {
            ESP_LOGW(TAG, "Font '%s' does not support pixel size=%d; selected fixed strike=%d instead",
                     cfg->font_name ? cfg->font_name : "unknown", entry->font_size, selected_px);
            entry->font_size = selected_px;
        } else {
            ESP_LOGE(TAG, "Failed to set font size: %d", error);
            FT_Done_Face(entry->face);
            entry->face = NULL;
            return ESP_VIDEO_RENDER_ERR_FAIL;
        }
    }

    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t text_font_resource_create(const text_font_resource_cfg_t *cfg,
                                                 text_font_resource_handle_t *handle)
{
    if (cfg == NULL || handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    // Initialize shared FreeType library
    esp_video_render_err_t ret = init_shared_freetype();
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        return ret;
    }

    // Check if font resource already exists in cache
    const char *font_name = cfg->font_name ? cfg->font_name : (cfg->font_path ? cfg->font_path : "memory");
    font_resource_entry_t *existing = find_font_resource(font_name, cfg->font_size);
    if (existing != NULL) {
        // Font already exists, increase ref count and return
        existing->ref_count++;
        *handle = (text_font_resource_handle_t)existing;
        ESP_LOGI(TAG, "Reusing existing font resource: %s (ref_count=%d)", font_name, existing->ref_count);
        return ESP_VIDEO_RENDER_ERR_OK;
    }

    // Create new font resource entry
    font_resource_entry_t *entry = (font_resource_entry_t *)video_render_calloc(1, sizeof(font_resource_entry_t));
    if (entry == NULL) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }

    // Copy font name
    strncpy(entry->name, font_name, sizeof(entry->name) - 1);
    entry->name[sizeof(entry->name) - 1] = '\0';
    entry->font_size = cfg->font_size;
    entry->ref_count = 1;

    // Load font face
    ret = load_font_face(entry, cfg);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        video_render_free(entry);
        return ret;
    }

    // Add to cache
    entry->next = g_font_cache;
    g_font_cache = entry;

    *handle = (text_font_resource_handle_t)entry;
    ESP_LOGI(TAG, "Created font resource: %s, size=%d (ref_count=1)", font_name, cfg->font_size);

    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t text_font_resource_get(const char *font_name, int font_size,
                                              text_font_resource_handle_t *handle)
{
    if (font_name == NULL || handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    font_resource_entry_t *entry = find_font_resource(font_name, font_size);
    if (entry == NULL) {
        *handle = NULL;
        return ESP_VIDEO_RENDER_ERR_NOT_FOUND;
    }

    entry->ref_count++;
    *handle = (text_font_resource_handle_t)entry;
    ESP_LOGI(TAG, "Found font resource: %s (ref_count=%d)", font_name, entry->ref_count);

    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t text_font_resource_add_ref(text_font_resource_handle_t handle)
{
    if (handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }

    font_resource_entry_t *entry = (font_resource_entry_t *)handle;
    entry->ref_count++;
    return ESP_VIDEO_RENDER_ERR_OK;
}

void text_font_resource_release(text_font_resource_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    font_resource_entry_t *entry = (font_resource_entry_t *)handle;
    entry->ref_count--;

    ESP_LOGI(TAG, "Release font resource: %s (ref_count=%d)", entry->name, entry->ref_count);

    if (entry->ref_count <= 0) {
        // Remove from cache
        if (g_font_cache == entry) {
            g_font_cache = entry->next;
        } else {
            font_resource_entry_t *prev = g_font_cache;
            while (prev != NULL && prev->next != entry) {
                prev = prev->next;
            }
            if (prev != NULL) {
                prev->next = entry->next;
            }
        }

        // Destroy font face
        if (entry->face != NULL) {
            FT_Done_Face(entry->face);
        }

        ESP_LOGI(TAG, "Destroyed font resource: %s", entry->name);
        video_render_free(entry);

        // If cache is now empty, clean up shared FreeType library
        if (g_font_cache == NULL && g_freetype_initialized && g_shared_library != NULL) {
            FT_Done_FreeType(g_shared_library);
            g_shared_library = NULL;
            g_freetype_initialized = false;
            ESP_LOGI(TAG, "Cleaned up shared FreeType library (no more font resources)");
        }
    }
}

esp_video_render_err_t text_font_resource_set_size(text_font_resource_handle_t handle, int font_size)
{
    if (handle == NULL || font_size <= 0) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    font_resource_entry_t *entry = (font_resource_entry_t *)handle;
    if (entry->font_size == font_size) {
        return ESP_VIDEO_RENDER_ERR_OK;  // Already set to this size
    }

    // Set new font size
    FT_Error error = FT_Set_Pixel_Sizes(entry->face, 0, font_size);
    if (error) {
        int selected_px = 0;
        if (select_best_fixed_size(entry->face, font_size, &selected_px)) {
            ESP_LOGW(TAG, "Font resource %s does not support pixel size=%d; selected fixed strike=%d instead",
                     entry->name, font_size, selected_px);
            entry->font_size = selected_px;
            return ESP_VIDEO_RENDER_ERR_OK;
        }
        ESP_LOGE(TAG, "Failed to set font size: %d", error);
        return ESP_VIDEO_RENDER_ERR_FAIL;
    }
    entry->font_size = font_size;
    ESP_LOGI(TAG, "Font resource %s size changed to %d", entry->name, font_size);
    return ESP_VIDEO_RENDER_ERR_OK;
}

int text_font_resource_get_size(text_font_resource_handle_t handle)
{
    if (handle == NULL) {
        return 0;
    }
    font_resource_entry_t *entry = (font_resource_entry_t *)handle;
    return entry->font_size;
}

void text_font_resource_cleanup_all(void)
{
    font_resource_entry_t *entry = g_font_cache;
    while (entry != NULL) {
        font_resource_entry_t *next = entry->next;
        if (entry->face != NULL) {
            FT_Done_Face(entry->face);
        }
        video_render_free(entry);
        entry = next;
    }
    g_font_cache = NULL;

    if (g_freetype_initialized && g_shared_library != NULL) {
        FT_Done_FreeType(g_shared_library);
        g_shared_library = NULL;
        g_freetype_initialized = false;
    }

    ESP_LOGI(TAG, "Cleaned up all font resources");
}

// Get FreeType face from resource handle (internal use)
FT_Face text_font_resource_get_face(text_font_resource_handle_t handle)
{
    if (handle == NULL) {
        return NULL;
    }

    font_resource_entry_t *entry = (font_resource_entry_t *)handle;
    return entry->face;
}

// Get FreeType library (shared, internal use)
FT_Library text_font_resource_get_library(void)
{
    return g_shared_library;
}
