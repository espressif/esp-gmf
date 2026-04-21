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
 * @brief  Video render backend handle
 */
typedef void *esp_video_render_backend_handle_t;

/**
 * @brief  Video render LCD type
 */
typedef enum {
    ESP_VIDEO_RENDER_LCD_TYPE_NONE = 0,  /*!< None LCD type */
    ESP_VIDEO_RENDER_LCD_TYPE_DVP  = 1,  /*!< DVP LCD type */
    ESP_VIDEO_RENDER_LCD_TYPE_RGB  = 2,  /*!< RGB LCD type */
    ESP_VIDEO_RENDER_LCD_TYPE_I80  = 3,  /*!< I80 LCD type */
    ESP_VIDEO_RENDER_LCD_TYPE_DPI  = 4,  /*!< DPI LCD type */
} esp_video_render_lcd_type_t;

/**
 * @brief  Video render LCD configuration
 */
typedef struct {
    esp_video_render_lcd_type_t  lcd_type;    /*!< LCD type */
    uint8_t                      fb_num;      /*!< Number of framebuffers */
    esp_video_render_format_t    out_format;  /*!< Output pixel format */
    uint16_t                     width;       /*!< Display width */
    uint16_t                     height;      /*!< Display height */
    void                        *lcd_handle;  /*!< LCD handle of `esp_lcd_panel_handle_t` */
    void                        *io_handle;   /*!< IO handle of `esp_lcd_panel_io_handle_t` */
} esp_video_render_lcd_cfg_t;

/**
 * @brief  Video render backend operations
 */
typedef struct {
    /**
     * @brief  Initialize backend
     *
     * @param[in]   cfg       Configuration
     * @param[in]   cfg_size  Configuration size
     * @param[out]  backend   Backend handle
     *
     * @return
     *       - ESP_VIDEO_RENDER_ERR_OK  On success
     *       - Others                   On failure
     */
    esp_video_render_err_t (*init)(void *cfg, int cfg_size, esp_video_render_backend_handle_t *backend);
    /**
     * @brief  Check if backend uses GRAM
     *
     * @param[in]  backend  Backend handle
     *
     * @return
     *       - true   On backend uses GRAM
     *       - false  On backend does not use GRAM
     */
    bool (*with_gram)(esp_video_render_backend_handle_t backend);

    /**
     * @brief  Get display information
     *
     * @param[in]   backend  Backend handle
     * @param[out]  fb       Display information
     *
     * @return
     *       - ESP_VIDEO_RENDER_ERR_OK  On success
     *       - Others                   On failure
     */
    esp_video_render_err_t (*get_display_info)(esp_video_render_backend_handle_t backend, esp_video_render_disp_info_t *fb);

    /**
     * @brief  Get frame buffer
     *
     * @param[in]   backend  Backend handle
     * @param[out]  fb       Frame buffer
     *
     * @return
     *       - ESP_VIDEO_RENDER_ERR_OK  On success
     *       - Others                   On failure
     */
    esp_video_render_err_t (*get_fb)(esp_video_render_backend_handle_t backend, esp_video_render_fb_t *fb);

    /**
     * @brief  Lock/unlock frame buffer
     *
     * @param[in]  backend  Backend handle
     * @param[in]  fb       Frame buffer
     * @param[in]  lock     Lock/unlock flag: true for lock, false for unlock
     *
     * @return
     *       - ESP_VIDEO_RENDER_ERR_OK  On success
     *       - Others                   On failure
     */
    esp_video_render_err_t (*lock_fb)(esp_video_render_backend_handle_t backend, esp_video_render_fb_t *fb,
                                      bool lock);

    /**
     * @brief  Write frame buffer
     *
     * @note  When position not set, frame buffer resolution need same as display resolution
     *        When support partial update (with gram backend), dirty rectangle can be used to set the partial update region
     *
     *        When position set on backend which supports partial update (with gram backend / partial frame buffer update)
     *        Frame buffer resolution no need to be same as display resolution
     *        Dirty rectangle is used to set the partial update region of input frame buffer
     *
     * @param[in]  backend     Backend handle
     * @param[in]  fb          Frame buffer
     * @param[in]  dirty_rect  Dirty rectangle, set to NULL for full update (optional)
     * @param[in]  pos         Position (optional)
     *
     * @return
     *       - ESP_VIDEO_RENDER_ERR_OK  On success
     *       - Others                   On failure
     */
    esp_video_render_err_t (*write_fb)(esp_video_render_backend_handle_t backend,
                                       esp_video_render_fb_t *fb,
                                       const esp_video_render_rect_t *dirty_rect,
                                       const esp_video_render_pos_t *pos);
    /**
     * @brief  Deinitialize backend
     *
     * @param[in]  backend  Backend handle
     *
     * @return
     *       - ESP_VIDEO_RENDER_ERR_OK  On success
     *       - Others                   On failure
     */
    esp_video_render_err_t (*deinit)(esp_video_render_backend_handle_t backend);  /*!< Deinitialize backend */
} esp_video_render_backend_ops_t;

/**
 * @brief  Get LCD backend operations
 *
 * @return
 *       - Backend  operations pointer on success
 *       - NULL     on failure
 */
const esp_video_render_backend_ops_t *esp_video_render_get_lcd_backend(void);

/**
 * @brief  LVGL backend configuration
 *
 * @note  To avoid forcing LVGL headers on users of this header,
 *        the display pointer is kept as void* here and casted
 *        to lv_disp_t* inside the backend implementation.
 */
typedef struct {
    void                      *lv_disp;     /*!< LVGL display handle (lv_disp_t *) */
    esp_video_render_format_t  out_format;  /*!< Output pixel format (e.g. RGB565) */
    uint16_t                   width;       /*!< Display width in pixels */
    uint16_t                   height;      /*!< Display height in pixels */
} esp_video_render_lvgl_cfg_t;

/**
 * @brief  Get LVGL backend operations
 *
 * @return
 *       - Backend  operations pointer on success
 *       - NULL     on failure
 */
const esp_video_render_backend_ops_t *esp_video_render_get_lvgl_backend(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
