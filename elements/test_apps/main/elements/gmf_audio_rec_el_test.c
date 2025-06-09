/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "unity.h"
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_bit_defs.h"

#include "esp_log.h"
#include "esp_err.h"
#include "driver/sdmmc_host.h"
#include "esp_http_client.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"
#include "esp_gmf_audio_helper.h"
#include "gmf_audio_play_com.h"

#ifdef MEDIA_LIB_MEM_TEST
#include "media_lib_adapter.h"
#include "media_lib_mem_trace.h"
#endif  /* MEDIA_LIB_MEM_TEST */

#define SETUP_AUDIO_SAMPLE_RATE 16000
#define SETUP_AUDIO_BITS        16
#define SETUP_AUDIO_CHANNELS    1
#define PIPELINE_BLOCK_BIT      BIT(0)

static const char *TAG = "AUDIO_REC_ELEMENT_TEST";

static const char *test_enc_format[] = {
    "aac",
    "amr",
    "awb",
    "pcm",
    "opus",
    "adpcm",
    "g711a",
    "g711u",
    "alac",
};

static const char *header_type[] = {
    "audio/aac",
    "audio/opus",
    "audio/pcm",
};

static esp_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    // The warning messages are used to make the content more noticeable.
    ESP_LOGW(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%x, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
            || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
            || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
            if (ctx) {
                xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
            }
    }
    return 0;
}

static esp_gmf_err_t _http_set_content_type_by_format(esp_http_client_handle_t http, uint32_t format)
{
    switch (format) {
        case ESP_AUDIO_TYPE_AAC:
            esp_http_client_set_header(http, "Content-Type", header_type[0]);
            break;
        case ESP_AUDIO_TYPE_OPUS:
            esp_http_client_set_header(http, "Content-Type", header_type[1]);
            break;
        case ESP_AUDIO_TYPE_PCM:
            esp_http_client_set_header(http, "Content-Type", header_type[2]);
            break;
        default:
            return ESP_GMF_ERR_FAIL;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    esp_http_client_handle_t http = (esp_http_client_handle_t)msg->http_client;
    char len_buf[16];
    if (msg->event_id == HTTP_STREAM_PRE_REQUEST) {
        // set header
        ESP_LOGW(TAG, "[ + ] HTTP client HTTP_STREAM_PRE_REQUEST, length=%d, format:%s", msg->buffer_len, (char *)msg->user_data);
        esp_http_client_set_method(http, HTTP_METHOD_POST);
        char dat[10] = {0};
        snprintf(dat, sizeof(dat), "%d", SETUP_AUDIO_SAMPLE_RATE);
        esp_http_client_set_header(http, "x-audio-sample-rates", dat);
        uint32_t fmt = 0;
        esp_gmf_audio_helper_get_audio_type_by_uri((char *)msg->user_data, &fmt);
        esp_gmf_err_t ret = _http_set_content_type_by_format(http, fmt);
        if (ret != ESP_GMF_ERR_OK) {
            ESP_LOGE(TAG, "Failed to set content type for format: %lx", fmt);
            return ret;
        }
        memset(dat, 0, sizeof(dat));
        snprintf(dat, sizeof(dat), "%d", SETUP_AUDIO_BITS);
        esp_http_client_set_header(http, "x-audio-bits", dat);
        memset(dat, 0, sizeof(dat));
        snprintf(dat, sizeof(dat), "%d", SETUP_AUDIO_CHANNELS);
        esp_http_client_set_header(http, "x-audio-channel", dat);
        return ESP_GMF_ERR_OK;
    }

    if (msg->event_id == HTTP_STREAM_ON_REQUEST) {
        // write data
        int wlen = sprintf(len_buf, "%x\r\n", msg->buffer_len);
        if (esp_http_client_write(http, len_buf, wlen) <= 0) {
            return ESP_GMF_ERR_FAIL;
        }
        if (esp_http_client_write(http, msg->buffer, msg->buffer_len) <= 0) {
            return ESP_GMF_ERR_FAIL;
        }
        if (esp_http_client_write(http, "\r\n", 2) <= 0) {
            return ESP_GMF_ERR_FAIL;
        }
        return msg->buffer_len;
    }

    if (msg->event_id == HTTP_STREAM_POST_REQUEST) {
        ESP_LOGW(TAG, "[ + ] HTTP client HTTP_STREAM_POST_REQUEST, write end chunked marker");
        if (esp_http_client_write(http, "0\r\n\r\n", 5) <= 0) {
            return ESP_GMF_ERR_FAIL;
        }
        return ESP_GMF_ERR_OK;
    }

    if (msg->event_id == HTTP_STREAM_FINISH_REQUEST) {
        ESP_LOGW(TAG, "[ + ] HTTP client HTTP_STREAM_FINISH_REQUEST");
        char *buf = (char *)calloc(1, 64);
        assert(buf);
        int read_len = esp_http_client_read(http, buf, 64);
        if (read_len <= 0) {
            free(buf);
            return ESP_GMF_ERR_FAIL;
        }
        buf[read_len] = 0;
        ESP_LOGI(TAG, "Got HTTP Response = %s", (char *)buf);
        free(buf);
        return ESP_GMF_ERR_OK;
    }
    ESP_LOGD(TAG, "Unknown event: %d", msg->event_id);
    return ESP_GMF_ERR_OK;
}

TEST_CASE("Recorder, One Pipe, [IIS->ENC->FILE]", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    // esp_log_level_set("GMF_CACHE", ESP_LOG_DEBUG);
    // esp_log_level_set("ESP_GMF_AENC", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    uint32_t I2S_REC_ENCODER_SAMPLE_RATE = 48000;
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.record_info.sample_rate = SETUP_AUDIO_SAMPLE_RATE;
    codec_info.record_info.channel = SETUP_AUDIO_CHANNELS;
    codec_info.record_info.bits_per_sample = SETUP_AUDIO_BITS;
    codec_info.play_info.sample_rate = codec_info.record_info.sample_rate;
    esp_gmf_app_setup_codec_dev(&codec_info);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);

    ESP_GMF_POOL_SHOW_ITEMS(pool);
    // Create the new elements
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *uri = "/sdcard/esp_gmf_rec1.aac";

    const char *name[] = {"aud_rate_cvt", "aud_ch_cvt", "aud_enc"};
    esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), "io_file", &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_in_dev(pipe);
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_init(&cfg, &work_task);
    TEST_ASSERT_NOT_NULL(work_task);

    esp_gmf_pipeline_bind_task(pipe, work_task);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    esp_gmf_pipeline_set_event(pipe, _pipeline_event, NULL);

    esp_gmf_pipeline_set_out_uri(pipe, uri);
    esp_gmf_element_handle_t enc_handle = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_enc", &enc_handle);
    uint32_t audio_type = 0;
    esp_gmf_audio_helper_get_audio_type_by_uri(uri, &audio_type);
    esp_gmf_info_sound_t info = {
        .sample_rates = SETUP_AUDIO_SAMPLE_RATE,
        .channels = SETUP_AUDIO_CHANNELS,
        .bits = SETUP_AUDIO_BITS,
        .bitrate = 90000,
        .format_id = audio_type,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_enc_reconfig_by_sound_info(enc_handle, &info));
    esp_gmf_element_handle_t resp = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &resp);
    esp_ae_rate_cvt_cfg_t *resp_cfg = (esp_ae_rate_cvt_cfg_t *)OBJ_GET_CFG(resp);
    resp_cfg->src_rate = SETUP_AUDIO_SAMPLE_RATE;
    if (audio_type == ESP_AUDIO_TYPE_AMRNB) {
        resp_cfg->dest_rate = 8000;
    } else if (audio_type == ESP_AUDIO_TYPE_G711U) {
        resp_cfg->dest_rate = 8000;
    } else if (audio_type == ESP_AUDIO_TYPE_G711A) {
        resp_cfg->dest_rate = 8000;
    } else if (audio_type == ESP_AUDIO_TYPE_AMRWB) {
        resp_cfg->dest_rate = 16000;
    } else {
        resp_cfg->dest_rate = I2S_REC_ENCODER_SAMPLE_RATE;
    }
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));

    vTaskDelay(10000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));

    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    gmf_unregister_audio_all(pool);
    esp_gmf_pool_deinit(pool);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_codec_dev();
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Recorder, One Pipe recoding multiple format, [IIS->ENC->FILE]", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    // esp_log_level_set("ESP_GMF_RATE_CVT", ESP_LOG_DEBUG);
    // esp_log_level_set("ESP_GMF_AENC", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.record_info.sample_rate = SETUP_AUDIO_SAMPLE_RATE;
    codec_info.record_info.channel = SETUP_AUDIO_CHANNELS;
    codec_info.record_info.bits_per_sample = SETUP_AUDIO_BITS;
    codec_info.play_info.sample_rate = codec_info.record_info.sample_rate;
    esp_gmf_app_setup_codec_dev(&codec_info);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_rate_cvt", "aud_enc"};
    esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), "io_file", &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_in_dev(pipe);
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.stack = 40 * 1024;
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_init(&cfg, &work_task);
    TEST_ASSERT_NOT_NULL(work_task);

    esp_gmf_pipeline_bind_task(pipe, work_task);
    esp_gmf_pipeline_loading_jobs(pipe);
    esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt);

    char uri[128] = {0};
    uint32_t I2S_REC_ENCODER_SAMPLE_RATE = 48000;

    for (int i = 0; i < sizeof(test_enc_format) / sizeof(char *); ++i) {
        snprintf(uri, 127, "/sdcard/esp_gmf_rec_%02d.%s", i, test_enc_format[i]);
        esp_gmf_element_handle_t enc_handle = NULL;
        esp_gmf_pipeline_get_el_by_name(pipe, "aud_enc", &enc_handle);
        esp_audio_enc_config_t *esp_gmf_enc_cfg = (esp_audio_enc_config_t *)OBJ_GET_CFG(enc_handle);
        uint32_t audio_type = 0;
        esp_gmf_audio_helper_get_audio_type_by_uri(uri, &audio_type);
        esp_gmf_info_sound_t info = {
            .sample_rates = SETUP_AUDIO_SAMPLE_RATE,
            .channels = SETUP_AUDIO_CHANNELS,
            .bits = SETUP_AUDIO_BITS,
            .format_id = audio_type,
        };
        if (audio_type == ESP_AUDIO_TYPE_AMRNB) {
            info.bitrate = 12200;
        } else if (audio_type == ESP_AUDIO_TYPE_AMRWB) {
            info.bitrate = 19850;
        } else {
            info.bitrate = 90000;
        }
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_enc_reconfig_by_sound_info(enc_handle, &info));
        esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

        esp_gmf_element_handle_t resp = NULL;
        esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &resp);
        esp_ae_rate_cvt_cfg_t *resp_cfg = (esp_ae_rate_cvt_cfg_t *)OBJ_GET_CFG(resp);
        resp_cfg->src_rate = SETUP_AUDIO_SAMPLE_RATE;
        if (audio_type == ESP_AUDIO_TYPE_AMRNB) {
            resp_cfg->dest_rate = 8000;
        } else if (audio_type == ESP_AUDIO_TYPE_G711U) {
            resp_cfg->dest_rate = 8000;
        } else if (audio_type == ESP_AUDIO_TYPE_G711A) {
            resp_cfg->dest_rate = 8000;
        } else if (audio_type == ESP_AUDIO_TYPE_AMRWB) {
            resp_cfg->dest_rate = 16000;
        } else {
            resp_cfg->dest_rate = I2S_REC_ENCODER_SAMPLE_RATE;
        }

        ESP_LOGW(TAG, "Recoding file, %s, type:%d\r\n", uri, esp_gmf_enc_cfg->type);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_out_uri(pipe, uri));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
        int cnt = 10;
        xEventGroupClearBits(pipe_sync_evt, PIPELINE_BLOCK_BIT);
        while (cnt) {
            printf(".");
            EventBits_t ret = xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, 1000 / portTICK_RATE_MS);
            if ((ret & PIPELINE_BLOCK_BIT) != 0) {
                cnt = 0;
                break;
            }
            fflush(stdout);
            cnt--;
        }
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    }

    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    gmf_unregister_audio_all(pool);
    esp_gmf_pool_deinit(pool);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    vEventGroupDelete(pipe_sync_evt);
    esp_gmf_app_teardown_codec_dev();
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

// Refer the 'esp_gmf_audio_rec_el_test.c' test_enc_format
static const char *recoding_file_path[] = {
    "/sdcard/esp_gmf_rec_00.aac",
    "/sdcard/esp_gmf_rec_01.amr",
    "/sdcard/esp_gmf_rec_02.awb",
    "/sdcard/esp_gmf_rec_03.pcm",
};

TEST_CASE("Record file for playback, multiple files with One Pipe, [FILE->dec->resample->IIS]", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_POOL", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_setup_codec_dev(NULL);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe));
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_out_dev(pipe);
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.thread.stack = 30 * 1024;
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_init(&cfg, &work_task);
    TEST_ASSERT_NOT_NULL(work_task);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));

    for (int i = 0; i < sizeof(recoding_file_path) / sizeof(char *); ++i) {
        play_pause_single_file(pipe, recoding_file_path[i]);
        xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));
    }

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    vEventGroupDelete(pipe_sync_evt);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_codec_dev();
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Recorder, One Pipe, [IIS->ENC->HTTP]", "[ESP_GMF_POOL][ignore][leaks=10000]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_POOL", ESP_LOG_DEBUG);
    esp_gmf_app_test_case_uses_tcpip();
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_wifi_connect();
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.record_info.sample_rate = SETUP_AUDIO_SAMPLE_RATE;
    codec_info.record_info.channel = SETUP_AUDIO_CHANNELS;
    codec_info.record_info.bits_per_sample = SETUP_AUDIO_BITS;
    codec_info.play_info.sample_rate = codec_info.record_info.sample_rate;
    esp_gmf_app_setup_codec_dev(&codec_info);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    // Create the new elements
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_enc"};
    esp_gmf_pool_new_pipeline(pool, "io_codec_dev", name, sizeof(name) / sizeof(char *), "io_http", &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_in_dev(pipe);
    const char *rec_type = ".aac";

    esp_gmf_element_handle_t enc_handle = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_enc", &enc_handle);
    uint32_t audio_type = ESP_AUDIO_TYPE_AAC;
    esp_gmf_audio_helper_get_audio_type_by_uri(rec_type, &audio_type);
    esp_gmf_info_sound_t info = {
        .sample_rates = SETUP_AUDIO_SAMPLE_RATE,
        .channels = SETUP_AUDIO_CHANNELS,
        .bits = SETUP_AUDIO_BITS,
        .bitrate = 90000,
        .format_id = audio_type,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_enc_reconfig_by_sound_info(enc_handle, &info));
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    esp_gmf_io_handle_t http_out = NULL;
    esp_gmf_pipeline_get_out(pipe, &http_out);
    http_io_cfg_t *http_cfg = (http_io_cfg_t *)OBJ_GET_CFG(http_out);
    http_cfg->user_data = (void *)rec_type;
    http_cfg->event_handle = _http_stream_event_handle;

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_init(&cfg, &work_task);
    TEST_ASSERT_NOT_NULL(work_task);

    esp_gmf_pipeline_bind_task(pipe, work_task);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    esp_gmf_pipeline_set_event(pipe, _pipeline_event, NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_out_uri(pipe, "http://192.168.31.28:8000/upload"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));

    vTaskDelay(15000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));
    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    gmf_unregister_audio_all(pool);
    esp_gmf_pool_deinit(pool);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_codec_dev();
    esp_gmf_app_wifi_disconnect();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}
