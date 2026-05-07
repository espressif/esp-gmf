/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "esp_gmf_video_dec.h"
#include "esp_gmf_video_enc.h"
#include "esp_gmf_video_overlay.h"
#include "esp_gmf_video_ppa.h"
#include "esp_gmf_video_scale.h"
#include "esp_gmf_video_crop.h"
#include "esp_gmf_pool.h"
#include "esp_video_dec_default.h"
#include "esp_gmf_video_color_convert.h"
#include "esp_video_render.h"
#include "esp_video_render_backend.h"
#include "settings.h"
#include "esp_board_manager_includes.h"
#include "esp_lcd_panel_ops.h"
#include "video_render.h"
#ifdef VIDEO_RENDER_LVGL_SUPPORT
#include "lvgl.h"
#include "esp_lvgl_port.h"
#endif  /* VIDEO_RENDER_LVGL_SUPPORT */

#define TAG  "RENDER_SYS"

static esp_video_render_handle_t video_render;
static esp_gmf_pool_handle_t render_pool;
static bool use_lvgl_backend = false;
#ifdef VIDEO_RENDER_LVGL_SUPPORT
static lv_disp_t *disp_handle = NULL;
#endif  /* VIDEO_RENDER_LVGL_SUPPORT */

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

void video_render_use_lvgl(bool use)
{
    use_lvgl_backend = use;
}

static int get_display_cfg(esp_video_render_lcd_cfg_t *cfg, const dev_display_lcd_config_t **dev_cfg)
{
    const dev_display_lcd_config_t *lcd_cfg = NULL;
    dev_display_lcd_handles_t *lcd_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_handle);
    if (lcd_handle == NULL) {
        ESP_LOGE(TAG, "No display found");
        return -1;
    }
    esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, (void **)&lcd_cfg);
    if (lcd_cfg == NULL) {
        ESP_LOGE(TAG, "Failed to get display config");
        return -1;
    }
    if (dev_cfg) {
        *dev_cfg = lcd_cfg;
    }
    cfg->width = lcd_cfg->lcd_width;
    cfg->height = lcd_cfg->lcd_height;
    cfg->fb_num = 1;
    cfg->lcd_handle = lcd_handle->panel_handle;
    cfg->io_handle = lcd_handle->io_handle;
    if (strcmp(lcd_cfg->sub_type, "spi") == 0) {
        cfg->lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_DVP;
        cfg->out_format = ESP_VIDEO_RENDER_FORMAT_RGB565_BE;
    } else if (strcmp(lcd_cfg->sub_type, "rgb") == 0) {
        cfg->lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_RGB;
        cfg->out_format = ESP_VIDEO_RENDER_FORMAT_RGB565_BE;
    } else if (strcmp(lcd_cfg->sub_type, "i80") == 0) {
        cfg->lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_I80;
        cfg->out_format = ESP_VIDEO_RENDER_FORMAT_RGB565;
    } else if (strcmp(lcd_cfg->sub_type, "dsi") == 0) {
        cfg->lcd_type = ESP_VIDEO_RENDER_LCD_TYPE_DPI;
        cfg->out_format = ESP_VIDEO_RENDER_FORMAT_RGB565;
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT
        cfg->fb_num = lcd_cfg->sub_cfg.dsi.dpi_config.num_fbs;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT */
    } else {
        ESP_LOGE(TAG, "Unsupported LCD bus type: %s", lcd_cfg->sub_type);
        return -1;
    }
    return 0;
}

#ifdef VIDEO_RENDER_LVGL_SUPPORT

// Weak declaration to fetch IO handle from board layer if available
static int lvgl_init_ref = 0;
lv_disp_t *init_lvgl_display_from_board(esp_video_render_lcd_cfg_t *cfg, const dev_display_lcd_config_t *dev_cfg)
{
    lvgl_port_cfg_t vgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lv_disp_t *disp = NULL;
    if (lvgl_init_ref == 0) {
        if (lvgl_port_init(&vgl_port_cfg) != 0) {
            ESP_LOGE(TAG, "LVGL port init failed");
            return NULL;
        }
        lvgl_init_ref++;
        ESP_LOGI(TAG, "LVGL port initialized");
    }
    lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = cfg->io_handle,
        .panel_handle = cfg->lcd_handle,
        .double_buffer = 0,
        .hres = cfg->width,
        .vres = cfg->height,
        .monochrome = false,
        .rotation = {
            .swap_xy = dev_cfg->swap_xy,
            .mirror_x = dev_cfg->mirror_x,
            .mirror_y = dev_cfg->mirror_y,
        },
#if LVGL_VERSION_MAJOR >= 9
        .color_format = LV_COLOR_FORMAT_RGB565,
#endif  /* LVGL_VERSION_MAJOR >= 9 */
        .flags = {
            .swap_bytes = (cfg->out_format == ESP_VIDEO_RENDER_FORMAT_RGB565_BE),
            .buff_dma = true,
            .buff_spiram = false,
        }};
    // Set buffer size based on bus type
    switch (cfg->lcd_type) {
        case ESP_VIDEO_RENDER_LCD_TYPE_DVP: {
            // SPI typically needs smaller buffer
            disp_cfg.buffer_size = cfg->width * 50;
            ESP_LOGI(TAG, "Adding SPI display: %dx%d, buffer=%d", cfg->width, cfg->height, (int)disp_cfg.buffer_size);
            disp = lvgl_port_add_disp(&disp_cfg);
            break;
        }
        case ESP_VIDEO_RENDER_LCD_TYPE_DPI: {
            disp_cfg.buffer_size = cfg->width * 50;  // Porting from p4 function ev board
            const lvgl_port_display_dsi_cfg_t dpi_cfg = {
                .flags = {
                    .avoid_tearing = false,
                }};
            disp = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);
            break;
        }
        default:
            ESP_LOGE(TAG, "Unsupported LCD bus type: %d", cfg->lcd_type);
            disp = NULL;
            break;
    }
    if (disp == NULL) {
        lvgl_init_ref--;
        if (lvgl_init_ref == 0) {
            lvgl_port_deinit();
        }
    }
    return disp;
}
#endif  /* VIDEO_RENDER_LVGL_SUPPORT */

static int create_default_pool(esp_gmf_pool_handle_t *pool)
{
    *pool = NULL;
    esp_gmf_element_handle_t el = NULL;
    do {
        BREAK_ON_FAIL(esp_gmf_pool_init(pool));

        // Register decode to decode image, if render raw frame no need to call it
        esp_gmf_video_dec_init(NULL, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));

        el = NULL;
        esp_gmf_video_overlay_init(NULL, &el);
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

        el = NULL;
        esp_imgfx_color_convert_cfg_t color_convert_cfg = {
            .color_space_std = ESP_IMGFX_COLOR_SPACE_STD_BT601};
        esp_gmf_video_color_convert_init(&color_convert_cfg, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
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

esp_video_render_handle_t create_lvgl_render(esp_video_render_lcd_cfg_t *lcd_cfg, uint8_t fps)
{
#ifdef VIDEO_RENDER_LVGL_SUPPORT
    if (disp_handle == NULL) {
        ESP_LOGE(TAG, "LVGL display not initialized");
        return NULL;
    }
    esp_video_render_cfg_t render_cfg = {
        .pool = render_pool,
        .fps = fps,
    };
    esp_video_render_handle_t render = NULL;
    int ret = esp_video_render_create(&render_cfg, &render);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        return NULL;
    }
    esp_video_render_backend_cfg_t backend_cfg = {};
    esp_video_render_lvgl_cfg_t lv_cfg = {
        .out_format = ESP_VIDEO_RENDER_FORMAT_RGB565,
        .width = lcd_cfg->width,
        .height = lcd_cfg->height,
    };
    lv_cfg.lv_disp = disp_handle;
    backend_cfg.ops = esp_video_render_get_lvgl_backend();
    backend_cfg.cfg = &lv_cfg;
    backend_cfg.cfg_size = sizeof(lv_cfg);
    ret = esp_video_render_set_display(render, &backend_cfg);
    if (ret != ESP_VIDEO_RENDER_ERR_OK) {
        esp_video_render_destroy(render);
        return NULL;
    }
    return render;
#endif  /* VIDEO_RENDER_LVGL_SUPPORT */
    return NULL;
}

int create_video_render(uint8_t fps)
{
    esp_video_render_lcd_cfg_t lcd_cfg = {};
    const dev_display_lcd_config_t *dev_cfg = NULL;
    if (get_display_cfg(&lcd_cfg, &dev_cfg) != 0) {
        ESP_LOGE(TAG, "Failed to get display config");
        return -1;
    }
    int ret = 0;
    fps = fps > 0 ? fps : 10;
    do {
        // Register decode to decode image, if render raw frame no need to call it
        esp_video_dec_register_default();
        ret = create_default_pool(&render_pool);
        BREAK_ON_FAIL(ret);
        // Now try to add display, choose backend per flag
        esp_video_render_backend_cfg_t backend_cfg = {};
#ifdef VIDEO_RENDER_LVGL_SUPPORT
        if (use_lvgl_backend) {
            if (disp_handle == NULL) {
                disp_handle = init_lvgl_display_from_board(&lcd_cfg, dev_cfg);
            }
            if (disp_handle == NULL) {
                ESP_LOGW(TAG, "LVGL display init failed, fallback to LCD backend");

            } else {
                video_render = create_lvgl_render(&lcd_cfg, fps);
                if (video_render == NULL) {
                    ESP_LOGE(TAG, "Failed to create LVGL render");
                    ret = -1;
                    break;
                }
                return 0;
            }
        }
#endif  /* VIDEO_RENDER_LVGL_SUPPORT */
        // Create video render
        esp_video_render_cfg_t render_cfg = {
            .pool = render_pool,
            .fps = fps,
        };
        ret = esp_video_render_create(&render_cfg, &video_render);
        BREAK_ON_FAIL(ret);

        backend_cfg.ops = esp_video_render_get_lcd_backend();
        backend_cfg.cfg = &lcd_cfg;
        backend_cfg.cfg_size = sizeof(lcd_cfg);
        ret = esp_video_render_set_display(video_render, &backend_cfg);
        BREAK_ON_FAIL(ret);
        return ret;
    } while (0);
    destroy_video_render();
    return ret;
}

esp_video_render_handle_t get_video_render(void)
{
    return video_render;
}

void destroy_video_render(void)
{
    if (video_render) {
        esp_video_render_destroy(video_render);
        video_render = NULL;
    }
    if (render_pool) {
        esp_gmf_pool_deinit(render_pool);
        render_pool = NULL;
    }
#ifdef VIDEO_RENDER_LVGL_SUPPORT
    if (use_lvgl_backend && lvgl_init_ref > 0) {
        lvgl_init_ref--;
        if (lvgl_init_ref == 0) {
            if (disp_handle) {
                lvgl_port_remove_disp(disp_handle);
                disp_handle = NULL;
            }
            ESP_LOGI(TAG, "LVGL port deinit");
            lvgl_port_deinit();
            // Delay here to wait for thread is quit complete
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
    }
#endif  /* VIDEO_RENDER_LVGL_SUPPORT */
    esp_video_dec_unregister_default();
}
