/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"

#include "esp_board_manager_includes.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_gmf_oal_sys.h"

#define PIPELINE_BLOCK_BIT    BIT(0)
#define PIPELINE_OPENING_BIT  BIT(1)
// Set to true to test HTTP download speed only (no SD card writing), false for full pipeline with SD card storage
#define ONLY_ENABLE_HTTP      (false)

static const char *TAG      = "HTTP_DOWNLOAD_TO_SDCARD";

static const char *http_url = "https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.opus";
#if !ONLY_ENABLE_HTTP
static const char *save_url = "/sdcard/ff-16b-2c-44100hz.opus";
#endif

esp_gmf_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGI(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             OBJ_GET_TAG(event->from), event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if (event->sub == ESP_GMF_EVENT_STATE_OPENING) {
        xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_OPENING_BIT);
    }
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
    }
    return ESP_GMF_ERR_OK;
}

void app_main(void)
{
    ESP_GMF_MEM_SHOW("APP_MAIN");
    esp_log_level_set("*", ESP_LOG_INFO);

    ESP_LOGI(TAG, "[ 1 ] Mount sdcard and connect to wifi");
    int ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init SD card");
        return;
    }
    esp_gmf_app_wifi_connect();

    ESP_LOGI(TAG, "[ 2 ] Register elements and setup io");
    esp_gmf_pool_handle_t pool = NULL;
    ESP_GMF_RET_ON_NOT_OK(TAG, esp_gmf_pool_init(&pool), goto cleanup, "Failed to init pool");
    ESP_GMF_RET_ON_NOT_OK(TAG, gmf_loader_setup_io_default(pool), goto cleanup, "Failed to setup io");
    ESP_GMF_RET_ON_NOT_OK(TAG, gmf_loader_setup_misc_default(pool), goto cleanup, "Failed to setup misc element");
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    ESP_LOGI(TAG, "[ 3 ] Create pipeline");
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"copier"};
#if ONLY_ENABLE_HTTP
    ret = esp_gmf_pool_new_pipeline(pool, "io_http", name, sizeof(name) / sizeof(char *), NULL, &pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to new pipeline");
#else
    ret = esp_gmf_pool_new_pipeline(pool, "io_http", name, sizeof(name) / sizeof(char *), "io_file", &pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to new pipeline");
    ret = esp_gmf_pipeline_set_out_uri(pipe, save_url);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to set out uri");
#endif  /* ONLY_ENABLE_HTTP */

    ret = esp_gmf_pipeline_set_in_uri(pipe, http_url);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to set in uri");

    ESP_LOGI(TAG, "[ 3.1 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task");
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.name = "pipeline_task";
    cfg.thread.stack = 1024 * 5;
    cfg.thread.stack_in_ext = true;
    ret = esp_gmf_task_init(&cfg, &work_task);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to create pipeline task");
    ret = esp_gmf_pipeline_bind_task(pipe, work_task);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to bind task to pipeline");
    ret = esp_gmf_task_set_timeout(work_task, 20000);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to set task timeout");
    ret = esp_gmf_pipeline_loading_jobs(pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to load linked element jobs");

    ESP_LOGI(TAG, "[ 3.2 ] Create an event group and listen for events from the pipeline");
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, goto cleanup);
    ret = esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to set event");

    ESP_LOGI(TAG, "[ 4 ] Start pipeline");
    ret = esp_gmf_pipeline_run(pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to run pipeline");
    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_OPENING_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    int start_time = esp_gmf_oal_sys_get_time_ms();

    // Wait until completion or error occurs
    ESP_LOGI(TAG, "[ 5 ] Wait stop event to the pipeline and stop all the pipeline");
    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    int duration = esp_gmf_oal_sys_get_time_ms() - start_time;
    ret = esp_gmf_pipeline_stop(pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to stop pipeline");

    // Calculate the total speed
    esp_gmf_io_handle_t http_io = NULL;
    ret = esp_gmf_pipeline_get_in(pipe, &http_io);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to get in element handle");
    uint64_t file_size = 0;
    ret = esp_gmf_io_get_size(http_io, &file_size);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, goto cleanup, "Failed to get size");
    if (duration) {
        float speed = ((float)file_size * 1000.0) / ((float)duration * 1024 * 1024);
        ESP_LOGI(TAG, "Http download and write to sdcard speed: %.2f MB/s", speed);
    }

cleanup:
    ESP_LOGI(TAG, "[ 6 ] Destroy all the resources");
    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    gmf_loader_teardown_misc_default(pool);
    gmf_loader_teardown_io_default(pool);
    esp_gmf_pool_deinit(pool);
    esp_gmf_app_wifi_disconnect();
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    ESP_GMF_MEM_SHOW("APP_MAIN_END");
}
