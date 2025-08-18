/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "esp_board_manager.h"
#include "esp_board_device.h"
#include "esp_board_periph.h"
#include "test_dev_lcd_lvgl.h"

static const char *TAG = "TEST_DEV_LCD_LVGL";

// Button dimensions
#define TEST_BTN_WIDTH  100
#define TEST_BTN_HEIGHT 50

// Maximum result text length
#define MAX_RESULT_TEXT 512

typedef enum {
    LCD_LVGL_TEST_SUCCESS = 0,
    LCD_LVGL_TEST_FAIL    = -1,
    LCD_LVGL_TEST_ERROR   = -2,
} lcd_lvgl_test_result_t;

typedef enum {
    LCD_LVGL_COLOR_MAGENTA = 0,
    LCD_LVGL_COLOR_CYAN    = 1,
    LCD_LVGL_COLOR_BLUE    = 2,
    LCD_LVGL_COLOR_WHITE   = 3,
    LCD_LVGL_COLOR_COUNT   = 4
} lcd_lvgl_color_t;

typedef struct {
    lcd_lvgl_color_t  current_color;
    uint8_t           test_results[LCD_LVGL_COLOR_COUNT];
    lv_obj_t         *color_screen;
    lv_obj_t         *pass_btn;
    lv_obj_t         *fail_btn;
    lv_obj_t         *hint_label;
    bool              is_testing;
    bool              button_pressed;
} lcd_lvgl_test_ctx_t;

static const lv_color_t test_colors[] = {
    LV_COLOR_MAKE(255, 0, 255),
    LV_COLOR_MAKE(0, 255, 255),
    LV_COLOR_MAKE(0, 0, 255),
    LV_COLOR_MAKE(255, 255, 255),
};

static const char *color_names[] = {
    "Magenta",
    "Cyan",
    "Blue",
    "White",
};

static lcd_lvgl_test_ctx_t test_ctx;

static void ui_acquire(void);
static void ui_release(void);
static void get_screen_size(lv_coord_t *width, lv_coord_t *height);
static void cleanup_ui_elements(void);
static void pass_btn_event_cb(lv_event_t *e);
static void fail_btn_event_cb(lv_event_t *e);
static void show_test_results(bool result);

static void ui_acquire(void)
{
    lvgl_port_lock(100);
}

static void ui_release(void)
{
    lvgl_port_unlock();
}

static void get_screen_size(lv_coord_t *width, lv_coord_t *height)
{
    lv_disp_t *disp = lv_disp_get_default();
    if (disp) {
        *width = lv_disp_get_hor_res(disp);
        *height = lv_disp_get_ver_res(disp);
    } else {
        *width = 320;
        *height = 240;
    }
}

static void cleanup_ui_elements(void)
{
    ui_acquire();

    if (test_ctx.color_screen && lv_obj_is_valid(test_ctx.color_screen)) {
        lv_obj_del(test_ctx.color_screen);
        test_ctx.color_screen = NULL;
    }

    if (test_ctx.hint_label && lv_obj_is_valid(test_ctx.hint_label)) {
        lv_obj_del(test_ctx.hint_label);
        test_ctx.hint_label = NULL;
    }

    if (test_ctx.pass_btn && lv_obj_is_valid(test_ctx.pass_btn)) {
        lv_obj_del(test_ctx.pass_btn);
        test_ctx.pass_btn = NULL;
    }

    if (test_ctx.fail_btn && lv_obj_is_valid(test_ctx.fail_btn)) {
        lv_obj_del(test_ctx.fail_btn);
        test_ctx.fail_btn = NULL;
    }

    ui_release();
}

// Pass button event callback - record result and signal completion
static void pass_btn_event_cb(lv_event_t *e)
{
    if (!e) {
        ESP_LOGE(TAG, "Invalid event pointer");
        return;
    }

    ui_acquire();

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Check test status
        if (!test_ctx.is_testing) {
            ui_release();
            return;
        }

        if (test_ctx.current_color < LCD_LVGL_COLOR_COUNT) {
            test_ctx.test_results[test_ctx.current_color] = 1;  // Pass
            ESP_LOGI(TAG, "%s screen test PASSED", color_names[test_ctx.current_color]);
        } else {
            ESP_LOGE(TAG, "Invalid current color: %d", test_ctx.current_color);
        }

        // Signal that button was pressed - don't clean up UI here
        test_ctx.button_pressed = true;
        test_ctx.is_testing = false;
    }

    ui_release();
}

// Fail button event callback - record result and signal completion
static void fail_btn_event_cb(lv_event_t *e)
{
    if (!e) {
        ESP_LOGE(TAG, "Invalid event pointer");
        return;
    }

    ui_acquire();

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Check test status
        if (!test_ctx.is_testing) {
            ui_release();
            return;
        }

        if (test_ctx.current_color < LCD_LVGL_COLOR_COUNT) {
            test_ctx.test_results[test_ctx.current_color] = 0;  // Fail
            ESP_LOGI(TAG, "%s screen test FAILED", color_names[test_ctx.current_color]);
        } else {
            ESP_LOGE(TAG, "Invalid current color: %d", test_ctx.current_color);
        }

        // Signal that button was pressed - don't clean up UI here
        test_ctx.button_pressed = true;
        test_ctx.is_testing = false;
    }

    ui_release();
}

// Show test results - simplified like board_production_test
static void show_test_results(bool result)
{
    ui_acquire();

    // Clear screen first
    lv_obj_clean(lv_scr_act());

    lv_obj_t *result_label = lv_label_create(lv_scr_act());
    if (!result_label) {
        ESP_LOGE(TAG, "Failed to create result label");
        ui_release();
        return;
    }

    lv_obj_align(result_label, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_text_font(result_label, &lv_font_montserrat_14, 0);
    // Set contrasting text color for better visibility
    lv_obj_set_style_text_color(result_label, lv_color_black(), 0);

    char result_text[MAX_RESULT_TEXT] = {0};
    char temp[64];

    snprintf(result_text, sizeof(result_text), "LCD Test: %s\n\n", result ? "Pass" : "Fail");
    for (int i = 0; i < LCD_LVGL_COLOR_COUNT; i++) {
        snprintf(temp, sizeof(temp), "%s Screen: %s\n",
                 color_names[i],
                 test_ctx.test_results[i] ? "Pass" : "Fail");
        strcat(result_text, temp);
    }

    lv_label_set_text(result_label, result_text);

    ui_release();

    // delay 1500ms to show results
    vTaskDelay(pdMS_TO_TICKS(1000));

    ui_acquire();

    // delete result_label
    lv_obj_del(result_label);

    ui_release();
}

esp_err_t lvgl_gui_init(void)
{
    ESP_LOGI(TAG, "Initializing LCD LVGL test");

    // Check if LVGL screen is available
    if (!lv_scr_act()) {
        ESP_LOGE(TAG, "Screen object is NULL");
        return ESP_ERR_INVALID_STATE;
    }

    memset(&test_ctx, 0, sizeof(test_ctx));
    test_ctx.current_color = LCD_LVGL_COLOR_MAGENTA;
    test_ctx.is_testing = false;
    test_ctx.button_pressed = false;

    return ESP_OK;
}

lcd_lvgl_test_result_t lcd_lvgl_test_run_single(lcd_lvgl_color_t color)
{
    if (!lv_scr_act()) {
        ESP_LOGE(TAG, "Screen object is NULL");
        return LCD_LVGL_TEST_ERROR;
    }

    if (color >= LCD_LVGL_COLOR_COUNT) {
        ESP_LOGE(TAG, "Invalid test color: %d", color);
        return LCD_LVGL_TEST_ERROR;
    }

    // Clean up any existing UI elements
    cleanup_ui_elements();

    test_ctx.current_color = color;
    test_ctx.test_results[color] = 0;  // Initialize as fail
    test_ctx.is_testing = true;
    test_ctx.button_pressed = false;

    // Get screen dimensions
    lv_coord_t screen_width, screen_height;
    get_screen_size(&screen_width, &screen_height);

    ui_acquire();

    // Create color background screen
    test_ctx.color_screen = lv_obj_create(lv_scr_act());
    if (!test_ctx.color_screen) {
        ESP_LOGE(TAG, "Failed to create color screen");
        cleanup_ui_elements();
        return LCD_LVGL_TEST_ERROR;
    }

    lv_obj_set_size(test_ctx.color_screen, screen_width, screen_height);
    lv_obj_set_pos(test_ctx.color_screen, 0, 0);
    lv_obj_set_style_bg_color(test_ctx.color_screen, test_colors[color], 0);

    // Create hint label with contrasting text color
    test_ctx.hint_label = lv_label_create(test_ctx.color_screen);
    if (!test_ctx.hint_label) {
        ESP_LOGE(TAG, "Failed to create hint label");
        cleanup_ui_elements();
        return LCD_LVGL_TEST_ERROR;
    }

    lv_obj_align(test_ctx.hint_label, LV_ALIGN_TOP_MID, 0, 60);
    lv_obj_set_style_text_font(test_ctx.hint_label, &lv_font_montserrat_14, 0);

    // Set contrasting text color based on background color
    lv_color_t text_color;
    if (color == LCD_LVGL_COLOR_WHITE) {
        text_color = lv_color_black();  // Black text on white background
    } else if (color == LCD_LVGL_COLOR_BLUE) {
        text_color = lv_color_white();  // White text on blue background
    } else if (color == LCD_LVGL_COLOR_MAGENTA) {
        text_color = lv_color_white();  // White text on magenta background
    } else if (color == LCD_LVGL_COLOR_CYAN) {
        text_color = lv_color_black();  // Black text on cyan background (better contrast)
    } else {
        text_color = lv_color_white();  // Default to white text
    }
    lv_obj_set_style_text_color(test_ctx.hint_label, text_color, 0);

    lv_label_set_text(test_ctx.hint_label, "Press PASS or FAIL button");

    // Create pass button with contrasting colors
    test_ctx.pass_btn = lv_btn_create(test_ctx.color_screen);
    if (!test_ctx.pass_btn) {
        ESP_LOGE(TAG, "Failed to create pass button");
        cleanup_ui_elements();
        return LCD_LVGL_TEST_ERROR;
    }

    lv_obj_set_size(test_ctx.pass_btn, TEST_BTN_WIDTH, TEST_BTN_HEIGHT);
    lv_obj_align(test_ctx.pass_btn, LV_ALIGN_BOTTOM_LEFT, 20, -60);
    lv_obj_set_style_bg_color(test_ctx.pass_btn, lv_color_make(0, 200, 0), 0);  // Green background
    lv_obj_add_event_cb(test_ctx.pass_btn, pass_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *pass_label = lv_label_create(test_ctx.pass_btn);
    if (pass_label) {
        lv_label_set_text(pass_label, "PASS");
        lv_obj_set_style_text_color(pass_label, lv_color_white(), 0);  // White text on green
        lv_obj_center(pass_label);
    }

    // Create fail button with contrasting colors
    test_ctx.fail_btn = lv_btn_create(test_ctx.color_screen);
    if (!test_ctx.fail_btn) {
        ESP_LOGE(TAG, "Failed to create fail button");
        cleanup_ui_elements();
        return LCD_LVGL_TEST_ERROR;
    }

    lv_obj_set_size(test_ctx.fail_btn, TEST_BTN_WIDTH, TEST_BTN_HEIGHT);
    lv_obj_align(test_ctx.fail_btn, LV_ALIGN_BOTTOM_RIGHT, -20, -60);
    lv_obj_set_style_bg_color(test_ctx.fail_btn, lv_color_make(200, 0, 0), 0);  // Red background
    lv_obj_add_event_cb(test_ctx.fail_btn, fail_btn_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *fail_label = lv_label_create(test_ctx.fail_btn);
    if (fail_label) {
        lv_label_set_text(fail_label, "FAIL");
        lv_obj_set_style_text_color(fail_label, lv_color_white(), 0);  // White text on red
        lv_obj_center(fail_label);
    }

    ui_release();

    // Wait for button press with timeout
    TickType_t start_time = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(15000);  // 15 seconds timeout

    while (!test_ctx.button_pressed) {
        vTaskDelay(pdMS_TO_TICKS(100));

        if ((xTaskGetTickCount() - start_time) > timeout) {
            ESP_LOGW(TAG, "Test timeout for color %d", color);
            test_ctx.test_results[color] = 0;  // Fail on timeout
            break;
        }
    }

    // Clean up UI elements after test completion
    cleanup_ui_elements();

    return test_ctx.test_results[color] ? LCD_LVGL_TEST_SUCCESS : LCD_LVGL_TEST_FAIL;
}

lcd_lvgl_test_result_t lcd_lvgl_test_run(void)
{
    ESP_LOGI(TAG, "Starting LCD LVGL test sequence");

    if (lvgl_gui_init() != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LVGL GUI");
        return LCD_LVGL_TEST_ERROR;
    }

    // Run tests for each color
    for (lcd_lvgl_color_t color = LCD_LVGL_COLOR_MAGENTA; color < LCD_LVGL_COLOR_COUNT; color++) {
        ESP_LOGI(TAG, "Testing %s screen", color_names[color]);

        lcd_lvgl_test_result_t result = lcd_lvgl_test_run_single(color);
        if (result == LCD_LVGL_TEST_ERROR) {
            ESP_LOGE(TAG, "Error during %s screen test", color_names[color]);
            return LCD_LVGL_TEST_ERROR;
        }

        // Small delay between tests
        vTaskDelay(pdMS_TO_TICKS(300));
    }

    // Calculate overall result
    bool overall_result = true;
    for (int i = 0; i < LCD_LVGL_COLOR_COUNT; i++) {
        if (!test_ctx.test_results[i]) {
            overall_result = false;
            break;
        }
    }

    // Show final results
    show_test_results(overall_result);

    ESP_LOGI(TAG, "LCD LVGL test sequence completed: %s", overall_result ? "PASS" : "FAIL");
    return overall_result ? LCD_LVGL_TEST_SUCCESS : LCD_LVGL_TEST_FAIL;
}

esp_err_t test_dev_lcd_lvgl_show_menu(void)
{
    ESP_LOGI(TAG, "Starting LCD LVGL test menu");

    lcd_lvgl_test_result_t result = lcd_lvgl_test_run();

    if (result == LCD_LVGL_TEST_SUCCESS) {
        ESP_LOGI(TAG, "LCD LVGL test PASSED");
        return ESP_OK;
    } else if (result == LCD_LVGL_TEST_FAIL) {
        ESP_LOGW(TAG, "LCD LVGL test FAILED");
        return ESP_FAIL;
    } else {
        ESP_LOGE(TAG, "LCD LVGL test ERROR");
        return ESP_ERR_INVALID_STATE;
    }
}
