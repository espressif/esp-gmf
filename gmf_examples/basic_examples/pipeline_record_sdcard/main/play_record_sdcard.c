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
#include "esp_pm.h"
#include "soc/rtc.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_amrnb_enc.h"
#include "esp_amrwb_enc.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_helper.h"
#include "esp_board_manager_includes.h"
#include "esp_gmf_io_codec_dev.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_gmf_app_sys.h"

static const char *TAG = "REC_SDCARD";

#define DEFAULT_RECORD_SAMPLE_RATE  48000
#define DEFAULT_RECORD_CHANNEL      1
#define DEFAULT_RECORD_BITS         16
#define DEFAULT_RECORD_DURATION_MS  10000
#define DEFAULT_MICROPHONE_GAIN     32
#define DEFAULT_RECORD_BITRATE      90000
#define DEFAULT_RECORD_OUTPUT_URL   "/sdcard/esp_gmf_rec001.aac"

#if CONFIG_PM_ENABLE
#if CONFIG_IDF_TARGET_ESP32P4
#if CONFIG_ESP32P4_REV_MIN_300
#define DEFAULT_CPU_FREQ_MHZ  100
#else
#define DEFAULT_CPU_FREQ_MHZ  90
#endif  /* CONFIG_ESP32P4_REV_MIN_300 */
#else
#define DEFAULT_CPU_FREQ_MHZ  80
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
#endif  /* CONFIG_PM_ENABLE */

esp_gmf_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGI(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             OBJ_GET_TAG(event->from), event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    return ESP_GMF_ERR_OK;
}

#if CONFIG_PM_ENABLE
static void esp_enable_pm_with_freq(int min_freq, int max_freq)
{
    max_freq = max_freq < min_freq ? min_freq : max_freq;
    rtc_cpu_freq_config_t cfg = {0};
    if (!rtc_clk_cpu_freq_mhz_to_config(max_freq, &cfg)) {
        ESP_LOGW(TAG, "Cannot config for max frequency(%d MHz)", max_freq);
        return;
    }
    if (!rtc_clk_cpu_freq_mhz_to_config(min_freq, &cfg)) {
        ESP_LOGW(TAG, "Cannot config for min frequency(%d MHz)", min_freq);
        return;
    }
    esp_pm_config_t pm_config = {
        .max_freq_mhz = max_freq,
        .min_freq_mhz = min_freq,
    };
    ESP_ERROR_CHECK(esp_pm_configure(&pm_config));
}
#endif  /* CONFIG_PM_ENABLE */

static int record_peripheral_init(esp_codec_dev_handle_t *rec_handle)
{
    int ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to init audio ADC");
    dev_audio_codec_handles_t *rec_dev_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&rec_dev_handle);
    ESP_GMF_NULL_CHECK(TAG, rec_dev_handle, return ESP_GMF_ERR_NOT_FOUND);
    esp_codec_dev_handle_t record_handle = rec_dev_handle->codec_dev;
    ESP_GMF_NULL_CHECK(TAG, record_handle, return ESP_GMF_ERR_NOT_FOUND);
    ret = esp_codec_dev_set_in_gain(record_handle, DEFAULT_MICROPHONE_GAIN);
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
    esp_gmf_app_sys_monitor_start();
#if CONFIG_PM_ENABLE
    esp_enable_pm_with_freq(DEFAULT_CPU_FREQ_MHZ, DEFAULT_CPU_FREQ_MHZ);
#endif  /* CONFIG_PM_ENABLE */
    int ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to init SD card");

    ESP_LOGI(TAG, "[ 1 ] Init record peripheral for audio codec device and sdcard");
    esp_codec_dev_handle_t record_handle = NULL;
    ret = record_peripheral_init(&record_handle);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to init record peripheral");

    ESP_LOGI(TAG, "[ 2 ] Register all the elements and set audio information to record codec device");
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    gmf_loader_setup_io_default(pool);
    gmf_loader_setup_audio_codec_default(pool);
    gmf_loader_setup_audio_effects_default(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    ESP_LOGI(TAG, "[ 3 ] Create audio pipeline");
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_enc"};
    ret = esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), "io_file", &pipe);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to new pipeline");

    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_IN_INSTANCE(pipe), record_handle);

    ESP_LOGI(TAG, "[ 3.1 ] Set audio url to record");
    esp_gmf_pipeline_set_out_uri(pipe, DEFAULT_RECORD_OUTPUT_URL);

    ESP_LOGI(TAG, "[ 3.2 ] Reconfig audio encoder type by url and audio information and report information to the record pipeline");
    esp_gmf_element_handle_t enc_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_enc", &enc_el);
    esp_gmf_info_sound_t info = {
        .sample_rates = DEFAULT_RECORD_SAMPLE_RATE,
        .channels = DEFAULT_RECORD_CHANNEL,
        .bits = DEFAULT_RECORD_BITS,
        .bitrate = DEFAULT_RECORD_BITRATE,
    };
    esp_gmf_audio_helper_get_audio_type_by_uri(DEFAULT_RECORD_OUTPUT_URL, &info.format_id);
    if (info.format_id == ESP_AUDIO_TYPE_AMRWB) {
        info.bitrate = ESP_AMRWB_ENC_BITRATE_MD885;
    } else if (info.format_id == ESP_AUDIO_TYPE_AMRNB) {
        info.bitrate = ESP_AMRNB_ENC_BITRATE_MR122;
    }
    esp_gmf_audio_enc_reconfig_by_sound_info(enc_el, &info);
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    ESP_LOGI(TAG, "[ 3.3 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task");
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.stack = 40 * 1024;  // If you encode to AMR, please make sure the stack is large enough
    cfg.thread.stack_in_ext = true;
    cfg.name = "gmf_rec";
    esp_gmf_task_handle_t work_task = NULL;
    ret = esp_gmf_task_init(&cfg, &work_task);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to create pipeline task");
    esp_gmf_pipeline_bind_task(pipe, work_task);
    esp_gmf_pipeline_loading_jobs(pipe);

    ESP_LOGI(TAG, "[ 3.4 ] Create envent group and listening event from pipeline");
    esp_gmf_pipeline_set_event(pipe, _pipeline_event, NULL);

    ESP_LOGI(TAG, "[ 4 ] Start audio_pipeline");
    esp_gmf_pipeline_run(pipe);

    ESP_LOGI(TAG, "[ 5 ] Wait for a while to stop record pipeline");
    vTaskDelay(DEFAULT_RECORD_DURATION_MS / portTICK_PERIOD_MS);
    esp_gmf_pipeline_stop(pipe);

    ESP_LOGI(TAG, "[ 6 ] Destroy all the resources");
    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    gmf_loader_teardown_audio_effects_default(pool);
    gmf_loader_teardown_audio_codec_default(pool);
    gmf_loader_teardown_io_default(pool);
    esp_gmf_pool_deinit(pool);
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    esp_gmf_app_sys_monitor_stop();
}
