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
 * @brief  Font resource handle - shared among multiple text widgets
 */
typedef void *text_font_resource_handle_t;

/**
 * @brief  Font resource configuration
 */
typedef struct {
    const char    *font_path;      /*!< Path to font file (NULL if using font_mem) */
    const uint8_t *font_mem;       /*!< Font data in memory (NULL if using font_path) */
    int            font_mem_size;  /*!< Size of font data in memory (0 if using font_path) */
    const char    *font_name;      /*!< Font name (for identification and caching) */
    int            font_size;      /*!< Font size in pixels */
} text_font_resource_cfg_t;

/**
 * @brief  Create a font resource (shared among multiple widgets)
 *
 * @param[in]   cfg     Font resource configuration
 * @param[out]  handle  Font resource handle to store
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t text_font_resource_create(const text_font_resource_cfg_t *cfg,
                                                 text_font_resource_handle_t *handle);

/**
 * @brief  Get an existing font resource by name (if already created)
 *
 * @param[in]   font_name  Font name (used as cache key)
 * @param[in]   font_size  Font size
 * @param[out]  handle     Font resource handle to store (NULL if not found)
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           Font resource found
 *       - ESP_VIDEO_RENDER_ERR_NOT_FOUND    Font resource not found
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t text_font_resource_get(const char *font_name, int font_size,
                                              text_font_resource_handle_t *handle);

/**
 * @brief  Increase reference count of font resource
 *
 * @param[in]  handle  Font resource handle
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_video_render_err_t text_font_resource_add_ref(text_font_resource_handle_t handle);

/**
 * @brief  Decrease reference count and destroy font resource if count reaches 0
 *
 * @param[in]  handle  Font resource handle
 */
void text_font_resource_release(text_font_resource_handle_t handle);

/**
 * @brief  Set font size for a font resource (will create new face if size changed)
 *
 * @param[in]  handle     Font resource handle
 * @param[in]  font_size  New font size
 *
 * @return
 *       - ESP_VIDEO_RENDER_ERR_OK           On success
 *       - ESP_VIDEO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_VIDEO_RENDER_ERR_NO_MEM       Not enough memory
 */
esp_video_render_err_t text_font_resource_set_size(text_font_resource_handle_t handle, int font_size);

/**
 * @brief  Get font size of a font resource
 *
 * @param[in]  handle  Font resource handle
 *
 * @return
 *       - Font  size on success
 *       - 0     if handle is invalid
 */
int text_font_resource_get_size(text_font_resource_handle_t handle);

/**
 * @brief  Cleanup all font resources (for testing/debugging)
 */
void text_font_resource_cleanup_all(void);

// Internal APIs for text_render.c
#include <ft2build.h>
#include FT_FREETYPE_H

/**
 * @brief  Get FreeType face from resource handle (internal use)
 *
 * @param[in]  handle  Font resource handle
 *
 * @return
 *       - FreeType  face pointer on success
 *       - NULL      on failure
 */
FT_Face text_font_resource_get_face(text_font_resource_handle_t handle);

/**
 * @brief  Get shared FreeType library (internal use)
 *
 * @return
 *       - FreeType  library pointer on success
 *       - NULL      on failure
 */
FT_Library text_font_resource_get_library(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
