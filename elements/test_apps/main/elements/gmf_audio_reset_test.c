/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_enc.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_dec_reg.h"
#include "esp_gmf_new_databus.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_unit_test.h"
#include "esp_gmf_audio_helper.h"
#include "gmf_audio_play_com.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_port.h"
#include "esp_gmf_audio_param.h"
#include "esp_gmf_io_file.h"
#include "esp_gmf_payload.h"
#include "esp_fourcc.h"

#define PIPELINE_BLOCK_BIT BIT(0)

/**
 * @brief  Context structure for pipeline strategy test 1 (replay test)
 */
typedef struct {
    int                  replay_count;  /*!< Total number of replays to perform */
    int                  replay_num;    /*!< Current replay iteration */
    esp_gmf_io_handle_t  io;            /*!< IO handle for this pipeline */
    SemaphoreHandle_t    reset_sem;     /*!< Semaphore to synchronize reset operation */
    esp_gmf_db_handle_t  db;            /*!< Databus handle */
} pipeline_strategy_ctx1_t;

/**
 * @brief  Context structure for pipeline strategy test 2 (file playlist test)
 */
typedef struct {
    int                  play_count;  /*!< Number of files played */
    int                  file_count;  /*!< Total number of files in the playlist */
    const char         **file_path;   /*!< Array of file paths to play */
    esp_gmf_io_handle_t  io;          /*!< IO handle for this pipeline */
    SemaphoreHandle_t    reset_sem;   /*!< Semaphore to synchronize reset operation */
} pipeline_strategy_ctx2_t;

/**
 * @brief  Context structure for pipeline strategy test 3 (encode test)
 */
typedef struct {
    int                  encode_count;  /*!< Number of times encoding has occurred */
    int                  encode_num;    /*!< Total number of encoding iterations */
    esp_gmf_io_handle_t  io_in;         /*!< Input IO handle for encoder */
    esp_gmf_io_handle_t  io_out;        /*!< Output IO handle for encoder */
    char                 url[100];      /*!< Output URL buffer */
} pipeline_strategy_ctx3_t;

static const char *TAG = "AUDIO_FINISH_RESET_TEST";

static const char *mp3_file_path[] = {
    "/sdcard/test_short.mp3",
    "/sdcard/test.mp3",
};

static const char *aac_file_path[] = {
    "/sdcard/test_short.aac",
    "/sdcard/test.aac",
};

static const char *flac_file_path[] = {
    "/sdcard/test.flac",
    "/sdcard/6_06-alwayGirl.flac",
};

static const char *amr_file_path[] = {
    "/sdcard/test.amr",
    "/sdcard/41_以父之名.amr",
};

static const char *m4a_file_path[] = {
    "/sdcard/test.m4a",
    "/sdcard/115_M4A_box_have_co64.m4a",
};

static const char *ts_file_path[] = {
    "/sdcard/test.ts",
    "/sdcard/1_2257-460-8796.ts",
};

static const char *wav_file_path[] = {
    "/sdcard/test.wav",
    "/sdcard/4_44100_2_24_2116800_15.wav",
};

static const char **file_path[] = {
    mp3_file_path,
    aac_file_path,
    flac_file_path,
    amr_file_path,
    m4a_file_path,
    ts_file_path,
    wav_file_path,
};

static const int dec_test_type[] = {
    ESP_FOURCC_MP3,
    ESP_FOURCC_AAC,
    ESP_FOURCC_FLAC,
    ESP_FOURCC_AMRNB,
    ESP_FOURCC_M4A,
    ESP_FOURCC_M2TS,
    ESP_FOURCC_WAV,
};

static const int enc_test_type[] = {
    ESP_FOURCC_AAC,
    ESP_FOURCC_ADPCM,
    ESP_FOURCC_AMRNB,
    ESP_FOURCC_AMRWB,
    ESP_FOURCC_ALAC,
    ESP_FOURCC_LC3,
    ESP_FOURCC_OPUS,
    ESP_FOURCC_SBC,
};

static const char *test_enc_format[] = {
    "aac",
    "adpcm",
    "amr",
    "awb",
    "alac",
    "lc3",
    "opus",
    "sbc",
};

static const char *mp3_http_path[] = {
    "https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3",
    "https://dl.espressif.com/dl/audio/gs-16b-1c-44100hz.mp3",
};

static esp_gmf_err_t pipeline_strategy_finish_func1(esp_gmf_task_handle_t handle, uint8_t strategy_type, void *ctx, uint8_t *out_action)
{
    pipeline_strategy_ctx1_t *strategy_ctx = (pipeline_strategy_ctx1_t *)ctx;
    esp_gmf_io_handle_t io = strategy_ctx->io;
    if (strategy_type == GMF_TASK_STRATEGY_TYPE_ABORT) {
        *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
    } else if (strategy_type == GMF_TASK_STRATEGY_TYPE_FINISH) {
        if (strategy_ctx->replay_count < strategy_ctx->replay_num) {
            // Wait for signal from effects pipeline before reset
            if (strategy_ctx->reset_sem) {
                if (xSemaphoreTake(strategy_ctx->reset_sem, portMAX_DELAY) == pdTRUE) {
                    *out_action = GMF_TASK_STRATEGY_ACTION_RESET;
                    esp_gmf_io_reset(io);
                    strategy_ctx->replay_count++;
                } else {
                    *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
                }
            } else {
                *out_action = GMF_TASK_STRATEGY_ACTION_RESET;
                esp_gmf_io_reset(io);
                strategy_ctx->replay_count++;
            }
        } else {
            *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
        }
    } else {
        *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t pipeline_strategy_finish_func2(esp_gmf_task_handle_t handle, uint8_t strategy_type, void *ctx, uint8_t *out_action)
{
    pipeline_strategy_ctx2_t *strategy_ctx = (pipeline_strategy_ctx2_t *)ctx;
    esp_gmf_io_handle_t io = strategy_ctx->io;
    if (strategy_type == GMF_TASK_STRATEGY_TYPE_ABORT) {
        *out_action = GMF_TASK_STRATEGY_ACTION_RESET;
    } else if (strategy_type == GMF_TASK_STRATEGY_TYPE_FINISH) {
        if (strategy_ctx->play_count < strategy_ctx->file_count) {
            if (strategy_ctx->reset_sem) {
                if (xSemaphoreTake(strategy_ctx->reset_sem, portMAX_DELAY) == pdTRUE) {
                    *out_action = GMF_TASK_STRATEGY_ACTION_RESET;
                    esp_gmf_io_close(io);
                    esp_gmf_io_reset(io);
                    esp_gmf_io_set_uri(io, strategy_ctx->file_path[strategy_ctx->play_count]);
                    esp_gmf_io_open(io);
                    strategy_ctx->play_count++;
                } else {
                    *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
                }
            } else {
                *out_action = GMF_TASK_STRATEGY_ACTION_RESET;
                esp_gmf_io_close(io);
                esp_gmf_io_reset(io);
                esp_gmf_io_set_uri(io, strategy_ctx->file_path[strategy_ctx->play_count]);
                esp_gmf_io_open(io);
                strategy_ctx->play_count++;
            }
        } else {
            *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
        }
    } else {
        *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t pipeline_strategy_encode_func(esp_gmf_task_handle_t handle, uint8_t strategy_type, void *ctx, uint8_t *out_action)
{
    pipeline_strategy_ctx3_t *strategy_ctx = (pipeline_strategy_ctx3_t *)ctx;
    esp_gmf_io_handle_t io_in = strategy_ctx->io_in;
    esp_gmf_io_handle_t io_out = strategy_ctx->io_out;
    if (strategy_type == GMF_TASK_STRATEGY_TYPE_ABORT) {
        *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
    } else if (strategy_type == GMF_TASK_STRATEGY_TYPE_FINISH) {
        if (strategy_ctx->encode_count < strategy_ctx->encode_num) {
            *out_action = GMF_TASK_STRATEGY_ACTION_RESET;
            esp_gmf_io_reset(io_in);
            esp_gmf_io_close(io_out);
            esp_gmf_io_reset(io_out);
            esp_gmf_io_set_uri(io_out, strategy_ctx->url);
            esp_gmf_io_open(io_out);
            strategy_ctx->encode_count++;
        } else {
            *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
        }
    } else {
        *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
    }
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t effects_pipeline_strategy_func(esp_gmf_task_handle_t handle, uint8_t strategy_type, void *ctx, uint8_t *out_action)
{
    pipeline_strategy_ctx1_t *strategy_ctx = (pipeline_strategy_ctx1_t *)ctx;
    if (strategy_type == GMF_TASK_STRATEGY_TYPE_ABORT) {
        *out_action = GMF_TASK_STRATEGY_ACTION_RESET;
        ESP_LOGE(TAG, "effects_pipeline_strategy_func: ABORT");
        if (strategy_ctx->db) {
            esp_gmf_db_reset(strategy_ctx->db);
        }
    } else if (strategy_type == GMF_TASK_STRATEGY_TYPE_FINISH) {
        if (strategy_ctx->replay_count < strategy_ctx->replay_num) {
            *out_action = GMF_TASK_STRATEGY_ACTION_RESET;
            strategy_ctx->replay_count++;
            if (strategy_ctx->db) {
                esp_gmf_db_reset(strategy_ctx->db);
            }
            if (strategy_ctx->reset_sem) {
                xSemaphoreGive(strategy_ctx->reset_sem);
            }
        } else {
            *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
        }
    } else {
        *out_action = GMF_TASK_STRATEGY_ACTION_DEFAULT;
    }
    return ESP_GMF_ERR_OK;
}

static esp_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
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

TEST_CASE("Audio File Stream Play Same URL Without Close, One pipeline", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_ELEMENT", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_IO", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = 44100;
    codec_info.play_info.channel = 2;
    codec_info.play_info.bits_per_sample = 16;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);
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
    // Create pipeline1
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_out_dev(pipe);
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_element_handle_t rate_cvt_el = NULL;
    esp_gmf_element_handle_t ch_cvt_el = NULL;
    esp_gmf_element_handle_t bit_cvt_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &rate_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_ch_cvt", &ch_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_bit_cvt", &bit_cvt_el);
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_cvt_el, 44100));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_cvt_el, 2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_cvt_el, 16));
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.prio = 9;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    pipeline_strategy_ctx1_t strategy_ctx = {
        .replay_count = 0,
        .replay_num = 5,
        .io = pipe->in,
        .reset_sem = NULL,
    };
    esp_gmf_task_set_strategy_func(work_task, pipeline_strategy_finish_func1, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, mp3_file_path[0]));
    ESP_GMF_MEM_SHOW(TAG);

    // Run pipelines
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
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

TEST_CASE("Audio File Stream Play Same URL Without Close, Two pipeline", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_ELEMENT", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_IO", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = 44100;
    codec_info.play_info.channel = 2;
    codec_info.play_info.bits_per_sample = 16;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt1 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt1, return);
    EventGroupHandle_t pipe_sync_evt2 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt2, return);
    // Create semaphore for synchronizing reset between pipelines
    SemaphoreHandle_t reset_sem = xSemaphoreCreateBinary();
    ESP_GMF_NULL_CHECK(TAG, reset_sem, return);

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);
    // Create pipeline1
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), NULL, &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_element_handle_t rate_cvt_el = NULL;
    esp_gmf_element_handle_t ch_cvt_el = NULL;
    esp_gmf_element_handle_t bit_cvt_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &rate_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_ch_cvt", &ch_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_bit_cvt", &bit_cvt_el);
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_cvt_el, 44100));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_cvt_el, 2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_cvt_el, 16));
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.prio = 9;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    pipeline_strategy_ctx1_t strategy_ctx = {
        .replay_count = 0,
        .replay_num = 5,
        .io = pipe->in,
        .reset_sem = reset_sem,
        .db = NULL,
    };
    esp_gmf_task_set_strategy_func(work_task, pipeline_strategy_finish_func1, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, mp3_file_path[0]));
    ESP_GMF_MEM_SHOW(TAG);

    // Create audio effects pipeline
    esp_gmf_pipeline_handle_t pipe_effects = NULL;
    const char *effects_name[] = {"aud_alc", "aud_sonic", "aud_eq", "aud_drc", "aud_mbc"};
    esp_gmf_pool_new_pipeline(pool, NULL, effects_name, sizeof(effects_name) / sizeof(char *), "io_codec_dev", &pipe_effects);
    TEST_ASSERT_NOT_NULL(pipe_effects);
    gmf_setup_pipeline_out_dev(pipe_effects);

    esp_gmf_element_handle_t alc_el = NULL;
    esp_gmf_element_handle_t sonic_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_effects, "aud_alc", &alc_el);
    esp_gmf_pipeline_get_el_by_name(pipe_effects, "aud_sonic", &sonic_el);
    esp_gmf_audio_param_set_alc_channel_gain(alc_el, 0xff, 10);
    esp_gmf_audio_param_set_speed(sonic_el, 1.2);
    esp_gmf_audio_param_set_pitch(sonic_el, 1.2);

    esp_gmf_task_cfg_t effects_task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    effects_task_cfg.ctx = NULL;
    effects_task_cfg.cb = NULL;
    cfg.thread.prio = 8;
    effects_task_cfg.name = "effects_task";
    esp_gmf_task_handle_t effects_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&effects_task_cfg, &effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_effects, effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_effects, _pipeline_event, pipe_sync_evt2));

    // Connect pipeline1 to effects pipeline
    esp_gmf_db_handle_t rb_handle = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &rb_handle));
    TEST_ASSERT_NOT_NULL(rb_handle);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write,
                                                               esp_gmf_db_deinit, rb_handle, 4096, ESP_GMF_MAX_DELAY);
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read,
                                                             esp_gmf_db_deinit, rb_handle, 4096, ESP_GMF_MAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe, "aud_bit_cvt", out_port, pipe_effects, "aud_alc", in_port));
    pipeline_strategy_ctx1_t strategy_ctx2 = {
        .replay_count = 0,
        .replay_num = 5,
        .io = pipe_effects->in,
        .reset_sem = reset_sem,
        .db = rb_handle,
    };
    esp_gmf_task_set_strategy_func(effects_task, effects_pipeline_strategy_func, &strategy_ctx2);
    // Run pipelines
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_effects));

    xEventGroupWaitBits(pipe_sync_evt1, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt2, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    vSemaphoreDelete(reset_sem);
    vEventGroupDelete(pipe_sync_evt1);
    vEventGroupDelete(pipe_sync_evt2);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio File Stream Play Different URL Without Close, One pipeline", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_ELEMENT", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_IO", ESP_LOG_DEBUG);

    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = 44100;
    codec_info.play_info.channel = 2;
    codec_info.play_info.bits_per_sample = 16;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);

    // Create semaphore for synchronizing reset between pipelines
    SemaphoreHandle_t reset_sem = xSemaphoreCreateBinary();
    ESP_GMF_NULL_CHECK(TAG, reset_sem, return);

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    // Create pipeline1
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_out_dev(pipe);
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_element_handle_t rate_cvt_el = NULL;
    esp_gmf_element_handle_t ch_cvt_el = NULL;
    esp_gmf_element_handle_t bit_cvt_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &rate_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_ch_cvt", &ch_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_bit_cvt", &bit_cvt_el);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_cvt_el, 44100));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_cvt_el, 2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_cvt_el, 16));
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.stack = 6 * 1024;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    static pipeline_strategy_ctx2_t strategy_ctx = {0};
    esp_gmf_task_set_strategy_func(work_task, pipeline_strategy_finish_func2, &strategy_ctx);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));
    for (int i = 0; i < sizeof(dec_test_type) / sizeof(int); i++) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, file_path[i][0]));
        strategy_ctx.play_count = 0;
        strategy_ctx.io = pipe->in;
        strategy_ctx.reset_sem = NULL;
        strategy_ctx.file_path = file_path[i];
        strategy_ctx.file_count = 2;
        esp_gmf_info_sound_t info = {
            .format_id = dec_test_type[i],
        };
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
        ESP_GMF_MEM_SHOW(TAG);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
        xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        ESP_LOGE(TAG, "do stop");
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
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio File Stream Play Different URL Without Close, Two pipeline", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_ELEMENT", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_IO", ESP_LOG_DEBUG);

    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = 44100;
    codec_info.play_info.channel = 2;
    codec_info.play_info.bits_per_sample = 16;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt1 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt1, return);
    EventGroupHandle_t pipe_sync_evt2 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt2, return);
    // Create semaphore for synchronizing reset between pipelines
    SemaphoreHandle_t reset_sem = xSemaphoreCreateBinary();
    ESP_GMF_NULL_CHECK(TAG, reset_sem, return);

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    // Create pipeline1
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), NULL, &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_element_handle_t rate_cvt_el = NULL;
    esp_gmf_element_handle_t ch_cvt_el = NULL;
    esp_gmf_element_handle_t bit_cvt_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &rate_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_ch_cvt", &ch_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_bit_cvt", &bit_cvt_el);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_cvt_el, 44100));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_cvt_el, 2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_cvt_el, 16));
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.stack = 6 * 1024;
    cfg.thread.prio = 9;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    pipeline_strategy_ctx2_t strategy_ctx = {
        .io = pipe->in,
        .reset_sem = reset_sem,
    };
    esp_gmf_task_set_strategy_func(work_task, pipeline_strategy_finish_func2, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt1));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, mp3_file_path[0]));
    ESP_GMF_MEM_SHOW(TAG);

    // Create audio effects pipeline
    esp_gmf_pipeline_handle_t pipe_effects = NULL;
    const char *effects_name[] = {"aud_alc", "aud_sonic", "aud_eq", "aud_drc", "aud_mbc"};
    esp_gmf_pool_new_pipeline(pool, NULL, effects_name, sizeof(effects_name) / sizeof(char *), "io_codec_dev", &pipe_effects);
    TEST_ASSERT_NOT_NULL(pipe_effects);
    gmf_setup_pipeline_out_dev(pipe_effects);

    esp_gmf_element_handle_t alc_el = NULL;
    esp_gmf_element_handle_t sonic_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_effects, "aud_alc", &alc_el);
    esp_gmf_pipeline_get_el_by_name(pipe_effects, "aud_sonic", &sonic_el);
    esp_gmf_audio_param_set_alc_channel_gain(alc_el, 0xff, -10);
    esp_gmf_audio_param_set_speed(sonic_el, 1.2);
    esp_gmf_audio_param_set_pitch(sonic_el, 1.2);

    esp_gmf_task_cfg_t effects_task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    effects_task_cfg.ctx = NULL;
    effects_task_cfg.cb = NULL;
    effects_task_cfg.name = "effects_task";
    cfg.thread.prio = 8;
    esp_gmf_task_handle_t effects_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&effects_task_cfg, &effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_effects, effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_effects, _pipeline_event, pipe_sync_evt2));

    // Connect pipeline1 to effects pipeline
    esp_gmf_db_handle_t rb_handle = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &rb_handle));
    TEST_ASSERT_NOT_NULL(rb_handle);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write,
                                                               esp_gmf_db_deinit, rb_handle, 4096, ESP_GMF_MAX_DELAY);
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read,
                                                             esp_gmf_db_deinit, rb_handle, 4096, ESP_GMF_MAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe, "aud_bit_cvt", out_port, pipe_effects, "aud_alc", in_port));
    pipeline_strategy_ctx1_t strategy_ctx2 = {
        .io = pipe_effects->in,
        .reset_sem = reset_sem,
        .db = rb_handle,
    };
    esp_gmf_task_set_strategy_func(effects_task, effects_pipeline_strategy_func, &strategy_ctx2);
    for (int i = 0; i < sizeof(dec_test_type) / sizeof(int); i++) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe_effects));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_effects));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, file_path[i][0]));
        strategy_ctx.play_count = 0;
        strategy_ctx.file_path = file_path[i];
        strategy_ctx.file_count = 2;
        strategy_ctx2.replay_num = strategy_ctx.file_count;
        strategy_ctx2.replay_count = 0;
        esp_gmf_info_sound_t info = {
            .format_id = dec_test_type[i],
        };
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_effects));
        xEventGroupWaitBits(pipe_sync_evt1, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
        xEventGroupWaitBits(pipe_sync_evt2, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_effects));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    vSemaphoreDelete(reset_sem);
    vEventGroupDelete(pipe_sync_evt1);
    vEventGroupDelete(pipe_sync_evt2);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio Http Stream Play Different URL Without Close, One pipeline", "[ESP_GMF_POOL][leaks=10000]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_ELEMENT", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_IO", ESP_LOG_DEBUG);

    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = 44100;
    codec_info.play_info.channel = 2;
    codec_info.play_info.bits_per_sample = 16;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);
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
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    esp_gmf_pool_new_pipeline(pool, "io_http", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    gmf_setup_pipeline_out_dev(pipe);
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_element_handle_t rate_cvt_el = NULL;
    esp_gmf_element_handle_t ch_cvt_el = NULL;
    esp_gmf_element_handle_t bit_cvt_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &rate_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_ch_cvt", &ch_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_bit_cvt", &bit_cvt_el);
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_cvt_el, 44100));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_cvt_el, 2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_cvt_el, 16));
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    pipeline_strategy_ctx2_t strategy_ctx = {
        .play_count = 0,
        .file_count = sizeof(mp3_http_path) / sizeof(char *),
        .file_path = mp3_http_path,
        .io = pipe->in,
        .reset_sem = NULL,
    };
    esp_gmf_task_set_strategy_func(work_task, pipeline_strategy_finish_func2, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, mp3_http_path[0]));
    ESP_GMF_MEM_SHOW(TAG);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
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
    esp_gmf_app_teardown_codec_dev();
    esp_gmf_app_wifi_disconnect();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio Http Stream Play Different URL Without Close, Two pipeline", "[ESP_GMF_POOL][leaks=10000]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_ELEMENT", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_IO", ESP_LOG_DEBUG);

    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_test_case_uses_tcpip();
    esp_gmf_app_wifi_connect();
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = 44100;
    codec_info.play_info.channel = 2;
    codec_info.play_info.bits_per_sample = 16;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt1 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt1, return);
    EventGroupHandle_t pipe_sync_evt2 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt2, return);
    // Create semaphore for synchronizing reset between pipelines
    SemaphoreHandle_t reset_sem = xSemaphoreCreateBinary();
    ESP_GMF_NULL_CHECK(TAG, reset_sem, return);

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    // Create pipeline1
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    esp_gmf_pool_new_pipeline(pool, "io_http", name, sizeof(name) / sizeof(char *), NULL, &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_element_handle_t rate_cvt_el = NULL;
    esp_gmf_element_handle_t ch_cvt_el = NULL;
    esp_gmf_element_handle_t bit_cvt_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &rate_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_ch_cvt", &ch_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_bit_cvt", &bit_cvt_el);
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_cvt_el, 44100));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_cvt_el, 2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_cvt_el, 16));
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    pipeline_strategy_ctx2_t strategy_ctx = {
        .play_count = 0,
        .file_count = sizeof(mp3_http_path) / sizeof(char *),
        .file_path = mp3_http_path,
        .io = pipe->in,
        .reset_sem = reset_sem,
    };
    esp_gmf_task_set_strategy_func(work_task, pipeline_strategy_finish_func2, &strategy_ctx);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt1));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, mp3_http_path[0]));
    ESP_GMF_MEM_SHOW(TAG);

    // Create audio effects pipeline
    esp_gmf_pipeline_handle_t pipe_effects = NULL;
    const char *effects_name[] = {"aud_alc", "aud_sonic", "aud_eq", "aud_drc", "aud_mbc"};
    esp_gmf_pool_new_pipeline(pool, NULL, effects_name, sizeof(effects_name) / sizeof(char *), "io_codec_dev", &pipe_effects);
    TEST_ASSERT_NOT_NULL(pipe_effects);
    gmf_setup_pipeline_out_dev(pipe_effects);

    esp_gmf_element_handle_t alc_el = NULL;
    esp_gmf_element_handle_t sonic_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_effects, "aud_alc", &alc_el);
    esp_gmf_pipeline_get_el_by_name(pipe_effects, "aud_sonic", &sonic_el);
    esp_gmf_audio_param_set_alc_channel_gain(alc_el, 0xff, -10);
    esp_gmf_audio_param_set_speed(sonic_el, 1.2);
    esp_gmf_audio_param_set_pitch(sonic_el, 1.2);

    esp_gmf_task_cfg_t effects_task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    effects_task_cfg.ctx = NULL;
    effects_task_cfg.cb = NULL;
    effects_task_cfg.name = "effects_task";
    esp_gmf_task_handle_t effects_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&effects_task_cfg, &effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe_effects, effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe_effects, _pipeline_event, pipe_sync_evt2));

    // Connect pipeline1 to effects pipeline
    esp_gmf_db_handle_t rb_handle = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &rb_handle));
    TEST_ASSERT_NOT_NULL(rb_handle);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write,
                                                               esp_gmf_db_deinit, rb_handle, 4096, ESP_GMF_MAX_DELAY);
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read,
                                                             esp_gmf_db_deinit, rb_handle, 4096, ESP_GMF_MAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe, "aud_bit_cvt", out_port, pipe_effects, "aud_alc", in_port));
    pipeline_strategy_ctx1_t strategy_ctx2 = {
        .replay_count = 0,
        .replay_num = strategy_ctx.file_count,
        .io = pipe_effects->in,
        .reset_sem = reset_sem,
        .db = rb_handle,
    };
    esp_gmf_task_set_strategy_func(effects_task, effects_pipeline_strategy_func, &strategy_ctx2);

    // Run pipelines
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe_effects));
    xEventGroupWaitBits(pipe_sync_evt1, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt2, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(effects_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe_effects));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    vSemaphoreDelete(reset_sem);
    vEventGroupDelete(pipe_sync_evt1);
    vEventGroupDelete(pipe_sync_evt2);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_codec_dev();
    esp_gmf_app_wifi_disconnect();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio File Stream Encode Different URL Without Close, One pipeline", "[ESP_GMF_POOL]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_ELEMENT", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_IO", ESP_LOG_DEBUG);

    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();
    codec_info.play_info.sample_rate = 44100;
    codec_info.play_info.channel = 2;
    codec_info.play_info.bits_per_sample = 16;
    codec_info.record_info = codec_info.play_info;
    esp_gmf_app_setup_codec_dev(&codec_info);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);

    // Create semaphore for synchronizing reset between pipelines
    SemaphoreHandle_t reset_sem = xSemaphoreCreateBinary();
    ESP_GMF_NULL_CHECK(TAG, reset_sem, return);

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    // Create pipeline1
    esp_gmf_pipeline_handle_t pipe = NULL;
    const char *name[] = {"aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt", "aud_enc"};
    esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_file", &pipe);
    TEST_ASSERT_NOT_NULL(pipe);
    esp_gmf_element_handle_t rate_cvt_el = NULL;
    esp_gmf_element_handle_t ch_cvt_el = NULL;
    esp_gmf_element_handle_t bit_cvt_el = NULL;
    esp_gmf_element_handle_t enc_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_rate_cvt", &rate_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_ch_cvt", &ch_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_bit_cvt", &bit_cvt_el);
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_enc", &enc_el);

    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    cfg.thread.stack = 40 * 1024;
    esp_gmf_task_handle_t work_task = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg, &work_task));
    static pipeline_strategy_ctx3_t strategy_ctx = {0};
    esp_gmf_task_set_strategy_func(work_task, pipeline_strategy_encode_func, &strategy_ctx);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));
    esp_gmf_info_sound_t src_info = {
        .sample_rates = 24000,
        .channels = 1,
        .bits = 16,
    };
    for (int i = 0; i < sizeof(enc_test_type) / sizeof(int); i++) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, "/sdcard/thetest24_1.pcm"));
        char out_url[100] = {0};
        sprintf(out_url, "/sdcard/test1.%s", test_enc_format[i]);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_out_uri(pipe, out_url));
        strategy_ctx.encode_count = 0;
        strategy_ctx.encode_num = 1;
        strategy_ctx.io_in = pipe->in;
        strategy_ctx.io_out = pipe->out;
        sprintf(strategy_ctx.url, "/sdcard/test2.%s", test_enc_format[i]);
        esp_gmf_info_sound_t info = {
            .format_id = enc_test_type[i],
            .bits = 16,
        };
        if (enc_test_type[i] == ESP_FOURCC_AMRNB) {
            info.sample_rates = 8000;
            info.channels = 1;
            info.bitrate = 12200;
        } else if (enc_test_type[i] == ESP_FOURCC_AMRWB) {
            info.sample_rates = 16000;
            info.channels = 1;
            info.bitrate = 23850;
        } else if (enc_test_type[i] == ESP_FOURCC_OPUS) {
            info.sample_rates = 48000;
            info.channels = 2;
            info.bitrate = 80000;
        } else {
            info.sample_rates = 44100;
            info.channels = 2;
            info.bitrate = 80000;
        }
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_enc_reconfig_by_sound_info(enc_el, &info));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_cvt_el, info.sample_rates));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_cvt_el, info.channels));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_cvt_el, info.bits));
        esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &src_info, sizeof(src_info));
        ESP_GMF_MEM_SHOW(TAG);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));
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
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}
