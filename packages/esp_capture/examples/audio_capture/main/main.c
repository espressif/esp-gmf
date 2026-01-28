/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include "settings.h"
#include "audio_capture.h"
#include "esp_audio_enc_default.h"
#include "esp_board_device.h"
#include "esp_board_manager_defs.h"
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
    esp_log_level_set("AUDIO_CAPTURE", ESP_LOG_INFO);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_err_t ret;
    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    bool mount_success = true;
    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (ret != ESP_OK) {
        mount_success = false;
    }

    // Register default audio encoders
    esp_audio_enc_register_default();
    // Register mp4 muxer
    mp4_muxer_register();

    // Set scheduler
    esp_capture_set_thread_scheduler(capture_test_scheduler);

    // Run audio capture typical cases
    RUN_CASE(audio_capture_run, 10000);
    RUN_CASE(audio_capture_run_with_aec, 10000);
    if (mount_success) {
        RUN_CASE(audio_capture_run_with_muxer, 10000);
    }
    RUN_CASE(audio_capture_run_with_customized_process, 10000);
    ESP_LOGI(TAG, "All case finished");
}
