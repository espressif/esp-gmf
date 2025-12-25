/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_helper.h"
#include "esp_board_manager_includes.h"
#include "esp_gmf_io_codec_dev.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_gmf_audio_muxer.h"

static const char *TAG = "REC_MUXER";

#define DEFAULT_RECORD_SAMPLE_RATE      48000
#define DEFAULT_RECORD_CHANNEL          1
#define DEFAULT_RECORD_BITS             16
#define DEFAULT_RECORD_BITRATE          90000
#define DEFAULT_RECODER_CODEC_TYPE      ESP_AUDIO_TYPE_ALAC
#define DEFAULT_RECODER_MUXER_TYPE      ESP_MUXER_TYPE_MP4
#define DEFAULT_RECODER_SLICE_DURATION  60000
#define DEFAULT_RECORD_DURATION_MS      10000

esp_gmf_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGI(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             OBJ_GET_TAG(event->from), event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    return ESP_GMF_ERR_OK;
}

static const char *get_file_extension(esp_muxer_type_t muxer_type)
{
    switch (muxer_type) {
        case ESP_MUXER_TYPE_TS:
            return "ts";
        case ESP_MUXER_TYPE_MP4:
            return "mp4";
        case ESP_MUXER_TYPE_FLV:
            return "flv";
        case ESP_MUXER_TYPE_WAV:
            return "wav";
        case ESP_MUXER_TYPE_CAF:
            return "caf";
        case ESP_MUXER_TYPE_OGG:
            return "ogg";
        case ESP_MUXER_TYPE_AVI:
            return "avi";
        default:
            return NULL;
    }
}

static int muxer_file_pattern_cb(esp_muxer_slice_info_t *info, void *ctx)
{
    esp_gmf_audio_muxer_cfg_t *muxer_cfg_ptr = (esp_gmf_audio_muxer_cfg_t *)ctx;
    const char *ext = get_file_extension(muxer_cfg_ptr->muxer_type);
    if (ext == NULL) {
        return -1;
    }
    snprintf(info->file_path, info->len, "/sdcard/esp_gmf_muxer_%03d.%s", info->slice_index, ext);
    ESP_LOGI(TAG, "Muxer file pattern: %s (slice index: %d)", info->file_path, info->slice_index);
    return 0;
}

static esp_gmf_err_t audio_enc_spec_info_query(void *ctx, esp_gmf_audio_helper_spec_info_t *spec_info)
{
    return esp_gmf_audio_enc_get_spec_info((esp_gmf_element_handle_t)ctx, spec_info);
}

static int setup_peripheral(esp_codec_dev_handle_t *rec_handle)
{
    int ret = ESP_OK;
    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to init SD card");
    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to init audio ADC");
    dev_audio_codec_handles_t *rec_dev_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&rec_dev_handle);
    ESP_GMF_NULL_CHECK(TAG, rec_dev_handle, return ESP_GMF_ERR_NOT_FOUND);
    esp_codec_dev_handle_t record_handle = rec_dev_handle->codec_dev;
    ESP_GMF_NULL_CHECK(TAG, record_handle, return ESP_GMF_ERR_NOT_FOUND);
    ret = esp_codec_dev_set_in_gain(record_handle, 32);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to set input gain");
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = DEFAULT_RECORD_SAMPLE_RATE,
        .channel = DEFAULT_RECORD_CHANNEL,
        .bits_per_sample = DEFAULT_RECORD_BITS,
    };
#ifdef CONFIG_BOARD_LYRAT_MINI_V1_1
    if (fs.channel == 1) {
        fs.channel = 2;
        fs.channel_mask = 0x02;
    }
#endif  /* CONFIG_BOARD_LYRAT_MINI_V1_1 */
    ret = esp_codec_dev_open(record_handle, &fs);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to open record codec");
    *rec_handle = record_handle;
    return ESP_OK;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    int ret = 0;
    ESP_LOGI(TAG, "[ 1 ] Setup peripheral for audio codec device and sdcard");
    esp_codec_dev_handle_t record_handle = NULL;
    ret = setup_peripheral(&record_handle);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to setup peripheral");

    ESP_LOGI(TAG, "[ 2 ] Register all the elements and set audio information to record codec device");
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    gmf_loader_setup_io_default(pool);
    gmf_loader_setup_audio_codec_default(pool);
    gmf_loader_setup_audio_muxer_default(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    ESP_LOGI(TAG, "[ 3 ] Create audio pipeline with encoder and muxer");
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_enc", "aud_muxer"};
    ret = esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), NULL, &pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to new pipeline");

    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_IN_INSTANCE(pipe), record_handle);

    ESP_LOGI(TAG, "[ 3.1 ] Configure muxer element");
    esp_gmf_element_handle_t muxer_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_muxer", &muxer_el);
    ESP_GMF_NULL_CHECK(TAG, muxer_el, return);

    esp_gmf_audio_muxer_cfg_t *muxer_cfg_ptr = (esp_gmf_audio_muxer_cfg_t *)OBJ_GET_CFG(muxer_el);
    ESP_GMF_NULL_CHECK(TAG, muxer_cfg_ptr, return);
    muxer_cfg_ptr->muxer_type = DEFAULT_RECODER_MUXER_TYPE;
    muxer_cfg_ptr->output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_FILE;
    muxer_cfg_ptr->slice_duration = DEFAULT_RECODER_SLICE_DURATION;
    muxer_cfg_ptr->url_pattern = muxer_file_pattern_cb;
    muxer_cfg_ptr->url_ctx = (void *)muxer_cfg_ptr;
    muxer_cfg_ptr->codec = DEFAULT_RECODER_CODEC_TYPE;

    ESP_LOGI(TAG, "[ 3.2 ] Reconfig audio encoder type and report information to the record pipeline");
    esp_gmf_element_handle_t enc_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_enc", &enc_el);
    ESP_GMF_NULL_CHECK(TAG, enc_el, return);
    muxer_cfg_ptr->get_codec_spec_info_cb = audio_enc_spec_info_query;
    muxer_cfg_ptr->get_codec_spec_info_ctx = enc_el;

    esp_gmf_info_sound_t info = {
        .sample_rates = DEFAULT_RECORD_SAMPLE_RATE,
        .channels = DEFAULT_RECORD_CHANNEL,
        .bits = DEFAULT_RECORD_BITS,
        .bitrate = DEFAULT_RECORD_BITRATE,
        .format_id = DEFAULT_RECODER_CODEC_TYPE,
    };
    esp_gmf_audio_enc_reconfig_by_sound_info(enc_el, &info);
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    ESP_LOGI(TAG, "[ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task");
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.stack_in_ext = true;
    cfg.name = "gmf_rec_muxer";
    esp_gmf_task_handle_t work_task = NULL;
    ret = esp_gmf_task_init(&cfg, &work_task);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to create pipeline task");
    esp_gmf_pipeline_bind_task(pipe, work_task);
    esp_gmf_pipeline_loading_jobs(pipe);

    ESP_LOGI(TAG, "[ 3.4 ] Create event group and listening event from pipeline");
    esp_gmf_pipeline_set_event(pipe, _pipeline_event, NULL);

    ESP_LOGI(TAG, "[ 4 ] Start audio_pipeline");
    esp_gmf_pipeline_run(pipe);

    ESP_LOGI(TAG, "[ 5 ] Wait for a while to stop record pipeline");
    vTaskDelay(DEFAULT_RECORD_DURATION_MS / portTICK_PERIOD_MS);
    esp_gmf_pipeline_stop(pipe);

    ESP_LOGI(TAG, "[ 6 ] Destroy all the resources");
    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    gmf_loader_teardown_audio_codec_default(pool);
    gmf_loader_teardown_io_default(pool);
    gmf_loader_teardown_audio_muxer_default(pool);
    esp_gmf_pool_deinit(pool);
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
}
