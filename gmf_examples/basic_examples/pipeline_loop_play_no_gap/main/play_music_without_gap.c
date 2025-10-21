/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
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
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_audio_dec.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_board_manager_includes.h"

#define PIPELINE_BLOCK_BIT    BIT(0)
#define PLAYBACK_DURATION_MS  (60000)
#define PLAYBACK_VOLUME       (60)

typedef struct {
    bool                 stop_loop;   /*!< Whether to stop the loop playback */
    int                  play_index;  /*!< Index of the file to play */
    int                  file_count;  /*!< Total number of files in the playlist */
    const char         **file_path;   /*!< Array of file paths to play */
    esp_gmf_io_handle_t  io;          /*!< IO handle for this pipeline */
} pipeline_strategy_ctx_t;

/**
 * @brief  File list for playback in continuous loop mode
 * @note  All files must be in the same format (e.g., MP3)
 * @note  Files must be located in the SD card directory (e.g., /sdcard/test.mp3)
 */
static const char *play_urls[] = {
    "/sdcard/test.mp3",
    "/sdcard/test_short.mp3",
};

static const char *TAG = "PLAY_MUSIC_NO_GAP";

esp_gmf_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGI(TAG, "CB: RECV Pipeline EVT: el: %s-%p, type: %x, sub: %s, payload: %p, size: %d, %p",
             OBJ_GET_TAG(event->from), event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);

    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
    }
    return ESP_GMF_ERR_OK;
}

static inline void _pipeline_set_finish_stop_strategy(pipeline_strategy_ctx_t *strategy_ctx)
{
    if (!strategy_ctx) {
        return;
    }
    strategy_ctx->stop_loop = true;
}

static esp_gmf_err_t pipeline_strategy_finish_func(esp_gmf_task_handle_t handle, uint8_t strategy_type, void *ctx, uint8_t *out_action)
{
    pipeline_strategy_ctx_t *strategy_ctx = (pipeline_strategy_ctx_t *)ctx;
    esp_gmf_io_handle_t io = strategy_ctx->io;
    if (strategy_type == GMF_TASK_STRATEGY_TYPE_ABORT) {
        *out_action = GMF_TASK_STRATEGY_ACTION_RESET;
    } else if (strategy_type == GMF_TASK_STRATEGY_TYPE_FINISH) {
        strategy_ctx->play_index++;
        if (!strategy_ctx->stop_loop) {
            *out_action = GMF_TASK_STRATEGY_ACTION_RESET;
            const char *file_path = strategy_ctx->file_path[strategy_ctx->play_index % strategy_ctx->file_count];
            // If file is the same as the previous one, just reset the io is enough, otherwise need to close and open the io
            esp_gmf_io_close(io);
            esp_gmf_io_reset(io);
            esp_gmf_io_set_uri(io, file_path);
            esp_gmf_io_open(io);
            ESP_LOGI(TAG, "Play file: %s", file_path);
        } else {
            *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
        }
    } else {
        *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
    }
    return ESP_GMF_ERR_OK;
}

static int setup_peripheral(void **playback_handle)
{
    int ret = ESP_OK;
    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to init SD card");
    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to init audio DAC");

    dev_audio_codec_handles_t *dac_dev_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&dac_dev_handle);
    ESP_GMF_NULL_CHECK(TAG, dac_dev_handle, return ESP_GMF_ERR_NOT_FOUND);
    esp_codec_dev_handle_t playback = dac_dev_handle->codec_dev;
    ESP_GMF_NULL_CHECK(TAG, playback, return ESP_GMF_ERR_NOT_FOUND);
    ret = esp_codec_dev_set_out_vol(playback, PLAYBACK_VOLUME);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to set output volume");
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = CONFIG_GMF_AUDIO_EFFECT_RATE_CVT_DEST_RATE,
        .channel = CONFIG_GMF_AUDIO_EFFECT_CH_CVT_DEST_CH,
        .bits_per_sample = CONFIG_GMF_AUDIO_EFFECT_BIT_CVT_DEST_BITS,
    };
    ret = esp_codec_dev_open(playback, &fs);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to open playback codec");

    *playback_handle = playback;
    return ESP_OK;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_WARN);
    esp_log_level_set(TAG, ESP_LOG_INFO);
    int ret;

    ESP_LOGI(TAG, "[ 1 ] Mount sdcard and setup audio codec");
    void *playback_handle = NULL;
    setup_peripheral(&playback_handle);

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
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to new pipeline");

    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(pipe), playback_handle);

    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    esp_gmf_info_sound_t info = {0};
    esp_gmf_audio_helper_get_audio_type_by_uri(play_urls[0], &info.format_id);
    esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);
    esp_gmf_pipeline_set_in_uri(pipe, play_urls[0]);

    ESP_LOGI(TAG, "[ 3.1 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task");
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.stack_in_ext = true;
    cfg.name = "pipe_dec";
    esp_gmf_task_handle_t work_task = NULL;
    ret = esp_gmf_task_init(&cfg, &work_task);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to create pipeline task");

    // Set strategy function for loop playback without gap
    pipeline_strategy_ctx_t strategy_ctx = {
        .stop_loop = false,
        .play_index = 0,
        .file_count = sizeof(play_urls) / sizeof(char *),
        .file_path = play_urls,
        .io = pipe->in,
    };
    esp_gmf_task_set_strategy_func(work_task, pipeline_strategy_finish_func, &strategy_ctx);

    esp_gmf_pipeline_bind_task(pipe, work_task);
    esp_gmf_pipeline_loading_jobs(pipe);

    ESP_LOGI(TAG, "[ 3.2 ] Create event group and listen events from pipeline");
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);
    esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt);

    ESP_LOGI(TAG, "[ 4 ] Start audio_pipeline");
    esp_gmf_pipeline_run(pipe);

    ESP_LOGI(TAG, "[ 4.1 ] Playing %dms before change strategy", PLAYBACK_DURATION_MS);
    vTaskDelay(PLAYBACK_DURATION_MS / portTICK_PERIOD_MS);
    _pipeline_set_finish_stop_strategy(&strategy_ctx);

    // Waiting for finished or error occurred (use finished to stop strategy)
    ESP_LOGI(TAG, "[ 5 ] Wait stop event to the pipeline and stop all the pipeline");
    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    esp_gmf_pipeline_stop(pipe);

    ESP_LOGI(TAG, "[ 6 ] Destroy all the resources");
    vEventGroupDelete(pipe_sync_evt);
    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    gmf_loader_teardown_audio_effects_default(pool);
    gmf_loader_teardown_audio_codec_default(pool);
    gmf_loader_teardown_io_default(pool);
    esp_gmf_pool_deinit(pool);
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
}
