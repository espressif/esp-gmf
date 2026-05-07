/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "video_render_sys.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_video_color_convert.h"
#include "esp_gmf_video_crop.h"
#include "esp_gmf_video_dec.h"
#include "esp_gmf_video_ppa.h"
#include "esp_gmf_video_overlay.h"
#include "esp_gmf_video_scale.h"
#include "esp_video_dec_default.h"
#include "esp_video_render_backend.h"
#include "esp_board_manager_includes.h"
#include "esp_lcd_panel_io.h"
#include "esp_log.h"

#define TAG  "VIDEO_PLAYER_SYS"

#define BREAK_ON_FAIL(sta)  {                     \
    int _ret = (sta);                             \
    if (_ret != 0) {                              \
        ESP_LOGE(TAG, "Fail at %s:%d ret %d",     \
                 __func__, __LINE__, (int)_ret);  \
        break;                                    \
    }                                             \
}

static esp_video_render_handle_t s_render;
static esp_gmf_pool_handle_t s_pool;

#ifndef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
typedef struct {
    esp_lcd_panel_io_handle_t  io_handle;     /*!< LCD panel IO handle */
    esp_lcd_panel_handle_t     panel_handle;  /*!< LCD panel device handle */
} dev_display_lcd_handles_t;

typedef struct {
    const char *name;              /*!< Device name */
    const char *chip;              /*!< LCD chip type */
    const char *sub_type;          /*!< Sub type: dsi, spi, parlio */
    uint16_t    lcd_width;         /*!< LCD width */
    uint16_t    lcd_height;        /*!< LCD height */
    uint8_t     swap_xy      : 1;  /*!< Swap X and Y coordinates */
    uint8_t     mirror_x     : 1;  /*!< Mirror X coordinates */
    uint8_t     mirror_y     : 1;  /*!< Mirror Y coordinates */
    uint8_t     need_reset   : 1;  /*!< Whether the panel needs reset during initialization */
    uint8_t     invert_color : 1;  /*!< Invert color flag */
    union {
    } sub_cfg;
} dev_display_lcd_config_t;

#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */

static int create_default_pool(esp_gmf_pool_handle_t *pool)
{
    *pool = NULL;
    esp_gmf_element_handle_t el = NULL;
    do {
        BREAK_ON_FAIL(esp_gmf_pool_init(pool));

        el = NULL;
        esp_gmf_video_dec_init(NULL, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));

        // Only add ppa for P4
#if CONFIG_IDF_TARGET_ESP32P4
        el = NULL;
        esp_gmf_video_ppa_init(NULL, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));
#else
        el = NULL;
        esp_imgfx_scale_cfg_t scale_cfg = {
            .filter_type = ESP_IMGFX_SCALE_FILTER_TYPE_BILINEAR};
        esp_gmf_video_scale_init(&scale_cfg, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));

        el = NULL;
        esp_imgfx_crop_cfg_t crop_cfg = {};
        esp_gmf_video_crop_init(&crop_cfg, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
        // We always add color convert for some format PPA not supported
        el = NULL;
        esp_imgfx_color_convert_cfg_t color_convert_cfg = {
            .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601};
        esp_gmf_video_color_convert_init(&color_convert_cfg, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));
        return 0;
    } while (0);
    if (el) {
        esp_gmf_element_deinit(el);
    }
    if (*pool) {
        esp_gmf_pool_deinit(*pool);
        *pool = NULL;
    }
    return -1;
}

int video_render_sys_create(uint8_t fps)
{
    int ret = 0;
    fps = fps > 0 ? fps : 10;
    dev_display_lcd_config_t *lcd_cfg = NULL;
    dev_display_lcd_handles_t *lcd_handle = NULL;
    do {
        esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_handle);
        if (lcd_handle == NULL) {
            ESP_LOGE(TAG, "No display found");
            break;
        }
        // Register decode to decode image, if render raw frame no need to call it
        esp_video_dec_register_default();
        ret = esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_cfg);
        BREAK_ON_FAIL(ret);

        ret = create_default_pool(&s_pool);
        BREAK_ON_FAIL(ret);
        esp_video_render_lcd_cfg_t backend_cfg = {};
        backend_cfg.width = lcd_cfg->lcd_width;
        backend_cfg.height = lcd_cfg->lcd_height;
        backend_cfg.fb_num = 1;
        // Now try to add display, choose backend per flag
        esp_video_render_backend_cfg_t backend_cfg_wrapper = {};
        // Create video render
        esp_video_render_cfg_t render_cfg = {
            .pool = s_pool,
            .fps = fps,
        };
        ret = esp_video_render_create(&render_cfg, &s_render);
        BREAK_ON_FAIL(ret);
        if (backend_cfg_wrapper.ops == NULL) {
            backend_cfg.lcd_handle = lcd_handle->panel_handle;
            if (strcmp(lcd_cfg->sub_type, "spi") == 0) {
                backend_cfg.lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_DVP;
                backend_cfg.out_format = ESP_VIDEO_RENDER_FORMAT_RGB565_BE;
            } else if (strcmp(lcd_cfg->sub_type, "rgb") == 0) {
                backend_cfg.lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_RGB;
                backend_cfg.out_format = ESP_VIDEO_RENDER_FORMAT_RGB565_BE;
            } else if (strcmp(lcd_cfg->sub_type, "i80") == 0) {
                backend_cfg.lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_I80;
                backend_cfg.out_format = ESP_VIDEO_RENDER_FORMAT_RGB565;
            } else if (strcmp(lcd_cfg->sub_type, "dsi") == 0) {
                backend_cfg.lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_DPI;
                backend_cfg.out_format = ESP_VIDEO_RENDER_FORMAT_RGB565;
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT
                backend_cfg.fb_num = lcd_cfg->sub_cfg.dsi.dpi_config.num_fbs;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT */
            } else {
                BREAK_ON_FAIL(-1);
            }
            backend_cfg_wrapper.ops = esp_video_render_get_lcd_backend();
            backend_cfg_wrapper.cfg = &backend_cfg;
            backend_cfg_wrapper.cfg_size = sizeof(backend_cfg);
        }
        ret = esp_video_render_set_display(s_render, &backend_cfg_wrapper);
        BREAK_ON_FAIL(ret);
        return ret;
    } while (0);
    video_render_sys_destroy();
    return ret;
}

esp_video_render_handle_t video_render_sys_get(void)
{
    return s_render;
}

void video_render_sys_destroy(void)
{
    if (s_render) {
        esp_video_render_destroy(s_render);
        s_render = NULL;
    }
    if (s_pool) {
        esp_gmf_pool_deinit(s_pool);
        s_pool = NULL;
    }
    esp_video_dec_unregister_default();
}
