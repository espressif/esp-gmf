/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * Test API C++ compilation only, not as a example reference
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"

static const char *TAG = "CXX_BUILD_TEST";

static esp_gmf_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    return ESP_GMF_ERR_OK;
}

extern "C" void test_cxx_build(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    int ret = 0;

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_enc"};
    ret = esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), "io_file", &pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return; }, "Failed to new pipeline");

    esp_gmf_pipeline_set_out_uri(pipe, "/sdcard/test.aac");

    esp_gmf_element_handle_t enc_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_enc", &enc_el);

    esp_gmf_info_sound_t info = {
        .format_id = 0,
        .sample_rates = 16000,
        .bitrate = 0,
        .channels = 1,
        .bits = 16,
    };
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    ret = esp_gmf_task_init(&cfg, &work_task);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return;}, "Failed to create pipeline task");
    esp_gmf_pipeline_bind_task(pipe, work_task);
    esp_gmf_pipeline_loading_jobs(pipe);

    esp_gmf_pipeline_set_event(pipe, _pipeline_event, NULL);

    esp_gmf_pipeline_run(pipe);

    vTaskDelay(10000 / portTICK_PERIOD_MS);
    esp_gmf_pipeline_stop(pipe);

    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    esp_gmf_pool_deinit(pool);
}
