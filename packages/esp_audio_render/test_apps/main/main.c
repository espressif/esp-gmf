/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_heap_trace.h"
#include "esp_heap_caps.h"
#include "audio_render_test.h"
#include "unity.h"
#include "esp_gmf_app_unit_test.h"
#include "settings.h"
#include "esp_log.h"

#define TAG                    "MAIN"
#define MAX_LEAK_TRACE_RECORDS 200

#define AUDIO_RENDER_TEST(func, run_count)                                             \
    {                                                                                  \
        ESP_LOGI(TAG, "Starting %s run %d", #func, run_count);                         \
        int _ret = func(run_count);                                                    \
        if (_ret == 0) {                                                               \
            ESP_LOGI(TAG, "Success to run %s", #func);                                 \
        } else {                                                                       \
            ESP_LOGE(TAG, "Fail to run %s", #func);                                    \
        }                                                                              \
        ESP_LOGW(TAG, "--------------------------------------------------------\n\n"); \
    }

#ifndef TEST_USE_UNITY
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

#else

TEST_CASE("Audio Render Process bypass", "[esp_audio_render]")
{
    TEST_ESP_OK(audio_render_proc_bypass_test(20));
}

TEST_CASE("Audio Render Process basic", "[esp_audio_render]")
{
    TEST_ESP_OK(audio_render_proc_basic_test(20));
}

TEST_CASE("Audio Render Process typical", "[esp_audio_render]")
{
    TEST_ESP_OK(audio_render_proc_typical_test(20));
}

TEST_CASE("Audio Render One Stream No Proc", "[esp_audio_render]")
{
    TEST_ESP_OK(audio_render_one_stream_no_proc(20));
}

TEST_CASE("Audio Render One Stream With Proc", "[esp_audio_render]")
{
    TEST_ESP_OK(audio_render_one_stream_with_proc(20));
}

TEST_CASE("Audio Render One Stream With Encoder Proc", "[esp_audio_render]")
{
    TEST_ESP_OK(audio_render_one_stream_with_enc_post(64));
}

TEST_CASE("Audio Render Dual Stream No Proc", "[esp_audio_render]")
{
    TEST_ESP_OK(audio_render_dual_stream_no_proc(20));
}

TEST_CASE("Audio Render Dual Stream With Proc", "[esp_audio_render]")
{
    TEST_ESP_OK(audio_render_dual_stream_with_proc(20));
}

TEST_CASE("Audio Render Dual Stream with One Slow", "[esp_audio_render]")
{
    TEST_ESP_OK(audio_render_dual_stream_one_slow(20));
}

TEST_CASE("Audio Render with no pool", "[esp_audio_render]")
{
    TEST_ESP_OK(audio_render_with_no_pool(20));
}

TEST_CASE("Audio Render with solo", "[esp_audio_render]")
{
    TEST_ESP_OK(audio_render_dual_stream_solo(20));
}

#endif  /* TEST_USE_UNITY */

void app_main(void)
{
#ifdef TEST_USE_UNITY
    esp_gmf_app_test_main();
#else
    trace_for_leak(true);

    // Basic function test
    AUDIO_RENDER_TEST(audio_render_proc_bypass_test, 20);
    AUDIO_RENDER_TEST(audio_render_proc_basic_test, 20);
    AUDIO_RENDER_TEST(audio_render_proc_typical_test, 20);
    AUDIO_RENDER_TEST(audio_render_one_stream_no_proc, 20);
    AUDIO_RENDER_TEST(audio_render_one_stream_with_proc, 20);
    AUDIO_RENDER_TEST(audio_render_one_stream_with_enc_post, 64);
    AUDIO_RENDER_TEST(audio_render_dual_stream_no_proc, 20);
    AUDIO_RENDER_TEST(audio_render_dual_stream_with_proc, 20);
    AUDIO_RENDER_TEST(audio_render_dual_stream_one_slow, 20);

    trace_for_leak(false);
 #endif  /* TEST_USE_UNITY */
    ESP_LOGI(TAG, "All test finished");
}
