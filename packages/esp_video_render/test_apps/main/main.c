/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_heap_trace.h"
#include "esp_heap_caps.h"
#include "video_render_test.h"
#include "unity.h"
#include "esp_gmf_app_unit_test.h"
#include "esp_gmf_oal_thread.h"
#include "settings.h"
#include "esp_log.h"
#include "esp_board_manager_includes.h"

#define TAG                     "MAIN"
#define MAX_LEAK_TRACE_RECORDS  200

#define VIDEO_RENDER_TEST(func, run_count)  {                                       \
    ESP_LOGI(TAG, "Starting %s run %d", #func, run_count);                          \
    int _ret = func(run_count);                                                     \
    if (_ret == 0) {                                                                \
        ESP_LOGI(TAG, "Success to run %s", #func);                                  \
    } else {                                                                        \
        ESP_LOGE(TAG, "Fail to run %s", #func);                                     \
    }                                                                               \
    ESP_LOGW(TAG, "--------------------------------------------------------\n\n");  \
}

static bool mount_success = true;

#ifndef TEST_USE_UNITY
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

#else

TEST_CASE("Backend render use FB", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_lcd_backend_fb_test(30));
}

TEST_CASE("Backend render use none FB", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_lcd_backend_none_fb_test(30));
}

TEST_CASE("Decoder proc test", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_proc_decode_test(30));
}

TEST_CASE("Color converter test", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_proc_color_convert_test(30));
}

TEST_CASE("Scale test", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_proc_scale_test(30));
}

TEST_CASE("Stream rotate test", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_stream_rotate_test(5));
}

TEST_CASE("Proc chain test", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_proc_chain_test(30));
}

TEST_CASE("Image decode test", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_image_decode_test(1));
}

TEST_CASE("Video proc test", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_proc_wrapper_test(1));
}

TEST_CASE("Video blender test", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_blend_test(1));
}

TEST_CASE("Video blender bitblt test", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_blend_bitblt_test(1));
}

TEST_CASE("Video blender transparent color test", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_blend_transparent_color_test(1));
}

TEST_CASE("Video compose monitor test", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_compose_monitor_test());
}

TEST_CASE("Video render stream with FB", "[esp_video_render]")
{
    TEST_ESP_OK(video_render_one_stream_with_fb(1));
}

TEST_CASE("One stream video only", "[esp_video_render]")
{
    TEST_ESP_OK(demo_one_stream_video_only(10));
}

TEST_CASE("One stream video + overlay", "[esp_video_render]")
{
    TEST_ESP_OK(demo_one_stream_video_with_overlay(5));
}

TEST_CASE("Dual stream overlay only", "[esp_video_render]")
{
    TEST_ESP_OK(demo_dual_stream_overlay_only(5));
}

TEST_CASE("Dual streams video", "[esp_video_render]")
{
    TEST_ESP_OK(demo_dual_streams_video(10));
}

TEST_CASE("Dual streams visible", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_stream_visible(20));
#else
    TEST_ESP_OK(demo_stream_visible(10));
#endif  /* FULL_TEST */
}

TEST_CASE("Widget test", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(simple_widget_test(true, 16));
#else
    TEST_ESP_OK(simple_widget_test(true, 2));
#endif  /* FULL_TEST */
}

TEST_CASE("Widget no cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(simple_widget_test(false, 16));
#else
    TEST_ESP_OK(simple_widget_test(false, 2));
#endif  /* FULL_TEST */
}

TEST_CASE("Dual container no overlap with cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(dual_container_no_overlap(true, 4));
#else
    TEST_ESP_OK(dual_container_no_overlap(true, 2));
#endif  /* FULL_TEST */
}

TEST_CASE("Dual container no overlap no cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(dual_container_no_overlap(false, 16));
#else
    TEST_ESP_OK(dual_container_no_overlap(false, 2));
#endif  /* FULL_TEST */
}

TEST_CASE("Dual container overlap with cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(dual_container_overlap(true, 16));
#else
    TEST_ESP_OK(dual_container_overlap(true, 2));
#endif  /* FULL_TEST */
}

TEST_CASE("Dual container overlap no cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(dual_container_overlap(false, 16));
#else
    TEST_ESP_OK(dual_container_overlap(false, 2));
#endif  /* FULL_TEST */
}

TEST_CASE("Bouncing balls game", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_bouncing_balls_game(60));
#else
    TEST_ESP_OK(demo_bouncing_balls_game(10));
#endif  /* FULL_TEST */
}

TEST_CASE("Clock widget with cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_clock_widget(true, 30));
#else
    TEST_ESP_OK(demo_clock_widget(true, 2));
#endif  /* FULL_TEST */
}

TEST_CASE("Clock widget without cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_clock_widget(false, 30));
#else
    TEST_ESP_OK(demo_clock_widget(false, 2));
#endif  /* FULL_TEST */
}

TEST_CASE("Text widget basic with cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_text_widget(true, 200, false, false));
#else
    TEST_ESP_OK(demo_text_widget(true, 20, false, false));
#endif  /* FULL_TEST */
}

TEST_CASE("Text widget basic without cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_text_widget(false, 200, false, false));
#else
    TEST_ESP_OK(demo_text_widget(false, 20, false, false));
#endif  /* FULL_TEST */
}

TEST_CASE("Text widget scroll with cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_text_widget(true, 300, true, false));
#else
    TEST_ESP_OK(demo_text_widget(true, 30, true, false));
#endif  /* FULL_TEST */
}

TEST_CASE("Text widget scroll without cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_text_widget(false, 300, true, false));
#else
    TEST_ESP_OK(demo_text_widget(false, 30, true, false));
#endif  /* FULL_TEST */
}

TEST_CASE("Text widget emoji with cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_text_widget(true, 200, false, true));
#else
    TEST_ESP_OK(demo_text_widget(true, 20, false, true));
#endif  /* FULL_TEST */
}

TEST_CASE("Text widget emoji scroll with cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_text_widget(true, 300, true, true));
#else
    TEST_ESP_OK(demo_text_widget(true, 30, true, true));
#endif  /* FULL_TEST */
}

TEST_CASE("Text widget alignment with cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_text_widget_alignment(true, 60));
#else
    TEST_ESP_OK(demo_text_widget_alignment(true, 20));
#endif  /* FULL_TEST */
}

TEST_CASE("Text widget alignment without cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_text_widget_alignment(false, 60));
#else
    TEST_ESP_OK(demo_text_widget_alignment(false, 20));
#endif  /* FULL_TEST */
}

TEST_CASE("Text widget scroll dedicated test with cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_text_widget_scroll(true, 500));
#else
    TEST_ESP_OK(demo_text_widget_scroll(true, 60));
#endif  /* FULL_TEST */
}

TEST_CASE("Text widget scroll dedicated test without cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_text_widget_scroll(false, 500));
#else
    TEST_ESP_OK(demo_text_widget_scroll(false, 60));
#endif  /* FULL_TEST */
}

TEST_CASE("Stream src_rect change", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_stream_src_rect_change(100));
#else
    TEST_ESP_OK(demo_stream_src_rect_change(10));
#endif  /* FULL_TEST */
}

TEST_CASE("Stream disp_rect change", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_stream_disp_rect_change(20));
#else
    TEST_ESP_OK(demo_stream_disp_rect_change(10));
#endif  /* FULL_TEST */
}

TEST_CASE("Stream src_rect and disp_rect change", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_stream_src_disp_rect_change(20));
#else
    TEST_ESP_OK(demo_stream_src_disp_rect_change(10));
#endif  /* FULL_TEST */
}

TEST_CASE("Dual stream rect change", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_dual_stream_rect_change(20));
#else
    TEST_ESP_OK(demo_dual_stream_rect_change(10));
#endif  /* FULL_TEST */
}

TEST_CASE("Stream zorder test", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_stream_zorder_test(20));
#else
    TEST_ESP_OK(demo_stream_zorder_test(10));
#endif  /* FULL_TEST */
}

TEST_CASE("Full screen stream with overlay widget", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_fullscreen_stream_with_overlay_widget(30));
#else
    TEST_ESP_OK(demo_fullscreen_stream_with_overlay_widget(3));
#endif  /* FULL_TEST */
}

TEST_CASE("Dual stream one video one overlay", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_dual_stream_with_overlay(30));
#else
    TEST_ESP_OK(demo_dual_stream_with_overlay(3));
#endif  /* FULL_TEST */
}

TEST_CASE("Xiaozhi panel with cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_xiaozhi_panel(true, 200));
#else
    TEST_ESP_OK(demo_xiaozhi_panel(true, 20));
#endif  /* FULL_TEST */
}

TEST_CASE("Xiaozhi panel without cache", "[esp_video_render]")
{
#if FULL_TEST
    TEST_ESP_OK(demo_xiaozhi_panel(false, 200));
#else
    TEST_ESP_OK(demo_xiaozhi_panel(false, 20));
#endif  /* FULL_TEST */
}

static bool run_end = false;

void dual_run(void *arg)
{
    demo_dual_eyes_on_single_display(2);
    ESP_LOGI(TAG, "Dual Exited");
    run_end = true;
    esp_gmf_oal_thread_delete(NULL);
}

TEST_CASE("Dual eyes single display", "[esp_video_render]")
{
    if (mount_success) {
        run_end = false;
        esp_gmf_oal_thread_create(NULL, "DUAL", dual_run, NULL, 32 * 1024, 10, true, 0);
        int n = 30;
        while (run_end == false && n-- > 0) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
        TEST_ESP_OK(run_end ? ESP_OK : ESP_FAIL);
    }
}

TEST_CASE("Render use LVGL", "[esp_video_render]")
{
    static bool use_lvgl = false;
    use_lvgl = !use_lvgl;
    video_render_use_lvgl(use_lvgl);
    ESP_LOGI(TAG, "Now render use %s", use_lvgl ? "LVGL" : "LCD");
    TEST_ESP_OK(demo_one_stream_video_only(10));
}

#endif  /* TEST_USE_UNITY */

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

void app_main(void)
{
    board_init_optional_gpio_expander();
    esp_board_device_init(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD);
    int ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (ret != ESP_OK) {
        mount_success = false;
    }
#ifdef TEST_USE_UNITY
    esp_gmf_app_test_main();
#else
    trace_for_leak(true);

    VIDEO_RENDER_TEST(video_render_proc_wrapper_test, 1);
    VIDEO_RENDER_TEST(demo_one_stream_video_only, 60);

    trace_for_leak(false);
#endif  /* TEST_USE_UNITY */
    ESP_LOGI(TAG, "All test finished");
}
