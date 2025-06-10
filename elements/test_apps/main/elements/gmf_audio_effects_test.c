/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "unity.h"
#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_port.h"
#include "esp_gmf_obj.h"
#include "esp_gmf_data_bus.h"
#include "esp_gmf_new_databus.h"

#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_fade.h"
#include "esp_gmf_mixer.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_sonic.h"
#include "esp_gmf_interleave.h"
#include "esp_gmf_deinterleave.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_audio_dec.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_enc_reg.h"
#include "esp_audio_dec_reg.h"

#include "esp_gmf_app_setup_peripheral.h"
#include "esp_audio_enc_default.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_dec_reg.h"
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_method.h"
#include "esp_gmf_audio_param.h"
#include "gmf_audio_play_com.h"

#ifdef MEDIA_LIB_MEM_TEST
#include "media_lib_adapter.h"
#include "media_lib_mem_trace.h"
#endif  /* MEDIA_LIB_MEM_TEST */

static const char *TAG = "AUDIO_EFFECTS_ELEMENT_TEST";

#define PIPELINE_BLOCK_BIT     BIT(0)
#define PIPELINE_BLOCK_RUN_BIT BIT(1)

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

esp_err_t _pipeline_event2(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGW(TAG, "CB: RECV Pipeline2 EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
    }
    return 0;
}

esp_err_t _pipeline_event3(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGW(TAG, "CB: RECV Pipeline3 EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
    }
    if (event->sub == ESP_GMF_EVENT_STATE_RUNNING) {
        if (ctx) {
            xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_RUN_BIT);
        }
    }
    return 0;
}

esp_err_t _pipeline_event4(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGW(TAG, "CB: RECV Pipeline4 EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
    }
    return 0;
}

TEST_CASE("Audio Effects Play, [io_file->aud_dec->aud_rate_cvt->aud_ch_cvt->aud_bit_cvt->aud_sonic->aud_alc->aud_eq->aud_fade->io_codec_dev]", "[ESP_GMF_Effects]")
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

    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt", "aud_sonic", "aud_alc", "aud_eq", "aud_fade"};
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

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, "/sdcard/test.mp3"));
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_element_handle_t sonic_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe, "aud_sonic", &sonic_el));
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);
    esp_gmf_sonic_set_speed(sonic_el, 1.5f);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));

    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_pause(pipe));
    vTaskDelay(1000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_resume(pipe));
    esp_gmf_sonic_set_pitch(sonic_el, 1.5f);
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

/***
// Test deinterleave with two pipeline
                                                   +-----+
                                          +- RB1 ->+ alc +-> RB3 -+
+-------------------------------------+   |        +-----+        |  +-----------------------+
|     Pipe: file-->dec-->deinterleave |-- +                       +--| interleave-->rate_cvt |
+---------------------+-------------- +   |        +-----+        |  +-----------------------+
                                          +- RB2 ->+ alc +-> RB4 -+
                                                   +-----+
***/
TEST_CASE("Audio Effects Data Weaver test", "[ESP_GMF_Effects]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_setup_codec_dev(NULL);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
    EventGroupHandle_t pipe_sync_evt1 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt1, return);
    EventGroupHandle_t pipe_sync_evt2 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt2, return);
    EventGroupHandle_t pipe_sync_evt3 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt3, return);
    EventGroupHandle_t pipe_sync_evt4 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt4, return);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);

    gmf_register_audio_all(pool);

    ESP_GMF_POOL_SHOW_ITEMS(pool);
    esp_gmf_pipeline_handle_t pipe1 = NULL;
    const char *name1[] = {"aud_dec", "aud_deintlv"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name1, sizeof(name1) / sizeof(char *), NULL, &pipe1));
    TEST_ASSERT_NOT_NULL(pipe1);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe1, "/sdcard/test.mp3"));
    esp_gmf_element_handle_t dec_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe1, "aud_dec", &dec_el));
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);

    esp_gmf_pipeline_handle_t pipe2 = NULL;
    const char *name2[] = {"aud_alc"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name2, sizeof(name2) / sizeof(char *), NULL, &pipe2));
    TEST_ASSERT_NOT_NULL(pipe2);

    esp_gmf_pipeline_handle_t pipe3 = NULL;
    const char *name3[] = {"aud_alc"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name3, sizeof(name3) / sizeof(char *), NULL, &pipe3));
    TEST_ASSERT_NOT_NULL(pipe3);

    esp_gmf_pipeline_handle_t pipe4 = NULL;
    const char *name4[] = {"aud_intlv", "aud_rate_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name4, sizeof(name4) / sizeof(char *), "io_codec_dev", &pipe4));
    TEST_ASSERT_NOT_NULL(pipe4);

    gmf_setup_pipeline_out_dev(pipe4);
    // create rb
    esp_gmf_db_handle_t db1 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db1));
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db1,
                                                               ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db1,
                                                             ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe1, "aud_deintlv", out_port, pipe2, "aud_alc", in_port));
    esp_gmf_db_handle_t db2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db2));
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db2,
                                         ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db2,
                                       ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe1, "aud_deintlv", out_port, pipe3, "aud_alc", in_port));
    esp_gmf_db_handle_t db3 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db3));
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db3,
                                         ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db3,
                                       ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe2, "aud_alc", out_port, pipe4, "aud_intlv", in_port));
    esp_gmf_db_handle_t db4 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db4));
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db4,
                                         ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db4,
                                       ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe3, "aud_alc", out_port, pipe4, "aud_intlv", in_port));

    esp_gmf_task_cfg_t cfg1 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg1.ctx = NULL;
    cfg1.cb = NULL;
    cfg1.thread.core = 0;
    cfg1.thread.prio = 3;
    cfg1.name = "aud_deintlv";
    esp_gmf_task_handle_t work_task1 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg1, &work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe1, work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe1, _pipeline_event, pipe_sync_evt1));

    esp_gmf_task_cfg_t cfg2 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg2.thread.core = 0;
    cfg2.thread.prio = 3;
    cfg2.name = "channel1";
    esp_gmf_task_handle_t work_task2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg2, &work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe2, work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe2, _pipeline_event2, pipe_sync_evt2));

    esp_gmf_task_cfg_t cfg3 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg3.ctx = NULL;
    cfg3.cb = NULL;
    cfg3.thread.core = 1;
    cfg3.thread.prio = 3;
    cfg3.name = "channel2";
    esp_gmf_task_handle_t work_task3 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg3, &work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe3, work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe3, _pipeline_event3, pipe_sync_evt3));

    esp_gmf_task_cfg_t cfg4 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg4.ctx = NULL;
    cfg4.cb = NULL;
    cfg4.thread.core = 0;
    cfg4.thread.prio = 3;
    cfg4.name = "aud_intlv";
    esp_gmf_task_handle_t work_task4 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg4, &work_task4));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe4, work_task4));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe4));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe4, _pipeline_event4, pipe_sync_evt4));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe4));

    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_pause(pipe4));

    vTaskDelay(1000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_resume(pipe4));

    xEventGroupWaitBits(pipe_sync_evt1, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt2, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt3, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt4, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe4));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task4));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe4));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    vEventGroupDelete(pipe_sync_evt1);
    vEventGroupDelete(pipe_sync_evt2);
    vEventGroupDelete(pipe_sync_evt3);
    vEventGroupDelete(pipe_sync_evt4);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

/***
// Test mix with two pipeline

+-------------------------------------+
|    Pipe1: file-->dec-->deinterleave |-+- RB1 -+
+---------------------+-------------- +         |
                                                |   +-------+
                                                +-- | mixer |
                                                |   +-------+
+-------------------------------------+         |
|    Pipe1: file-->dec-->deinterleave |-+- RB2 -+
+---------------------+-------------- +

***/
TEST_CASE("Audio mixer Play", "[ESP_GMF_Effects]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_POOL", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_setup_codec_dev(NULL);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
    EventGroupHandle_t pipe_sync_evt1 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt1, return);

    EventGroupHandle_t pipe_sync_evt2 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt2, return);

    EventGroupHandle_t pipe_sync_evt3 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt3, return);

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);

    ESP_GMF_POOL_SHOW_ITEMS(pool);
    esp_gmf_pipeline_handle_t pipe1 = NULL;
    const char *name1[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name1, sizeof(name1) / sizeof(char *), NULL, &pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe1, "/sdcard/test1.mp3"));
    esp_gmf_element_handle_t dec_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe1, "aud_dec", &dec_el));
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);

    esp_gmf_pipeline_handle_t pipe2 = NULL;
    const char *name2[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name2, sizeof(name2) / sizeof(char *), NULL, &pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe2, "/sdcard/test.mp3"));
    dec_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe2, "aud_dec", &dec_el));
    esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);

    esp_gmf_pipeline_handle_t pipe3 = NULL;
    const char *name3[] = {"aud_mixer"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name3, sizeof(name3) / sizeof(char *), "io_codec_dev", &pipe3));
    gmf_setup_pipeline_out_dev(pipe3);
    // create rb
    esp_gmf_db_handle_t db = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db));
    esp_gmf_db_handle_t db2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db2));

    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db,
                                                               ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db,
                                                             ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe1, "aud_bit_cvt", out_port, pipe3, "aud_mixer", in_port));

    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db2,
                                         ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db2,
                                       ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe2, "aud_bit_cvt", out_port, pipe3, "aud_mixer", in_port));

    esp_gmf_task_cfg_t cfg1 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg1.ctx = NULL;
    cfg1.cb = NULL;
    cfg1.thread.core = 1;
    cfg1.thread.prio = 10;
    cfg1.name = "stream1";
    esp_gmf_task_handle_t work_task1 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg1, &work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe1, work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe1, _pipeline_event, pipe_sync_evt1));

    esp_gmf_task_cfg_t cfg2 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg2.thread.core = 0;
    cfg2.thread.prio = 10;
    cfg2.name = "stream2";
    esp_gmf_task_handle_t work_task2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg2, &work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe2, work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe2, _pipeline_event2, pipe_sync_evt2));

    esp_gmf_task_cfg_t cfg3 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg3.ctx = NULL;
    cfg3.cb = NULL;
    cfg3.thread.core = 0;
    cfg3.thread.prio = 5;
    cfg3.name = "mix_process";
    esp_gmf_task_handle_t work_task3 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg3, &work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe3, work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe3, _pipeline_event3, pipe_sync_evt3));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe3));
    esp_gmf_element_handle_t mixer_hd = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe3, "aud_mixer", &mixer_hd));
    xEventGroupWaitBits(pipe_sync_evt3, PIPELINE_BLOCK_RUN_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    // set mode
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_mixer_set_mode(mixer_hd, 0, ESP_AE_MIXER_MODE_FADE_UPWARD));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_mixer_set_mode(mixer_hd, 1, ESP_AE_MIXER_MODE_FADE_DOWNWARD));

    xEventGroupWaitBits(pipe_sync_evt1, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt2, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe1));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe3));
    gmf_unregister_audio_all(pool);
    esp_gmf_pool_deinit(pool);
    vEventGroupDelete(pipe_sync_evt1);
    vEventGroupDelete(pipe_sync_evt2);
    vEventGroupDelete(pipe_sync_evt3);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}
