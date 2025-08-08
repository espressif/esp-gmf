/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_audio_dec.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_gmf_io_codec_dev.h"

static const char *TAG = "PLAY_SDCARD_MUSIC";

#define PIPELINE_BLOCK_BIT  BIT(0)
#define DEFAULT_PLAY_URL    "/sdcard/test.mp3"

esp_gmf_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGI(TAG, "CB: RECV Pipeline EVT: el: %s-%p, type: %x, sub: %s, payload: %p, size: %d, %p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
    }
    return ESP_GMF_ERR_OK;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    int ret;

    // Configuration of codec to be aligned with audio pipeline output
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = CONFIG_GMF_AUDIO_EFFECT_RATE_CVT_DEST_RATE;
    codec_info.play_info.channel = CONFIG_GMF_AUDIO_EFFECT_CH_CVT_DEST_CH;
    codec_info.play_info.bits_per_sample = CONFIG_GMF_AUDIO_EFFECT_BIT_CVT_DEST_BITS;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);

    // Set default output volume range from [0, 100]
    esp_codec_dev_set_out_vol((esp_codec_dev_handle_t)esp_gmf_app_get_playback_handle() , 80);

    ESP_LOGI(TAG, "[ 1 ] Mount sdcard");

    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);

    ESP_LOGI(TAG, "[ 2 ] Register all the elements and set audio information to play codec device");
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    gmf_loader_setup_io_default(pool);
    gmf_loader_setup_audio_codec_default(pool);
    gmf_loader_setup_audio_effects_default(pool);

    ESP_LOGI(TAG, "[ 3 ] Create audio pipeline");
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_ch_cvt", "aud_bit_cvt", "aud_rate_cvt"};
    ret = esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return; }, "Failed to new pipeline");

    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(pipe), esp_gmf_app_get_playback_handle());

    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);

    esp_gmf_info_sound_t info = {};
    esp_gmf_audio_helper_get_audio_type_by_uri(DEFAULT_PLAY_URL, &info.format_id);
    esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);

    ESP_LOGI(TAG, "[ 3.1 ] Set audio url to play");
    esp_gmf_pipeline_set_in_uri(pipe, DEFAULT_PLAY_URL);

    ESP_LOGI(TAG, "[ 3.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task");
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.stack = 3 * 1024;
    cfg.ctx = NULL;
    cfg.cb = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    ret = esp_gmf_task_init(&cfg, &work_task);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, { return;}, "Failed to create pipeline task");
    esp_gmf_pipeline_bind_task(pipe, work_task);
    esp_gmf_pipeline_loading_jobs(pipe);

    ESP_LOGI(TAG, "[ 3.3 ] Create envent group and listening event from pipeline");
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);
    esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt);

    ESP_LOGI(TAG, "[ 4 ] Start audio_pipeline");
    esp_gmf_pipeline_run(pipe);

    // Wait to finished or got error
    ESP_LOGI(TAG, "[ 5 ] Wait stop event to the pipeline and stop all the pipeline");
    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    esp_gmf_pipeline_stop(pipe);

    ESP_LOGI(TAG, "[ 6 ] Destroy all the resources");
    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    gmf_loader_teardown_audio_effects_default(pool);
    gmf_loader_teardown_audio_codec_default(pool);
    gmf_loader_teardown_io_default(pool);
    esp_gmf_pool_deinit(pool);
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
}
