/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_board_manager_includes.h"
#include "esp_heap_trace.h"
#include "esp_heap_caps.h"
#include "video_render.h"
#include "settings.h"
#include "esp_log.h"

#define TAG  "MAIN"

#define MAX_LEAK_TRACE_RECORDS  500
#include "freertos/FreeRTOS.h"
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
    board_init_optional_gpio_expander();
    int ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LCD");
        return;
    }
    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card");
        return;
    }
    trace_for_leak(true);
    for (int i = 0; i < 2; i++) {
        video_render_use_lvgl(i > 0);
        ESP_LOGI(TAG, "Now render use %s", i ? "LVGL" : "LCD");

        // Render sync, it will try to render according the setting fps
        video_render_play_one_video(LEFT_FILE, false, 30);

        // Sync render one file
        video_render_play_one_video(LEFT_FILE, true, 30);

        // Render async with progress bar with 30fps
        video_render_play_one_video_with_progress(LEFT_FILE, 30);

        // Render dual video
        video_render_play_dual_video(LEFT_FILE, RIGHT_FILE, 30);

        // Render dual video, each with a progress-bar
        video_render_play_dual_video_with_progress(LEFT_FILE, RIGHT_FILE, 30);
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    trace_for_leak(false);
}
