/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "esp_gmf_app_setup_peripheral.h"
#include "settings.h"
#include "video_capture.h"
#include "esp_video_enc_default.h"
#include "esp_audio_enc_default.h"
#include "mp4_muxer.h"
#include "esp_capture.h"
#include "esp_log.h"

#define TAG "MAIN"

#define RUN_CASE(case, duration) {                        \
    printf("--------Start to run " #case "--------\n");   \
    case(duration);                                       \
    printf("--------End to run " #case "--------\n\n");   \
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

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_ERROR);
    esp_log_level_set("VIDEO_CAPTURE", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    // Initialize board
    esp_gmf_app_codec_info_t codec_info = {
        .play_info = {
            .sample_rate = 16000,
            .channel = 2,
            .bits_per_sample = 16,
            .mode = ESP_GMF_APP_I2S_MODE_STD,
        },
        .record_info = {
            .sample_rate = 16000,
            .channel = 2,
            .bits_per_sample = 16,
#if CONFIG_IDF_TARGET_ESP32S3
            .mode = ESP_GMF_APP_I2S_MODE_TDM,
#else
            .mode = ESP_GMF_APP_I2S_MODE_STD,
#endif  /* CONFIG_IDF_TARGET_ESP32S3 */
        },
    };
    esp_gmf_app_setup_codec_dev(&codec_info);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);

    // Register default audio encoders
    esp_audio_enc_register_default();
    // Register default video encoders
    esp_video_enc_register_default();
    // Register mp4 muxer, if want other format support call related API instead
    mp4_muxer_register();

    // Set scheduler
    esp_capture_set_thread_scheduler(capture_test_scheduler);

    // Run video capture typical cases
    RUN_CASE(video_capture_run, 10000);
    RUN_CASE(video_capture_run_one_shot, 10000);
    RUN_CASE(video_capture_run_with_overlay, 10000);
    if (sdcard_handle) {
        RUN_CASE(video_capture_run_with_muxer, 10000);
    }
    RUN_CASE(video_capture_run_with_customized_process, 10000);
    RUN_CASE(video_capture_run_dual_path, 10000);

    if (sdcard_handle) {
        esp_gmf_app_teardown_sdcard(sdcard_handle);
    }
    ESP_LOGI(TAG, "All case finished");
}
