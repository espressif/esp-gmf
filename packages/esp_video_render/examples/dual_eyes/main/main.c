/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_heap_trace.h"
#include "esp_heap_caps.h"
#include "settings.h"
#include "dual_eyes.h"
#include "freertos/FreeRTOS.h"
#include "esp_board_manager_includes.h"

#define TAG  "MAIN"

#define MAX_LEAK_TRACE_RECORDS  500

static void trace_for_leak(bool start)
{
#if defined(CONFIG_IDF_TARGET_ESP32S3) && !(defined(CONFIG_HEAP_TRACING_OFF))
    static heap_trace_record_t *trace_record;
    if (trace_record == NULL) {
        trace_record = heap_caps_malloc(MAX_LEAK_TRACE_RECORDS * sizeof(heap_trace_record_t), MALLOC_CAP_SPIRAM);
        if (trace_record) {
            heap_trace_init_standalone(trace_record, MAX_LEAK_TRACE_RECORDS);
        }
    }
    if (trace_record == NULL) {
        ESP_LOGE(TAG, "No memory to start trace");
        return;
    }
    static bool started = false;
    if (start) {
        if (started == false) {
            heap_trace_start(HEAP_TRACE_LEAKS);
            started = true;
        }
    } else {
        heap_trace_dump();
    }
#endif  /* defined(CONFIG_IDF_TARGET_ESP32S3) && !(defined(CONFIG_HEAP_TRACING_OFF)) */
}

static esp_err_t board_init_optional_gpio_expander(void)
{
#if CONFIG_ESP_BOARD_DEV_GPIO_EXPANDER_SUPPORT
    static bool gpio_expander_inited = false;
    if (gpio_expander_inited) {
        return ESP_OK;
    }
    esp_err_t ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_GPIO_EXPANDER);
    if (ret == ESP_OK) {
        gpio_expander_inited = true;
    } else {
        ESP_LOGW(TAG, "Failed to initialize gpio expander: %s", esp_err_to_name(ret));
    }
#endif  /* CONFIG_ESP_BOARD_DEV_GPIO_EXPANDER_SUPPORT */
    return ESP_OK;
}

void app_main()
{
    // Prepare board related
#ifndef DUAL_EYES_ON_DUAL_DISPLAY
    board_init_optional_gpio_expander();
    int ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD");
        return;
    }
    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD or SD card");
        return;
    }
#endif  /* DUAL_EYES_ON_DUAL_DISPLAY */

    trace_for_leak(true);
    dual_eyes_set_repeat_count(20);
#ifdef DUAL_EYES_ON_DUAL_DISPLAY
    // Try to display on dual display without LVGL
    dual_eyes_on_dual_display(LEFT_FILE, RIGHT_FILE, 30, false);
    // Try to display on dual display with LVGL
    dual_eyes_on_dual_display(LEFT_FILE, RIGHT_FILE, 30, true);
#else
    // Try to display on single display without LVGL
    dual_eyes_on_one_display(LEFT_FILE, RIGHT_FILE, 20, false);
    // Try to display on single display with LVGL
    dual_eyes_on_one_display(LEFT_FILE, RIGHT_FILE, 20, true);
#endif  /* DUAL_EYES_ON_DUAL_DISPLAY */
    // Check for leakage
    vTaskDelay(pdMS_TO_TICKS(2000));
    trace_for_leak(false);
    ESP_LOGI(TAG, "Dual eyes test finished");
}
