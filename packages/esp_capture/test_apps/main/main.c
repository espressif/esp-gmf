/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_heap_trace.h"
#include "esp_heap_caps.h"
#include "esp_audio_enc_default.h"
#include "esp_video_enc_default.h"
#include "ts_muxer.h"
#include "mp4_muxer.h"
#include "esp_capture.h"
#include "esp_capture_version.h"
#include "capture_test.h"
#include "capture_builder.h"
#include "settings.h"
#include "esp_log.h"
#include "unity.h"
#include "esp_gmf_app_unit_test.h"
#include "esp_board_device.h"
#include "esp_board_manager_defs.h"

#define TAG                    "MAIN"
#define MAX_LEAK_TRACE_RECORDS 1500

#define CAPTURE_TEST(func, timeout, dual) {                                            \
    ESP_LOGI(TAG, "Starting %s (%s mode)", #func, (dual) ? "dual" : "single");         \
    int _ret = func(timeout, dual);                                                    \
    if (_ret == 0) {                                                                   \
        ESP_LOGI(TAG, "Completed %s (%s mode)", #func, (dual) ? "dual" : "single");    \
    } else {                                                                           \
        ESP_LOGE(TAG, "Fail to run %s (%s mode)", #func, (dual) ? "dual" : "single");  \
    }                                                                                  \
    ESP_LOGW(TAG, "--------------------------------------------------------\n\n");     \
}

static void capture_test_scheduler(const char *thread_name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
{
    if (strcmp(thread_name, "buffer_in") == 0) {
        // AEC feed task can have high priority
        schedule_cfg->stack_size = 6 * 1024;
        schedule_cfg->priority = 10;
        schedule_cfg->core_id = 0;
    } else if (strcmp(thread_name, "venc_0") == 0) {
        // For H264 may need huge stack if use hardware encoder can set it to small value
        schedule_cfg->core_id = 0;
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 1;
    } else if (strcmp(thread_name, "venc_1") == 0) {
        // For H264 may need huge stack if use hardware encoder can set it to small value
        schedule_cfg->core_id = 1;
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 1;
    } else if (strcmp(thread_name, "aenc_0") == 0) {
        // For OPUS encoder it need huge stack, when use G711 can set it to small value
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 2;
        schedule_cfg->core_id = 1;
    } else if (strcmp(thread_name, "AUD_SRC") == 0) {
        schedule_cfg->priority = 15;
    }
}

static void trace_for_leak(bool start)
{
#if CONFIG_IDF_TARGET_ESP32S3
    static heap_trace_record_t *trace_record;
    if (trace_record == NULL) {
        trace_record = heap_caps_malloc(MAX_LEAK_TRACE_RECORDS * sizeof(heap_trace_record_t), MALLOC_CAP_SPIRAM);
        heap_trace_init_standalone(trace_record, MAX_LEAK_TRACE_RECORDS);
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
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */
}

#ifdef TEST_USE_UNITY

TEST_CASE("Customized auto audio capture", "[esp_capture]")
{
    TEST_ESP_OK(auto_customized_audio_capture_test(1000, false));
}

TEST_CASE("Capture with overlay", "[esp_capture]")
{
    TEST_ESP_OK(demo_video_capture_with_overlay(1000, false));
}

TEST_CASE("Capture one shot for one path", "[esp_capture]")
{
    TEST_ESP_OK(demo_capture_one_shot(1000, false));
}

TEST_CASE("Capture one shot for dual path", "[esp_capture]")
{
    TEST_ESP_OK(demo_capture_one_shot(1000, true));
}

TEST_CASE("Auto audio only capture for one path", "[esp_capture]")
{
    TEST_ESP_OK(auto_audio_only_path_test(1000, false));
}

TEST_CASE("Auto audio only capture for dual path", "[esp_capture]")
{
    TEST_ESP_OK(auto_audio_only_path_test(1000, true));
}

TEST_CASE("Auto audio bypass capture for one path", "[esp_capture]")
{
    TEST_ESP_OK(auto_audio_only_bypass_test(1000, false));
}

TEST_CASE("Auto audio bypass capture for dual path", "[esp_capture]")
{
    TEST_ESP_OK(auto_audio_only_bypass_test(1000, true));
}

TEST_CASE("Manual audio only capture for one path", "[esp_capture]")
{
    TEST_ESP_OK(manual_audio_only_path_test(1000, false));
}

TEST_CASE("Manual audio only capture for dual path", "[esp_capture]")
{
    TEST_ESP_OK(manual_audio_only_path_test(1000, true));
}

TEST_CASE("Template audio only capture for one path", "[esp_capture]")
{
    TEST_ESP_OK(advance_audio_only_path_test(1000, false));
}

TEST_CASE("Template audio only capture for dual path", "[esp_capture]")
{
    TEST_ESP_OK(advance_audio_only_path_test(1000, true));
}

#ifdef TEST_WITH_VIDEO

TEST_CASE("Auto video only capture for one path", "[esp_capture]")
{
    TEST_ESP_OK(auto_video_only_path_test(1000, false));
}

TEST_CASE("Auto video only capture for dual path", "[esp_capture]")
{
    TEST_ESP_OK(auto_video_only_path_test(1000, true));
}

TEST_CASE("Manual video only capture for one path", "[esp_capture]")
{
    TEST_ESP_OK(manual_video_only_path_test(1000, false));
}

TEST_CASE("Manual video only capture for dual path", "[esp_capture]")
{
    TEST_ESP_OK(manual_video_only_path_test(1000, true));
}

TEST_CASE("Template video only capture for one path", "[esp_capture]")
{
    TEST_ESP_OK(advance_video_only_path_test(1000, false));
}

TEST_CASE("Template video only capture for dual path", "[esp_capture]")
{
    TEST_ESP_OK(advance_video_only_path_test(1000, true));
}

TEST_CASE("Auto AV capture for one path", "[esp_capture]")
{
    TEST_ESP_OK(auto_av_path_test(1000, false));
}

TEST_CASE("Auto AV capture for dual path", "[esp_capture]")
{
    TEST_ESP_OK(auto_av_path_test(1000, true));
}

TEST_CASE("Auto AV capture dynamic enable for one path", "[esp_capture]")
{
    TEST_ESP_OK(auto_av_path_dynamic_enable_test(1000, false));
}

TEST_CASE("Auto AV capture dynamic enable for dual path", "[esp_capture]")
{
    TEST_ESP_OK(auto_av_path_dynamic_enable_test(1000, true));
}

TEST_CASE("Manual AV capture for one path", "[esp_capture]")
{
    TEST_ESP_OK(manual_av_path_test(1000, false));
}

TEST_CASE("Manual AV capture  for dual path", "[esp_capture]")
{
    TEST_ESP_OK(manual_av_path_test(1000, true));
}

TEST_CASE("Template AV capture for one path", "[esp_capture]")
{
    TEST_ESP_OK(advance_av_path_test(1000, false));
}

TEST_CASE("Template AV capture  for dual path", "[esp_capture]")
{
    TEST_ESP_OK(advance_av_path_test(1000, true));
}
#endif  /* TEST_WITH_VIDEO */

TEST_CASE("Auto AV muxer for one path", "[esp_capture]")
{
    TEST_ESP_OK(auto_av_muxer_path_test(1000, false));
}

TEST_CASE("Auto AV muxer for dual path", "[esp_capture]")
{
    TEST_ESP_OK(auto_av_muxer_path_test(1000, true));
}

TEST_CASE("Template AV muxer for one path", "[esp_capture]")
{
    TEST_ESP_OK(advance_av_muxer_path_test(1000, false));
}

TEST_CASE("Template AV muxer for dual path", "[esp_capture]")
{
    TEST_ESP_OK(advance_av_muxer_path_test(1000, true));
}

TEST_CASE("Storage for one path", "[esp_capture]")
{
    TEST_ESP_OK(demo_capture_to_storage(5000, true));
}
#endif  /* TEST_USE_UNITY */

void app_main(void)
{
    // Set default level
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_log_level_set("CAPTURE_TEST", ESP_LOG_INFO);
    esp_log_level_set("CAPTURE_BUILDER", ESP_LOG_INFO);

    // Initialize board
     esp_err_t ret;
    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_CAMERA);
    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    bool mount_success = true;
    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (ret != ESP_OK) {
        mount_success = false;
    }
    if (mount_success == false) {
        ESP_LOGE(TAG, "Failed to mount SDcard, will skip storage test");
        capture_set_skip_storage_test(true);
    }

    // Register audio and video codecs
    esp_audio_enc_register_default();
    esp_video_enc_register_default();
    // Register for muxer
    ts_muxer_register();
    mp4_muxer_register();
    ESP_LOGI(TAG, "This is esp_capture version %s", esp_capture_get_version());

    // Set scheduler
    esp_capture_set_thread_scheduler(capture_test_scheduler);

    // Test all capture modules
    capture_use_fake_source(true);

#ifdef TEST_USE_UNITY
    esp_gmf_app_test_main();
    return;
#endif  /* TEST_USE_UNITY */

    CAPTURE_TEST(auto_av_muxer_path_test, 5000, true);
    trace_for_leak(true);

    // Basic function test
    CAPTURE_TEST(auto_customized_audio_capture_test, 1000, false);
    CAPTURE_TEST(demo_video_capture_with_overlay, 1000, false);
    CAPTURE_TEST(demo_capture_one_shot, 2000, false);
    CAPTURE_TEST(demo_capture_one_shot, 2000, true);

    // Test for audio only auto mode
    CAPTURE_TEST(auto_audio_only_path_test, 5000, false);
    CAPTURE_TEST(auto_audio_only_path_test, 5000, true);

    // Test for audio only bypass mode
    CAPTURE_TEST(auto_audio_only_bypass_test, 5000, false);
    CAPTURE_TEST(auto_audio_only_bypass_test, 5000, true);

    // Test for audio only manual mode
    CAPTURE_TEST(manual_audio_only_path_test, 5000, false);
    CAPTURE_TEST(manual_audio_only_path_test, 5000, true);

    // Test for audio only advance mode
    CAPTURE_TEST(advance_audio_only_path_test, 5000, false);
    CAPTURE_TEST(advance_audio_only_path_test, 5000, true);

#ifdef TEST_WITH_VIDEO
    // Test for video only auto mode
    CAPTURE_TEST(auto_video_only_path_test, 5000, false);
    CAPTURE_TEST(auto_video_only_path_test, 5000, true);
    // Test for video only manual mode
    CAPTURE_TEST(manual_video_only_path_test, 5000, false);
    CAPTURE_TEST(manual_video_only_path_test, 5000, true);

    // Test for video only advance mode
    CAPTURE_TEST(advance_video_only_path_test, 5000, false);
    CAPTURE_TEST(advance_video_only_path_test, 5000, true);

    // Test for av both auto mode
    CAPTURE_TEST(auto_av_path_test, 5000, false);
    CAPTURE_TEST(auto_av_path_test, 5000, true);

    // Test for av both auto dynamic enable
    CAPTURE_TEST(auto_av_path_dynamic_enable_test, 5000, false);
    CAPTURE_TEST(auto_av_path_dynamic_enable_test, 5000, true);

    // Test for av both manual mode
    CAPTURE_TEST(manual_av_path_test, 5000, false);
    CAPTURE_TEST(manual_av_path_test, 5000, true);

    // Test for advance both manual mode
    CAPTURE_TEST(advance_av_path_test, 5000, false);
    CAPTURE_TEST(advance_av_path_test, 5000, true);
#endif  /* TEST_WITH_VIDEO */

    // Test for av both manual mode
    CAPTURE_TEST(auto_av_muxer_path_test, 5000, false);
    CAPTURE_TEST(auto_av_muxer_path_test, 5000, true);
    CAPTURE_TEST(advance_av_muxer_path_test, 5000, false);
    CAPTURE_TEST(advance_av_muxer_path_test, 5000, true);

    CAPTURE_TEST(demo_capture_to_storage, 10000, false);

    ESP_LOGI(TAG, "All test finished");
    trace_for_leak(false);
}
