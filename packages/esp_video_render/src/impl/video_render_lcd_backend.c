
/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_video_render_backend.h"
#include "video_render_sys.h"
#include "esp_lcd_panel_io.h"
#if SOC_LCD_RGB_SUPPORTED
#include "esp_lcd_panel_rgb.h"
#endif  /* SOC_LCD_RGB_SUPPORTED */
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_lcd_mipi_dsi.h"
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
#include "esp_lcd_panel_ops.h"
#include "video_render_utils.h"
#include "esp_log.h"
#include "esp_video_render_log.h"
#include "esp_timer.h"
#include "freertos/semphr.h"

#define TAG                 "LCD_BACKEND"
#define MAX_FB_NUM          (2)
#define DVP_MAX_WRITE_LINE  (40)

#define GET_FRAME_BUFFER(frame_buf_cb, lcd_handle, fb_num, lcd)  if (fb_num == 1) {  \
    frame_buf_cb(lcd_handle, fb_num, (void **)&lcd->fb[0]);                          \
    } else if (fb_num == 2) {                                                        \
    frame_buf_cb(lcd_handle, fb_num, (void **)&lcd->fb[0], (void **)&lcd->fb[1]);    \
}

typedef struct {
    esp_video_render_lcd_cfg_t   cfg;
    bool                         with_gram;
    uint8_t                     *fb[MAX_FB_NUM];
    video_render_mutex_handle_t  fb_lock[MAX_FB_NUM];
    uint32_t                     fb_size;
    bool                         manual_fb;
    uint8_t                      fb_num;
    uint8_t                      disp_fb;
    video_render_mutex_handle_t  mutex;
    uint16_t                     pending_draws;
    bool                         wait_for_draw;
    SemaphoreHandle_t            draw_sem;
} lcd_backend_t;

static esp_video_render_err_t lcd_backend_deinit(esp_video_render_backend_handle_t h);
static video_render_mutex_handle_t lcd_bus_lock;
static uint8_t lcd_count = 0;

static uint16_t get_max_pending_draws(const esp_video_render_lcd_cfg_t *cfg)
{
    if (cfg->lcd_type == ESP_VIDEO_RENDER_LCD_TYPE_DVP) {
        uint16_t height = cfg->height ? cfg->height : DVP_MAX_WRITE_LINE;
        return (height + DVP_MAX_WRITE_LINE - 1) / DVP_MAX_WRITE_LINE;
    }
    return 1;
}

static void drain_draw_sem(lcd_backend_t *lcd)
{
    if (lcd->draw_sem) {
        while (xSemaphoreTake(lcd->draw_sem, 0) == pdTRUE) {
        }
    }
}

static void draw_finished(lcd_backend_t *lcd)
{
    if (lcd == NULL || lcd->draw_sem == NULL) {
        return;
    }
    if (xPortInIsrContext()) {
        BaseType_t higher_priority_task_woken = pdFALSE;
        xSemaphoreGiveFromISR(lcd->draw_sem, &higher_priority_task_woken);
        portYIELD_FROM_ISR(higher_priority_task_woken);
    } else {
        xSemaphoreGive(lcd->draw_sem);
    }
}

#if CONFIG_IDF_TARGET_ESP32P4
static bool dpi_draw_finished(esp_lcd_panel_handle_t panel, esp_lcd_dpi_panel_event_data_t *data, void *ctx)
{
    draw_finished((lcd_backend_t *)ctx);
    return true;
}
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

static bool on_color_trans_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *data, void *ctx)
{
    draw_finished((lcd_backend_t *)ctx);
    return false;
}

static bool is_display_fb(lcd_backend_t *lcd, esp_video_render_fb_t *fb)
{
    for (int i = 0; i < lcd->fb_num; i++) {
        if (lcd->fb[i] == fb->data) {
            return true;
        }
    }
    return false;
}

static inline esp_video_render_err_t allocate_fb_lock(lcd_backend_t *lcd)
{
    for (int i = 0; i < lcd->fb_num; i++) {
        if (lcd->fb_lock[i] == NULL) {
            lcd->fb_lock[i] = video_render_mutex_create();
            if (lcd->fb_lock[i] == NULL) {
                return ESP_VIDEO_RENDER_ERR_NO_MEM;
            }
        }
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t lcd_backend_manual_fb(lcd_backend_t *lcd)
{
    uint8_t fb_num = lcd->cfg.fb_num ? lcd->cfg.fb_num : 1;
    lcd->fb_num = 0;
    for (int i = 0; i < fb_num; i++) {
        lcd->fb[i] = video_render_malloc_align(lcd->fb_size, 64);
        if (lcd->fb[i] == NULL) {
            ESP_LOGE(TAG, "Fail to allocate manual fb");
            break;
        }
        lcd->fb_num++;
    }
    if (lcd->fb_num == 0) {
        return ESP_VIDEO_RENDER_ERR_NO_MEM;
    }
    lcd->manual_fb = true;
    return allocate_fb_lock(lcd);
}

static esp_video_render_err_t prepare_lcd_panel(lcd_backend_t *lcd, esp_video_render_lcd_cfg_t *cfg)
{
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_OK;
    uint8_t fb_num = cfg->fb_num ? cfg->fb_num : MAX_FB_NUM;
    lcd->wait_for_draw = false;
    switch (cfg->lcd_type) {
        default:
            return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        case ESP_VIDEO_RENDER_LCD_TYPE_DVP:
            // Currently only DVP display with gram ?
            lcd->with_gram = true;
            if (cfg->io_handle == NULL) {
                ESP_LOGW(TAG, "DVP IO handle not set, display may teared");
                break;
            }
            const esp_lcd_panel_io_callbacks_t cbs = {
                .on_color_trans_done = on_color_trans_done,
            };
            if (esp_lcd_panel_io_register_event_callbacks(cfg->io_handle, &cbs, lcd) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to register event callbacks");
                return ESP_VIDEO_RENDER_ERR_FAIL;
            }
            lcd->wait_for_draw = true;
            break;
        case ESP_VIDEO_RENDER_LCD_TYPE_RGB:
#if SOC_LCD_RGB_SUPPORTED
            if (fb_num > 1) {
                GET_FRAME_BUFFER(esp_lcd_rgb_panel_get_frame_buffer, cfg->lcd_handle, fb_num, lcd);
            }

#endif  /* SOC_LCD_RGB_SUPPORTED */
            break;
        case ESP_VIDEO_RENDER_LCD_TYPE_DPI:
#if CONFIG_IDF_TARGET_ESP32P4
            if (fb_num > 1) {
                GET_FRAME_BUFFER(esp_lcd_rgb_panel_get_frame_buffer, cfg->lcd_handle, fb_num, lcd);
            }
            esp_lcd_dpi_panel_event_callbacks_t dpi_cb = {
                .on_color_trans_done = dpi_draw_finished,
            };
            lcd->wait_for_draw = true;
            if (esp_lcd_dpi_panel_register_event_callbacks(cfg->lcd_handle, &dpi_cb, lcd) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to register event callbacks");
                return ESP_VIDEO_RENDER_ERR_FAIL;
            }
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
            break;
    }
    fb_num = 0;
    for (int i = 0; i < MAX_FB_NUM; i++) {
        if (lcd->fb[i]) {
            fb_num++;
        }
    }
    lcd->fb_num = fb_num;
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Get fb count %d\n", fb_num);
    ret = allocate_fb_lock(lcd);
    return ret;
}

static esp_video_render_err_t lcd_backend_init(void *cfg, int cfg_size, esp_video_render_backend_handle_t *render)
{
    if (cfg == NULL || cfg_size != sizeof(esp_video_render_lcd_cfg_t)) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    esp_video_render_lcd_cfg_t *lcd_cfg = (esp_video_render_lcd_cfg_t *)cfg;
    if (lcd_cfg->lcd_handle == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    lcd_backend_t *lcd = video_render_calloc(1, sizeof(lcd_backend_t));
    esp_video_render_err_t ret = ESP_VIDEO_RENDER_ERR_NO_MEM;
    do {
        if (lcd == NULL) {
            break;
        }
        if (lcd_bus_lock == NULL) {
            lcd_bus_lock = video_render_mutex_create();
            if (lcd_bus_lock == NULL) {
                break;
            }
        }
        lcd_count++;
        lcd->mutex = video_render_mutex_create();
        if (lcd->mutex == NULL) {
            break;
        }
        lcd->draw_sem = xSemaphoreCreateCounting(get_max_pending_draws(lcd_cfg), 0);
        if (lcd->draw_sem == NULL) {
            break;
        }
        lcd->cfg = *lcd_cfg;
        ret = prepare_lcd_panel(lcd, lcd_cfg);
        if (ret != ESP_VIDEO_RENDER_ERR_OK) {
            break;
        }
        video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_INFO, "lcd %dx%d format %x\n", lcd_cfg->width, lcd_cfg->height, lcd_cfg->out_format);
        lcd->fb_size = lcd->cfg.width * lcd->cfg.height * video_render_get_pixel_bits(lcd->cfg.out_format) >> 3;
        *render = (esp_video_render_backend_handle_t)lcd;
        return ESP_VIDEO_RENDER_ERR_OK;
    } while (0);
    // Cleanup on failure
    if (lcd) {
        lcd_backend_deinit(lcd);
    }
    return ret;
}

static bool lcd_backend_with_gram(esp_video_render_backend_handle_t h)
{
    if (h == NULL) {
        return false;
    }
    lcd_backend_t *lcd = (lcd_backend_t *)h;
    return lcd->with_gram;
}

static esp_video_render_err_t lcd_backend_get_display_info(esp_video_render_backend_handle_t h, esp_video_render_disp_info_t *info)
{
    if (h == NULL || info == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    lcd_backend_t *lcd = (lcd_backend_t *)h;
    info->width = lcd->cfg.width;
    info->height = lcd->cfg.height;
    info->format = lcd->cfg.out_format;
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t lcd_backend_get_fb(esp_video_render_backend_handle_t h, esp_video_render_fb_t *fb)
{
    if (h == NULL || fb == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    lcd_backend_t *lcd = (lcd_backend_t *)h;
    if (lcd->fb_num == 0) {
        lcd_backend_manual_fb(lcd);
        if (lcd->fb_num == 0) {
            fb->data = NULL;
            return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
        }
    }
    video_render_mutex_lock(lcd->mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    fb->info.format = lcd->cfg.out_format;
    fb->info.width = lcd->cfg.width;
    fb->info.height = lcd->cfg.height;
    fb->info.fps = 0;
    fb->data = lcd->fb[lcd->disp_fb];
    fb->size = lcd->fb_size;
    video_render_mutex_unlock(lcd->mutex);
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t lcd_backend_lock_fb(esp_video_render_backend_handle_t h, esp_video_render_fb_t *fb, bool lock)
{
    if (h == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    lcd_backend_t *lcd = (lcd_backend_t *)h;
    if (lcd->fb_num == 0) {
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    int fb_idx = -1;
    for (int i = 0; i < lcd->fb_num; i++) {
        if (lcd->fb[i] == fb->data) {
            fb_idx = i;
            break;
        }
    }
    if (fb_idx == -1) {
        return ESP_VIDEO_RENDER_ERR_NOT_FOUND;
    }
    if (lock) {
        video_render_mutex_lock(lcd->fb_lock[fb_idx], VIDEO_RENDER_MAX_LOCK_TIME);
    } else {
        video_render_mutex_unlock(lcd->fb_lock[fb_idx]);
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

static esp_video_render_err_t lcd_backend_wait_for_draw(lcd_backend_t *lcd)
{
    uint16_t pending = lcd->pending_draws;
    if (pending == 0) {
        return ESP_VIDEO_RENDER_ERR_OK;
    }
    if (lcd->wait_for_draw && lcd->draw_sem) {
        for (int i = 0; i < pending; i++) {
            if (xSemaphoreTake(lcd->draw_sem, VIDEO_RENDER_TIME_TO_TICKS(1000)) != pdTRUE) {
                lcd->pending_draws = pending - i;
                ESP_LOGE(TAG, "Wait draw timeout, pending %d", lcd->pending_draws);
                return ESP_VIDEO_RENDER_ERR_TIMEOUT;
            }
        }
    }
    lcd->pending_draws = 0;
    return ESP_VIDEO_RENDER_ERR_OK;
}

esp_video_render_err_t lcd_backend_write_fb(esp_video_render_backend_handle_t h,
                                            esp_video_render_fb_t *fb,
                                            const esp_video_render_rect_t *dirty_rect,
                                            const esp_video_render_pos_t *pos)
{
    if (h == NULL || fb == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    const esp_video_render_rect_t full = {
        .x = 0, .y = 0, .width = fb->info.width, .height = fb->info.height};
    const esp_video_render_rect_t *r = (dirty_rect && dirty_rect->width && dirty_rect->height) ? dirty_rect : &full;
    uint16_t width = r->width;
    uint16_t height = r->height;
    // TODO currently not support fb in block
    if ((width > fb->info.width) || (height > fb->info.height)) {
        ESP_LOGE(TAG, "Frame buffer %dx%d over actual size %dx%d", width, height, fb->info.width, fb->info.height);
        return ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
    }
    lcd_backend_t *lcd = (lcd_backend_t *)h;
    int ret = 0;
    // Wait for last draw finished
    esp_video_render_err_t wait_ret = lcd_backend_wait_for_draw(lcd);
    if (wait_ret != ESP_VIDEO_RENDER_ERR_OK) {
        return wait_ret;
    }

    video_render_mutex_lock(lcd->mutex, VIDEO_RENDER_MAX_LOCK_TIME);
    uint8_t display_fb = lcd->disp_fb;
    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Write on %d/%d\n", display_fb, lcd->fb_num);
    if (lcd->fb_num) {
        lcd->disp_fb = (lcd->disp_fb + 1) % lcd->fb_num;
    }
    video_render_mutex_unlock(lcd->mutex);

    drain_draw_sem(lcd);

    switch (lcd->cfg.lcd_type) {
        default:
            ret = ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
            break;
        case ESP_VIDEO_RENDER_LCD_TYPE_DPI:
        case ESP_VIDEO_RENDER_LCD_TYPE_RGB:
            if (is_display_fb(lcd, fb)) {
                // For direct panel frame buffers, update full frame to avoid tearing.
                video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Draw full %dx%d data %p\n", fb->info.width, fb->info.height, fb->data);
                ret = esp_lcd_panel_draw_bitmap(lcd->cfg.lcd_handle, 0, 0, fb->info.width, fb->info.height, fb->data);
                video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "draw end\n");
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to draw bitmap ret %d", ret);
                    break;
                }
            } else {
                // Support partial updates by dirty_rect.
                int bpp = video_render_get_pixel_bits(fb->info.format) >> 3;
                if (bpp <= 0) {
                    ret = ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
                    break;
                }
                uint8_t *rgb_data = fb->data + (r->y * fb->info.width + r->x) * bpp;
                if (pos) {
                    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Draw %d-%d %dx%d data %p\n", pos->x, pos->y, width, height, rgb_data);
                    ret = esp_lcd_panel_draw_bitmap(lcd->cfg.lcd_handle, pos->x, pos->y, pos->x + width, pos->y + height, rgb_data);
                } else {
                    video_render_printf(ESP_VIDEO_RENDER_LOG_LEVEL_DEBUG, "Draw %d-%d %dx%d data %p\n", r->x, r->y, width, height, rgb_data);
                    ret = esp_lcd_panel_draw_bitmap(lcd->cfg.lcd_handle, r->x, r->y, r->x + width, r->y + height, rgb_data);
                }
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to draw bitmap ret %d", ret);
                    ret = ESP_VIDEO_RENDER_ERR_FAIL;
                    break;
                }
            }
            if (ret == ESP_OK && lcd->wait_for_draw) {
                lcd->pending_draws = 1;
            }
            break;
        case ESP_VIDEO_RENDER_LCD_TYPE_DVP: {
            if (fb->info.format != ESP_VIDEO_RENDER_FORMAT_RGB565 && fb->info.format != ESP_VIDEO_RENDER_FORMAT_RGB565_BE) {
                ret = ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
                break;
            }
            if (r->x != 0 || r->width != fb->info.width) {
                ESP_LOGE(TAG, "Only support x=0 and whole width");
                ret = ESP_VIDEO_RENDER_ERR_NOT_SUPPORTED;
                break;
            }
            uint8_t *rgb_data = fb->data + r->y * fb->info.width * 2;
            video_render_mutex_lock(lcd_bus_lock, VIDEO_RENDER_MAX_LOCK_TIME);

            uint16_t submitted = 0;
            if (r->y == 0) {
                ret = esp_lcd_panel_draw_bitmap(lcd->cfg.lcd_handle, 0, 0, width, height, rgb_data);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to draw bitmap ret %d", ret);
                    goto dvp_wait_done;
                }
                submitted++;
            } else {
                int i = 0;
                int n = DVP_MAX_WRITE_LINE;
                while (i < height) {
                    if (i + n > height) {
                        n = height - i;
                    }
                    ret = esp_lcd_panel_draw_bitmap(lcd->cfg.lcd_handle, 0, r->y + i,
                                                    width, r->y + i + n, rgb_data);
                    if (ret != ESP_OK) {
                        ESP_LOGE(TAG, "Failed to draw bitmap ret %d", ret);
                        goto dvp_wait_done;
                    }
                    submitted++;
                    rgb_data += n * fb->info.width * 2;
                    i += n;
                }
            }
        dvp_wait_done:
            lcd->pending_draws = submitted;
            wait_ret = lcd_backend_wait_for_draw(lcd);
            if (ret != ESP_OK) {
                ret = ESP_VIDEO_RENDER_ERR_FAIL;
            } else if (wait_ret != ESP_VIDEO_RENDER_ERR_OK) {
                ret = wait_ret;
            }
            video_render_mutex_unlock(lcd_bus_lock);
            break;
        }
    }
    return ret;
}

esp_video_render_err_t lcd_backend_deinit(esp_video_render_backend_handle_t h)
{
    if (h == NULL) {
        return ESP_VIDEO_RENDER_ERR_INVALID_ARG;
    }
    lcd_backend_t *lcd = (lcd_backend_t *)h;
    lcd_backend_wait_for_draw(lcd);
    if (lcd->mutex) {
        video_render_mutex_destroy(lcd->mutex);
    }
    if (lcd->draw_sem) {
        vSemaphoreDelete(lcd->draw_sem);
    }
    for (int i = 0; i < lcd->fb_num; i++) {
        if (lcd->fb_lock[i]) {
            video_render_mutex_destroy(lcd->fb_lock[i]);
            lcd->fb_lock[i] = NULL;
        }
    }
    if (lcd->manual_fb) {
        for (int i = 0; i < lcd->fb_num; i++) {
            if (lcd->fb[i]) {
                video_render_free(lcd->fb[i]);
                lcd->fb[i] = NULL;
            }
        }
    }
    video_render_free(lcd);
    lcd_count--;
    if (lcd_count == 0 && lcd_bus_lock) {
        video_render_mutex_destroy(lcd_bus_lock);
        lcd_bus_lock = NULL;
    }
    return ESP_VIDEO_RENDER_ERR_OK;
}

const esp_video_render_backend_ops_t *esp_video_render_get_lcd_backend(void)
{
    static const esp_video_render_backend_ops_t lcd_ops = {
        .init = lcd_backend_init,
        .with_gram = lcd_backend_with_gram,
        .get_display_info = lcd_backend_get_display_info,
        .get_fb = lcd_backend_get_fb,
        .lock_fb = lcd_backend_lock_fb,
        .write_fb = lcd_backend_write_fb,
        .deinit = lcd_backend_deinit,
    };
    return &lcd_ops;
}
