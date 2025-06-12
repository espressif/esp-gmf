/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "unity.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_err.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_thread.h"

#include "esp_gmf_audio_dec.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_dec_reg.h"
#include "esp_gmf_new_databus.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"
#include "esp_gmf_audio_helper.h"
#include "gmf_audio_play_com.h"
#include "esp_gmf_io_http.h"

#ifdef MEDIA_LIB_MEM_TEST
#include "media_lib_adapter.h"
#include "media_lib_mem_trace.h"
#endif  /* MEDIA_LIB_MEM_TEST */

#define PIPELINE_BLOCK_BIT  BIT(0)
#define PIPELINE_BLOCK_BIT2 BIT(1)
#define PIPELINE_BLOCK_BIT3 BIT(2)

static const char *TAG = "AUDIO_PLAY_ELEMENT_TEST";

static const char *file_name  = "/sdcard/test.mp3";
static const char *file_name1 = "/sdcard/test_2.wav";

static const char *wav_file_path[] = {
    "/sdcard/test_1.wav",
    "/sdcard/test_2.wav",
    "/sdcard/test_3.wav",
};

static const char *dec_file_path[] = {
    // Two same ts file to test the reset function
    "/sdcard/test.ts",
    "/sdcard/test.ts",
    "/sdcard/test.aac",
    "/sdcard/test.mp3",
    "/sdcard/test.amr",
    "/sdcard/test.flac",
    "/sdcard/test.wav",
    "/sdcard/test.m4a",
};

#define ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT (4096)

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

static esp_err_t _pipeline_event2(esp_gmf_event_pkt_t *event, void *ctx)
{
    // The warning messages are used to make the content more noticeable.
    ESP_LOGW(TAG, "CB: RECV Pipeline 2 EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        if (ctx) {
            xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT2);
        }
    }
    return 0;
}

static esp_err_t _pipeline_event3(esp_gmf_event_pkt_t *event, void *ctx)
{
    // The warning messages are used to make the content more noticeable.
    ESP_LOGW(TAG, "CB: RECV Pipeline 3 EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        if (ctx) {
            xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT3);
        }
    }
    return 0;
}

TEST_CASE("Create and destroy pipeline", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_setup_codec_dev(NULL);
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);
    // Create the new pipeline
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_rate_cvt"};
    esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    esp_gmf_pipeline_destroy(pipe);
    gmf_unregister_audio_all(pool);
    esp_gmf_pool_deinit(pool);
    esp_gmf_app_teardown_codec_dev();
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio Play, ENC and DEC Loop TEST, [FILE->enc->dec->IIS]", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_POOL", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_audio_type_t type_list[] = {ESP_AUDIO_TYPE_AAC, ESP_AUDIO_TYPE_G711A, ESP_AUDIO_TYPE_G711U,
                                    ESP_AUDIO_TYPE_OPUS, ESP_AUDIO_TYPE_ADPCM, ESP_AUDIO_TYPE_PCM,
                                    ESP_AUDIO_TYPE_SBC, ESP_AUDIO_TYPE_LC3};
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
    ESP_GMF_MEM_SHOW(TAG);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_enc", "aud_dec", "aud_rate_cvt", "aud_ch_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe));
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_out_dev(pipe);
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.stack = 50 * 1024;
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_init(&cfg, &work_task);
    TEST_ASSERT_NOT_NULL(work_task);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));
    ESP_GMF_MEM_SHOW(TAG);
    for (int i = 0; i < sizeof(type_list) / sizeof(esp_audio_type_t); ++i) {
        play_loop_single_file(pipe, type_list[i]);
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
    vTaskDelay(1000 / portTICK_RATE_MS);
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio Play, One Pipe, [FILE->dec->resample->IIS]", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_ELEMENT", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_IO", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_setup_codec_dev(NULL);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_rate_cvt"};
    esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_out_dev(pipe);
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, file_name));
    ESP_GMF_MEM_SHOW(TAG);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));

    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_pause(pipe));
    vTaskDelay(1000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_resume(pipe));

    // Wait to finished or got error
    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    vEventGroupDelete(pipe_sync_evt);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio Play, multiple file with One Pipe, [FILE->dec->resample->IIS]", "[ESP_GMF_POOL]")
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

    ESP_GMF_MEM_SHOW(TAG);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe));
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_out_dev(pipe);
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_init(&cfg, &work_task);
    TEST_ASSERT_NOT_NULL(work_task);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));
    ESP_GMF_MEM_SHOW(TAG);
    for (int i = 0; i < sizeof(dec_file_path) / sizeof(char *); ++i) {
        play_pause_single_file(pipe, dec_file_path[i]);
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
    vTaskDelay(1000 / portTICK_RATE_MS);
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio Play, two in pipe use same task, [file->dec]->rb->[resample+IIS]", "[ESP_GMF_POOL]")
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
    ESP_GMF_MEM_SHOW(TAG);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    esp_gmf_pipeline_handle_t pipe_in1, pipe_in2;
    const char *name[] = {"aud_dec"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), NULL, &pipe_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), NULL, &pipe_in2));
    TEST_ASSERT_NOT_NULL(pipe_in1);
    TEST_ASSERT_NOT_NULL(pipe_in2);

    const char *name2[] = {"aud_rate_cvt"};
    esp_gmf_pipeline_handle_t pipe_out = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name2, sizeof(name2) / sizeof(char *), "io_codec_dev", &pipe_out));
    TEST_ASSERT_NOT_NULL(pipe_out);
    gmf_setup_pipeline_out_dev(pipe_out);
    esp_gmf_db_handle_t db = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(20, 1024, &db));
    esp_gmf_port_handle_t out_port1 = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, NULL, db,
                                                                ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, portMAX_DELAY);

    esp_gmf_port_handle_t out_port2 = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, NULL, db,
                                                                ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, portMAX_DELAY);

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, NULL, db,
                                                             ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, portMAX_DELAY);
    esp_gmf_pipeline_connect_pipe(pipe_in1, "aud_dec", out_port1, pipe_out, "aud_rate_cvt", in_port);

    ESP_LOGW(TAG, "Starting play the FIRST file.");
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.name = "aud_dec";
    esp_gmf_task_handle_t work_task_in1 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_in1, work_task_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_in1, _pipeline_event, pipe_sync_evt));

    cfg.name = "resample";
    cfg.thread.core = 1;
    cfg.thread.prio = 6;
    esp_gmf_task_handle_t work_task2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_out, work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_out, _pipeline_event2, pipe_sync_evt));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe_in1, (char *)wav_file_path[0]));

    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_in1, "aud_dec", &dec_el);
    uint32_t type = ESP_AUDIO_TYPE_UNSUPPORT;
    esp_gmf_audio_helper_get_audio_type_by_uri(wav_file_path[0], &type);
    esp_gmf_info_sound_t info = {
        .format_id = type,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_in1));
    ESP_GMF_MEM_SHOW(TAG);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_out));

    ESP_LOGW(TAG, "Waiting for the FIRST file to finish...");
    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT2, pdTRUE, pdFALSE, portMAX_DELAY);

    ESP_LOGW(TAG, "Starting play the SECOND file.");
    esp_gmf_audio_element_handle_t el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_in2, "aud_dec", &el);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(el, out_port2));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_reset(work_task_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_in2, work_task_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_in2, _pipeline_event, pipe_sync_evt));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe_in2, (char *)wav_file_path[1]));
    dec_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_in2, "aud_dec", &dec_el);
    esp_gmf_audio_helper_get_audio_type_by_uri(wav_file_path[1], &info.format_id);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_in2));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reg_event_recipient(pipe_in2, pipe_out));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_reset(db));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe_out));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_out));

    ESP_GMF_MEM_SHOW(TAG);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_in2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_out));

    ESP_LOGW(TAG, "Waiting for the SECOND file to finish...");
    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT2, pdTRUE, pdFALSE, portMAX_DELAY);
    ESP_LOGE(TAG, "It's finished,%s-%d", __func__, __LINE__);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_in2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_out));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_out));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_in2));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_deinit(db));
    vEventGroupDelete(pipe_sync_evt);
    ESP_GMF_MEM_SHOW(TAG);
    vTaskDelay(1000 / portTICK_RATE_MS);
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio Play, One Pipe, [HTTP->dec->resample->IIS]", "[ESP_GMF_POOL][leaks=10000]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    esp_gmf_app_setup_codec_dev(NULL);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#endif  /* MEDIA_LIB_MEM_TEST */
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_pool_handle_t pool = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_init(&pool));
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_MEM_SHOW(TAG);
    ESP_GMF_POOL_SHOW_ITEMS(pool);
    esp_gmf_info_sound_t info = {0};

    ESP_LOGI(TAG, "---- Test 1 for HTTP pipeline reset playing ----");
    // Create the new elements
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *uri = "https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3";
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_http", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe));
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_out_dev(pipe);
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    TEST_ASSERT_NOT_NULL(pipe);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));

    ESP_GMF_MEM_SHOW(TAG);

    for (int i = 0; i < 3; ++i) {
        printf("\r\nIndex:%d\r\n", i);
        ESP_GMF_MEM_SHOW(TAG);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));

        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, uri));
        esp_gmf_element_handle_t dec_el = NULL;
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el));
        esp_gmf_audio_helper_get_audio_type_by_uri(uri, &info.format_id);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));

        ESP_GMF_MEM_SHOW(TAG);
        esp_gmf_task_set_timeout(pipe->thread, 5000);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
        esp_gmf_pipeline_list_el(pipe);
        vTaskDelay(2000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_pause(pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_resume(pipe));

        // Wait to finished or got error
        xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));

        esp_gmf_pipeline_reset(pipe);
    }

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    vEventGroupDelete(pipe_sync_evt);
    ESP_GMF_MEM_SHOW(TAG);

    ESP_LOGI(TAG, "---- Test 2 for create destroy HTTP pipeline multiple times ----");

    for (int i = 0; i < 3; ++i) {
        printf("\r\nIndex:%d\r\n", i);
        ESP_GMF_MEM_SHOW(TAG);
        EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
        ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);

        // Create the new elements
        esp_gmf_pipeline_handle_t pipe = NULL;
        const char *uri = "https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3";
        const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt"};
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_http", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe));
        TEST_ASSERT_NOT_NULL(pipe);
        gmf_setup_pipeline_out_dev(pipe);
        esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
        cfg.ctx = NULL;
        cfg.cb = NULL;
        esp_gmf_task_handle_t work_task = NULL;
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
        TEST_ASSERT_NOT_NULL(pipe);

        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, uri));
        esp_gmf_element_handle_t dec_el = NULL;
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el));
        info.format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));

        ESP_GMF_MEM_SHOW(TAG);
        esp_gmf_task_set_timeout(pipe->thread, 5000);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
        esp_gmf_pipeline_list_el(pipe);
        vTaskDelay(2000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_pause(pipe));
        vTaskDelay(1000 / portTICK_RATE_MS);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_resume(pipe));

        // Wait to finished or got error
        xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));

        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));

        vEventGroupDelete(pipe_sync_evt);
    }

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
    esp_gmf_app_teardown_sdcard(sdcard_handle);
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_codec_dev();
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    esp_gmf_app_wifi_disconnect();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio Play, Two Pipe, [HTTP->dec]--RB-->[resample->IIS]", "[ESP_GMF_POOL][leaks=10000]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_POOL", ESP_LOG_DEBUG);
    esp_gmf_app_test_case_uses_tcpip();
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_wifi_connect();
    esp_gmf_app_setup_codec_dev(NULL);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);

    esp_gmf_pool_handle_t pool = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_init(&pool));
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_MEM_SHOW(TAG);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    // Create the new elements
    esp_gmf_pipeline_handle_t pipe_in = NULL;
    esp_gmf_pipeline_handle_t pipe_out = NULL;
    const char *uri = "https://dl.espressif.com/dl/audio/ff-16b-2c-16000hz.mp3";
    const char *name_in[] = {"aud_dec"};
    const char *name_out[] = {"aud_rate_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_http", name_in, sizeof(name_in) / sizeof(char *), NULL, &pipe_in));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name_out, sizeof(name_out) / sizeof(char *), "io_codec_dev", &pipe_out));
    TEST_ASSERT_NOT_NULL(pipe_in);
    TEST_ASSERT_NOT_NULL(pipe_out);
    gmf_setup_pipeline_out_dev(pipe_out);
    esp_gmf_db_handle_t db = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(15, 1024, &db));
    TEST_ASSERT_NOT_NULL(db);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db,
                                                               ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, portMAX_DELAY);
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, NULL, db,
                                                             ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, portMAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe_in, "aud_dec", out_port, pipe_out, "aud_rate_cvt", in_port));

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.name = "DECODER";
    esp_gmf_task_handle_t work_task_in = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task_in));
    TEST_ASSERT_NOT_NULL(work_task_in);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_in, work_task_in));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_in));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_in, _pipeline_event, pipe_sync_evt));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe_in, uri));
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_in, "aud_dec", &dec_el);
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));

    esp_gmf_task_handle_t work_task_out = NULL;
    cfg.name = "RES_IIS";
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task_out));
    TEST_ASSERT_NOT_NULL(work_task_out);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_out, work_task_out));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_out));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_out, _pipeline_event2, pipe_sync_evt));

    ESP_GMF_MEM_SHOW(TAG);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_in));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_out));

    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_pause(pipe_in));
    vTaskDelay(1000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_resume(pipe_in));

    vTaskDelay(10000 / portTICK_RATE_MS);
    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    esp_gmf_db_done_write(db);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_in));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_out));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task_in));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task_out));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_in));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_out));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    vEventGroupDelete(pipe_sync_evt);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_codec_dev();
    esp_gmf_app_wifi_disconnect();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

/***
// No gap loop playing case
// The pipeline diagram as follow

+----------------------+
| PipeIn1: File+decoder +---+
+----------------------+   |        +--------------------------+
                           +-- RB --> Pipe out: Resample + IIS |
+----------------------+   |        +--------------------------+
| PipeIn2: File+decoder +---+
+----------------------+

***/
esp_gmf_pipeline_handle_t pipe_in1, pipe_in2;
esp_err_t _loop_play_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGW(TAG, "CB:Loop Play, Pipeline EVT: %s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             OBJ_GET_TAG(event->from), event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if (event->sub == ESP_GMF_EVENT_STATE_FINISHED) {
        if (event->from == pipe_in1) {
            xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
        }  else if (event->from == pipe_in2) {
            xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT2);
        }
    }
    return 0;
}

TEST_CASE("Audio Play, loop with no gap, [file->dec]->rb->[resample+IIS]", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_setup_codec_dev(NULL);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);
    esp_gmf_port_handle_t out_port1, out_port2;
    esp_gmf_db_handle_t db = NULL;
    esp_gmf_pool_handle_t pool = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_init(&pool));
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    const char *name[] = {"aud_dec"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), NULL, &pipe_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), NULL, &pipe_in2));
    TEST_ASSERT_NOT_NULL(pipe_in1);
    TEST_ASSERT_NOT_NULL(pipe_in2);

    const char *name2[] = {"aud_rate_cvt"};
    esp_gmf_pipeline_handle_t pipe_out = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name2, sizeof(name2) / sizeof(char *), "io_codec_dev", &pipe_out));
    TEST_ASSERT_NOT_NULL(pipe_out);
    gmf_setup_pipeline_out_dev(pipe_out);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(20, 1024, &db));
    TEST_ASSERT_NOT_NULL(db);
    out_port1 = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, NULL, db,
                                          ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, portMAX_DELAY);
    out_port2 = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, NULL, db,
                                          ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, portMAX_DELAY);

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, NULL, db,
                                                             ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, portMAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe_in1, "aud_dec", out_port1, pipe_out, "aud_rate_cvt", in_port));

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.name = "aud_dec";
    esp_gmf_task_handle_t work_task_in1 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task_in1));
    TEST_ASSERT_NOT_NULL(work_task_in1);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_in1, work_task_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_in1, _loop_play_event, pipe_sync_evt));

    esp_gmf_task_handle_t work_task_in2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task_in2));
    TEST_ASSERT_NOT_NULL(work_task_in2);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_in2, work_task_in2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_in2, _loop_play_event, pipe_sync_evt));

    esp_gmf_audio_element_handle_t el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe_in2, "aud_dec", &el));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(el, out_port2));

    cfg.name = "res_iis";
    cfg.thread.core = 1;
    cfg.thread.prio = 1;
    esp_gmf_task_handle_t work_task2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task2));
    TEST_ASSERT_NOT_NULL(work_task2);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_out, work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_out, _pipeline_event2, NULL));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe_in1, file_name));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe_in2, file_name1));

    esp_gmf_element_handle_t dec_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe_in1, "aud_dec", &dec_el));
    esp_gmf_info_sound_t info = {0};
    esp_gmf_audio_helper_get_audio_type_by_uri(file_name, &info.format_id);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));

    dec_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe_in2, "aud_dec", &dec_el));
    esp_gmf_audio_helper_get_audio_type_by_uri(file_name1, &info.format_id);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_in1));
    ESP_GMF_MEM_SHOW(TAG);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_in1));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_out));
    ESP_GMF_MEM_SHOW(TAG);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_out));

    int loop_play_times = 0;
    while (1) {
        // Wait for pipeline auto stopped event after done then auto start next input pipeline (pipe_in2/pipe_in1)
        // Loop 2 cycles and quit
        int bits = xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT | PIPELINE_BLOCK_BIT2,
                                       pdTRUE, pdFALSE, portMAX_DELAY);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_reset_done_write(db));
        if (bits & PIPELINE_BLOCK_BIT) {
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_port_clean_payload_done(out_port1));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe_in2));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_in2));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_in2));
        }
        if (bits & PIPELINE_BLOCK_BIT2) {
            loop_play_times++;
            if (loop_play_times >= 2) {
                break;
            }
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_port_clean_payload_done(out_port2));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe_in1));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_in1));
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_in1));
        }
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_in2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_out));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task_in2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_out));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_in1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_in2));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_deinit(db));
    vEventGroupDelete(pipe_sync_evt);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

/***
// Test copier with two pipeline

+-------------------------------------------------------+
|     Pipe: file-->dec-->copier-->resample-->i2s        |
+---------------------+---------------------------------+
                      |
                      |       +-------------------------+
                      +- RB ->+ Pipe2: resample-->file  |
                              +-------------------------+
***/
TEST_CASE("Copier, 2 pipeline test, One pipeline play file to I2S, another save to file", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    // esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_POOL", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_setup_codec_dev(NULL);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);

    esp_gmf_pool_handle_t pool = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_init(&pool));
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "copier", "aud_rate_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe));
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_out_dev(pipe);
    ESP_LOGE(TAG, "%d, Copier, 2 pipeline test", __LINE__);
    esp_gmf_pipeline_handle_t pipe2 = NULL;
    const char *name2[] = {"aud_rate_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name2, sizeof(name2) / sizeof(char *), "io_file", &pipe2));
    TEST_ASSERT_NOT_NULL(pipe2);

    esp_gmf_db_handle_t db = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(20, 1024, &db));
    TEST_ASSERT_NOT_NULL(db);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db,
                                                               ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 100);
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db,
                                                             ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 100);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe, "copier", out_port, pipe2, "aud_rate_cvt", in_port));

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.core = 0;
    cfg.thread.prio = 6;
    cfg.name = "P_file";
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    TEST_ASSERT_NOT_NULL(work_task);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));

    cfg.name = "S_file";
    cfg.thread.core = 1;
    cfg.thread.prio = 6;
    esp_gmf_task_handle_t work_task2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task2));
    TEST_ASSERT_NOT_NULL(work_task2);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe2, work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe2, _pipeline_event2, NULL));
    ESP_LOGE(TAG, "%d, Copier, 2 pipeline test", __LINE__);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, file_name));

    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_info_sound_t info = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el));
    esp_gmf_audio_helper_get_audio_type_by_uri(file_name, &info.format_id);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_out_uri(pipe2, "/sdcard/test_save1.wav"));

    ESP_GMF_MEM_SHOW(TAG);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe2));

    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe2));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    vEventGroupDelete(pipe_sync_evt);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

/***
// 3 Pipeline testing
// The copier via ringbuffer connect to two pipelines

                                                 +------------------------+
                                       +-- RB1 --> Pipe2: Resample + FILE  |
+------------------------------+       |         +------------------------+
| Pipe: File + decoder + Copier +-------+
+------------------------------+       |         +------------------------+
                                       +-- RB2 -->  Pipe3: Resample + IIS |
                                                 +------------------------+
***/
TEST_CASE("Copier, 3 pipeline test, One pipeline decoding file, one is play to I2S, last one save to file", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    // esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_POOL", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_setup_codec_dev(NULL);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);

    esp_gmf_pool_handle_t pool = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_init(&pool));
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "copier"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), NULL, &pipe));
    TEST_ASSERT_NOT_NULL(pipe);

    esp_gmf_pipeline_handle_t pipe2 = NULL;
    const char *name2[] = {"aud_rate_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name2, sizeof(name2) / sizeof(char *), "io_codec_dev", &pipe2));
    TEST_ASSERT_NOT_NULL(pipe2);
    gmf_setup_pipeline_out_dev(pipe2);
    esp_gmf_pipeline_handle_t pipe3 = NULL;
    const char *name3[] = {"aud_rate_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name3, sizeof(name3) / sizeof(char *), "io_file", &pipe3));
    TEST_ASSERT_NOT_NULL(pipe3);
    esp_gmf_element_handle_t rate_hd = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe2, "aud_rate_cvt", &rate_hd);

    esp_gmf_db_handle_t db = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db));
    esp_gmf_db_handle_t db2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db2));
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db,
                                                               ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 10);
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db,
                                                             ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 10);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe, "copier", out_port, pipe2, "aud_rate_cvt", in_port));

    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db2,
                                         ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db2,
                                       ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe, "copier", out_port, pipe3, "aud_rate_cvt", in_port));

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.name = "P_file";
    cfg.thread.core = 0;
    cfg.thread.prio = 8;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    TEST_ASSERT_NOT_NULL(work_task);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, file_name));
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_info_sound_t info = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el));
    esp_gmf_audio_helper_get_audio_type_by_uri(file_name, &info.format_id);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));

    cfg.name = "IIS";
    cfg.thread.core = 0;
    cfg.thread.prio = 6;
    cfg.thread.stack = 8192;
    esp_gmf_task_handle_t work_task2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task2));
    TEST_ASSERT_NOT_NULL(work_task2);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe2, work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe2, _pipeline_event2, NULL));

    cfg.name = "O_file";
    cfg.thread.core = 1;
    cfg.thread.prio = 3;
    esp_gmf_task_handle_t work_task3 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task3));
    TEST_ASSERT_NOT_NULL(work_task3);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe3, work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe3, _pipeline_event3, pipe_sync_evt));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_out_uri(pipe3, "/sdcard/test_copier2.wav"));

    ESP_GMF_MEM_SHOW(TAG);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
    ESP_GMF_MEM_SHOW(TAG);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe2));
    ESP_GMF_MEM_SHOW(TAG);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe3));
    ESP_GMF_MEM_SHOW(TAG);

    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT3, pdTRUE, pdFALSE, portMAX_DELAY);
    ESP_GMF_MEM_SHOW(TAG);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));

    vTaskDelay(1000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe3));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    vEventGroupDelete(pipe_sync_evt);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}
