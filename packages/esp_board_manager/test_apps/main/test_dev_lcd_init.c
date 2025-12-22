/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "esp_err.h"
#include "esp_board_manager.h"
#include "esp_board_manager_err.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_lcd_types.h"
#include "esp_board_manager_includes.h"

#define TAG "LCD_INIT"

// LVGL configuration
#define LVGL_TICK_PERIOD_MS    5
#define LVGL_TASK_MAX_SLEEP_MS 500
#define LVGL_TASK_MIN_SLEEP_MS 5
#define LVGL_TASK_STACK_SIZE   (10 * 1024)
#define LVGL_TASK_PRIORITY     5

// Global handles
static void                     *lcd_handle   = NULL;
static void                     *touch_handle = NULL;
static esp_lcd_panel_handle_t    panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle    = NULL;
#ifdef CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT
static esp_lcd_touch_handle_t    tp           = NULL;
#endif  /* CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT */
static lv_display_t             *disp         = NULL;
static lv_indev_t               *touch_indev  = NULL;
#ifdef CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT
static periph_ledc_handle_t *ledc_handle = NULL;
#endif  /* CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT */

static esp_err_t lcd_lvgl_port_init(void)
{
    ESP_LOGI(TAG, "Initializing LVGL port...");
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = LVGL_TASK_PRIORITY,
        .task_stack = LVGL_TASK_STACK_SIZE,
        .task_affinity = 1,
        .task_max_sleep_ms = LVGL_TASK_MAX_SLEEP_MS,
        .timer_period_ms = LVGL_TICK_PERIOD_MS,
    };

    esp_err_t ret = lvgl_port_init(&lvgl_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL port: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    return ESP_OK;
}
#ifdef CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT
static esp_err_t lcd_backlight_set(int brightness_percent)
{
    if (brightness_percent > 100) {
        brightness_percent = 100;
    }
    if (brightness_percent < 0) {
        brightness_percent = 0;
    }

    ESP_LOGI(TAG, "Setting LCD backlight: %d%%,", brightness_percent);
    if (ledc_handle == NULL) {
        ESP_BOARD_RETURN_ON_ERROR(esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS, (void **)&ledc_handle), TAG, "Get LEDC control device handle failed");
    }
    dev_ledc_ctrl_config_t *dev_ledc_cfg = NULL;
    esp_err_t config_ret = esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS, (void *)&dev_ledc_cfg);
    if (config_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LEDC peripheral config '%s': %s", "lcd_brightness", esp_err_to_name(config_ret));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "dev_ledc_cfg.ledc_name: %s, name: %s, type: %s", dev_ledc_cfg->ledc_name, dev_ledc_cfg->name, dev_ledc_cfg->type);
    periph_ledc_config_t *ledc_config = NULL;
    esp_board_manager_get_periph_config(dev_ledc_cfg->ledc_name, (void **)&ledc_config);
    uint32_t duty = (brightness_percent * ((1 << (uint32_t)ledc_config->duty_resolution) - 1)) / 100;
    ESP_LOGI(TAG, "duty_cycle: %" PRIu32 ", speed_mode: %d, channel: %d, duty_resolution: %d", duty, ledc_handle->speed_mode, ledc_handle->channel, ledc_config->duty_resolution);
    ESP_BOARD_RETURN_ON_ERROR(ledc_set_duty(ledc_handle->speed_mode, ledc_handle->channel, duty), TAG, "LEDC set duty failed");
    ESP_BOARD_RETURN_ON_ERROR(ledc_update_duty(ledc_handle->speed_mode, ledc_handle->channel), TAG, "LEDC update duty failed");

    return ESP_OK;
}
#endif  /* CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT */

esp_err_t test_dev_lcd_lvgl_init(void)
{
    // Initialize LVGL port
    esp_err_t err = lcd_lvgl_port_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LVGL port initialization failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Initializing LCD display using Board Manager...");
#ifdef CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT
    lcd_backlight_set(100);
#endif  /* CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT */
    // Get LCD device handle from board manager
    esp_err_t ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD, &lcd_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LCD device handle: %s", esp_err_to_name(ret));
        return ESP_FAIL;
    }

    if (lcd_handle) {
        // Cast to the device structure based on configuration
#if CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
        dev_display_lcd_handles_t *lcd_handles = (dev_display_lcd_handles_t *)lcd_handle;
        panel_handle = lcd_handles->panel_handle;
        io_handle = lcd_handles->io_handle;

        // Get LCD configuration directly
        dev_display_lcd_config_t *lcd_cfg = NULL;
        esp_err_t ret = esp_board_manager_get_device_config("display_lcd", (void **)&lcd_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get LCD device config: %s", esp_err_to_name(ret));
            return ESP_FAIL;
        }

        lvgl_port_display_cfg_t disp_cfg = {
            .io_handle = io_handle,
            .panel_handle = panel_handle,
            .buffer_size = lcd_cfg->lcd_width * lcd_cfg->lcd_height,
            .double_buffer = true,
            .hres = lcd_cfg->lcd_width,
            .vres = lcd_cfg->lcd_height,
            .monochrome = false,
            .rotation = {
                .swap_xy = lcd_cfg->swap_xy,
                .mirror_x = lcd_cfg->mirror_x,
                .mirror_y = lcd_cfg->mirror_y,
            },
            .flags = {
                .buff_spiram = true,
#if LVGL_VERSION_MAJOR >= 9
                .swap_bytes = true,
#endif  /* LVGL_VERSION_MAJOR >= 9 */
            }};

        ESP_LOGI(TAG, "Unified LCD - Panel handle: %p, IO handle: %p, lcd_width: %d, lcd_height: %d, swap_xy: %d, mirror_x: %d, mirror_y: %d",
                 panel_handle, io_handle, lcd_cfg->lcd_width, lcd_cfg->lcd_height,
                 lcd_cfg->swap_xy, lcd_cfg->mirror_x, lcd_cfg->mirror_y);

        // Add LCD screen to LVGL based on sub_type
        if (strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_SPI) == 0) {
            disp = lvgl_port_add_disp(&disp_cfg);
            if (disp == NULL) {
                ESP_LOGE(TAG, "Failed to add unified SPI LCD display");
                return ESP_FAIL;
            }
        } else if (strcmp(lcd_cfg->sub_type, ESP_BOARD_DEVICE_LCD_SUB_TYPE_DSI) == 0) {
#if CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT
            lvgl_port_display_dsi_cfg_t dsi_cfg = {
                .flags = {
                    .avoid_tearing = false,
                },
            };
#if LVGL_VERSION_MAJOR >= 9
            disp_cfg.flags.swap_bytes = false;
#endif  /* LVGL_VERSION_MAJOR >= 9 */
            disp = lvgl_port_add_disp_dsi(&disp_cfg, &dsi_cfg);
            if (disp == NULL) {
                ESP_LOGE(TAG, "Failed to add unified DSI LCD display");
                return ESP_FAIL;
            }
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT */
        } else {
            ESP_LOGE(TAG, "Unknown LCD sub_type: %s", lcd_cfg->sub_type);
            return ESP_FAIL;
        }
#elif CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT
        dev_display_lcd_spi_handles_t *lcd_handles = (dev_display_lcd_spi_handles_t *)lcd_handle;
        dev_display_lcd_spi_config_t *lcd_cfg = NULL;

        panel_handle = lcd_handles->panel_handle;
        io_handle = lcd_handles->io_handle;
        esp_board_manager_get_device_config("display_lcd", (void **)&lcd_cfg);

        // Add LCD screen to LVGL
        ESP_LOGI(TAG, "Legacy SPI LCD - Panel handle: %p, IO handle: %p, x_max: %d, y_max: %d, swap_xy: %d, mirror_x: %d, mirror_y: %d",
                 panel_handle, io_handle, lcd_cfg->x_max, lcd_cfg->y_max, lcd_cfg->swap_xy, lcd_cfg->mirror_x, lcd_cfg->mirror_y);

        const lvgl_port_display_cfg_t disp_cfg = {
            .io_handle = io_handle,
            .panel_handle = panel_handle,
            .buffer_size = lcd_cfg->x_max * lcd_cfg->y_max,
            .double_buffer = true,
            .hres = lcd_cfg->x_max,
            .vres = lcd_cfg->y_max,
            .monochrome = false,
            .rotation = {
                .swap_xy = lcd_cfg->swap_xy,
                .mirror_x = lcd_cfg->mirror_x,
                .mirror_y = lcd_cfg->mirror_y,
            },
            .flags = {
                .buff_spiram = true,
#if LVGL_VERSION_MAJOR >= 9
                .swap_bytes = true,
#endif  /* LVGL_VERSION_MAJOR >= 9 */
            }};

        disp = lvgl_port_add_disp(&disp_cfg);
        if (disp == NULL) {
            ESP_LOGE(TAG, "Failed to add legacy SPI LCD display");
            return ESP_FAIL;
        }
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */
        ESP_LOGI(TAG, "LCD display initialized successfully");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "LCD device handle is NULL");
        return ESP_FAIL;
    }
}

esp_err_t test_dev_lcd_touch_init(void)
{
#ifdef CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT
    ESP_LOGI(TAG, "Initializing touch input using Board Manager...");

    // Get touch device handle from board manager
    esp_err_t ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LCD_TOUCH, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get touch device handle: %s (continuing without touch)", esp_err_to_name(ret));
        return ESP_FAIL;
    }
    if (touch_handle) {
        // Cast to the specific device structure
        dev_lcd_touch_i2c_handles_t *touch_handles = (dev_lcd_touch_i2c_handles_t *)touch_handle;
        tp = touch_handles->touch_handle;
        ESP_LOGI(TAG, "Touch device handle obtained: %p, tp: %p", touch_handle, tp);
        const lvgl_port_touch_cfg_t touch_cfg = {
            .disp = disp,  // Use the display we just created
            .handle = tp,
        };

        touch_indev = lvgl_port_add_touch(&touch_cfg);
        if (touch_indev == NULL) {
            ESP_LOGW(TAG, "Failed to add touch input (continuing without touch)");
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "Touch input initialized successfully");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Touch device handle is NULL (continuing without touch)");
    }
#endif  /* CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT */
    return ESP_FAIL;
}

esp_err_t test_dev_lcd_touch_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing touch input...");

    esp_err_t ret = ESP_OK;

    // Remove touch input device from LVGL
    if (touch_indev != NULL) {
        ret = lvgl_port_remove_touch(touch_indev);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove touch input device from LVGL: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Touch input device removed from LVGL successfully");
        }
        touch_indev = NULL;
    }
    // Clear touch handle
    touch_handle = NULL;

    ESP_LOGI(TAG, "Touch input deinitialization completed");
    return ESP_OK;
}

esp_err_t test_dev_lcd_lvgl_deinit(void)
{
    ESP_LOGI(TAG, "Deinitializing LCD display and LVGL...");

    esp_err_t ret = ESP_OK;
    // Remove display from LVGL
    if (disp != NULL) {
        ret = lvgl_port_remove_disp(disp);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to remove display from LVGL: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Display removed from LVGL successfully");
        }
        disp = NULL;
    }
    lcd_handle = NULL;

    // Deinitialize LVGL port
    ret = lvgl_port_deinit();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to deinitialize LVGL port: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LVGL port deinitialized successfully");
    }

    ESP_LOGI(TAG, "LCD display and LVGL deinitialization completed");
    return ESP_OK;
}
