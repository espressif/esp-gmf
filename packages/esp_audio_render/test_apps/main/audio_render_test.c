/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "audio_render_proc.h"
#include "esp_audio_render.h"
#include <esp_gmf_pool.h>
#include <esp_gmf_element.h>
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_sonic.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_fade.h"
#include "esp_gmf_audio_enc.h"
#include "esp_audio_enc_default.h"
#include "esp_audio_enc_reg.h"
#include "esp_gmf_fade.h"
#include "esp_log.h"

#define TAG "RENDER_TEST"

#define BREAK_ON_FAIL(sta)                                                   \
    {                                                                        \
        int _ret = sta;                                                      \
        if (_ret) {                                                          \
            ESP_LOGE(TAG, "Fail at %s:%d ret %d", __func__, __LINE__, _ret); \
            break;                                                           \
        }                                                                    \
    }

#define SAMPLE_SIZE(sample_info)            (sample_info.bits_per_sample * sample_info.channel / 8)
#define SAMPLE_SIZE_PER_SECOND(sample_info) (sample_info.sample_rate * SAMPLE_SIZE(sample_info))
#define AAC_FRAME_SAMPLES                   (1024)
#define ELEMS(arr)                          sizeof(arr)/sizeof(arr[0])
#define DEFAULT_MIXER_INTERVAL              (20)

typedef struct {
    esp_audio_render_sample_info_t  sample_info;
    uint32_t                        total_write;
    uint32_t                        write_count;
    bool                            is_open;
    bool                            is_close;
    uint8_t                        *expect_samples;
    uint8_t                        *actual_samples;
    uint8_t                         expect_num;
    uint8_t                         expect_filled;
} render_out_res_t;

static int create_default_pool(esp_gmf_pool_handle_t *pool)
{
    *pool = NULL;
    do {
        BREAK_ON_FAIL(esp_gmf_pool_init(pool));
        esp_gmf_element_handle_t el = NULL;

        esp_ae_ch_cvt_cfg_t ch_cvt_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
        esp_gmf_ch_cvt_init(&ch_cvt_cfg, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));

        esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
        esp_gmf_bit_cvt_init(&bit_cvt_cfg, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));

        esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
        esp_gmf_rate_cvt_init(&rate_cvt_cfg, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));

        esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
        esp_gmf_alc_init(&alc_cfg, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));

        esp_ae_sonic_cfg_t sonic_cfg = DEFAULT_ESP_GMF_SONIC_CONFIG();
        esp_gmf_sonic_init(&sonic_cfg, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));

        esp_ae_eq_cfg_t eq_cfg = DEFAULT_ESP_GMF_EQ_CONFIG();
        esp_gmf_eq_init(&eq_cfg, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));

        esp_ae_fade_cfg_t fade_cfg = DEFAULT_ESP_GMF_FADE_CONFIG();
        esp_gmf_fade_init(&fade_cfg, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));

        esp_gmf_audio_enc_init(NULL, &el);
        BREAK_ON_FAIL(esp_gmf_pool_register_element(*pool, el, NULL));
        return 0;
    } while (0);
    return -1;
}

static void destroy_default_pool(esp_gmf_pool_handle_t pool)
{
    if (pool) {
        esp_gmf_pool_deinit(pool);
    }
}

static int render_test_write_hdlr(uint8_t *pcm_data, uint32_t len, void *ctx)
{
    render_out_res_t *writer = (render_out_res_t*)ctx;
    writer->total_write += len;
    writer->write_count++;
    return 0;
}

int audio_render_proc_bypass_test(int write_count)
{
    esp_audio_render_sample_info_t in_sample_info = {
        .sample_rate = 48000,
        .bits_per_sample = 16,
        .channel = 2,
    };
    esp_audio_render_sample_info_t out_sample_info = in_sample_info;
    esp_gmf_pool_handle_t pool = NULL;
    render_out_res_t writer = {};
    create_default_pool(&pool);

    uint8_t write_data[256];
    audio_render_proc_handle_t proc_handle = NULL;
    bool success = false;
    do {
        audio_render_proc_create(pool, &proc_handle);
        BREAK_ON_FAIL(proc_handle == NULL);
        int ret = 0;
        ret = audio_render_proc_set_writer(proc_handle, render_test_write_hdlr, &writer);
        BREAK_ON_FAIL(ret);
        ret = audio_render_proc_open(proc_handle, &in_sample_info, &out_sample_info);
        BREAK_ON_FAIL(ret)
        for (int i = 0; i < write_count; i++) {
            ret = audio_render_proc_write(proc_handle, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret)
        }
        BREAK_ON_FAIL(ret);
        ret = audio_render_proc_close(proc_handle);
        BREAK_ON_FAIL(ret)
        int expected = sizeof(write_data) * write_count;
        BREAK_ON_FAIL(writer.total_write != expected);
        success = true;
    } while (0);
    if (proc_handle) {
        audio_render_proc_destroy(proc_handle);
    }
    destroy_default_pool(pool);
    return success ? 0 : -1;
}

int audio_render_proc_basic_test(int write_count)
{
    esp_audio_render_sample_info_t in_sample_info = {
        .sample_rate = 48000,
        .bits_per_sample = 16,
        .channel = 2,
    };
    esp_audio_render_sample_info_t out_sample_info = {
        .sample_rate = 16000,
        .bits_per_sample = 16,
        .channel = 1,
    };
    esp_gmf_pool_handle_t pool = NULL;
    render_out_res_t writer = {};

    create_default_pool(&pool);
    uint8_t write_data[256*3];
    audio_render_proc_handle_t proc_handle = NULL;
    bool success = false;
    uint64_t expected;
    int ret = 0;
    do {
        audio_render_proc_create(pool, &proc_handle);
        BREAK_ON_FAIL(proc_handle == NULL);
        ret = audio_render_proc_set_writer(proc_handle, render_test_write_hdlr, &writer);
        BREAK_ON_FAIL(ret);
        ret = audio_render_proc_open(proc_handle, &in_sample_info, &out_sample_info);
        BREAK_ON_FAIL(ret);
        for (int i = 0; i < write_count; i++) {
            ret = audio_render_proc_write(proc_handle, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret)
        }
        BREAK_ON_FAIL(ret);
        expected = sizeof(write_data) * write_count;
        expected = expected * SAMPLE_SIZE_PER_SECOND(out_sample_info) / SAMPLE_SIZE_PER_SECOND(in_sample_info);
        ESP_LOGI(TAG, "Get %d expected %d", (int)writer.total_write, (int)expected);
        BREAK_ON_FAIL(writer.total_write != expected);
        ret = audio_render_proc_close(proc_handle);
        BREAK_ON_FAIL(ret);

        // Reverse configuration and retry
        esp_audio_render_sample_info_t tmp = in_sample_info;
        in_sample_info = out_sample_info;
        out_sample_info = tmp;
        writer.total_write = 0;

        ret = audio_render_proc_open(proc_handle, &in_sample_info, &out_sample_info);
        BREAK_ON_FAIL(ret);
        BREAK_ON_FAIL(proc_handle == NULL);
        audio_render_proc_set_writer(proc_handle, render_test_write_hdlr, &writer);
        for (int i = 0; i < write_count; i++) {
            ret = audio_render_proc_write(proc_handle, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret)
        }
        BREAK_ON_FAIL(ret);
        ret = audio_render_proc_close(proc_handle);
        BREAK_ON_FAIL(ret);
        expected = sizeof(write_data) * write_count;
        expected = expected * SAMPLE_SIZE_PER_SECOND(out_sample_info) / SAMPLE_SIZE_PER_SECOND(in_sample_info);
        ESP_LOGI(TAG, "Get %d expected %d", (int)writer.total_write, (int)expected);
        BREAK_ON_FAIL(writer.total_write != expected);

        success = true;
    } while (0);
    if (proc_handle) {
        audio_render_proc_destroy(proc_handle);
    }
    destroy_default_pool(pool);
    return success ? 0 : -1;
}

int audio_render_proc_typical_test(int write_count)
{
    esp_audio_render_sample_info_t in_sample_info = {
         .sample_rate = 44100,
            .bits_per_sample = 16,
            .channel = 2,
    };
    esp_audio_render_sample_info_t out_sample_info = {
        .sample_rate = 16000,
        .bits_per_sample = 16,
        .channel = 1,
    };

    // Add ALC and sonic for test also
    esp_audio_render_proc_type_t procs[] = {
        ESP_AUDIO_RENDER_PROC_ALC,
        ESP_AUDIO_RENDER_PROC_FADE,
    };
    render_out_res_t writer = {};
    esp_gmf_pool_handle_t pool = NULL;
    create_default_pool(&pool);
    uint8_t write_data[256*3];
    audio_render_proc_handle_t proc_handle = NULL;
    bool success = false;
    uint64_t expected;
    int ret = 0;
    do {
        audio_render_proc_create(pool, &proc_handle);
        BREAK_ON_FAIL(proc_handle == NULL);
        audio_render_proc_set_writer(proc_handle, render_test_write_hdlr, &writer);

        ret = audio_render_proc_add(proc_handle, procs, sizeof(procs)/sizeof(procs[0]));
        BREAK_ON_FAIL(ret);

        ret = audio_render_proc_open(proc_handle, &in_sample_info, &out_sample_info);
        BREAK_ON_FAIL(ret);
        for (int i = 0; i < write_count; i++) {
            ret = audio_render_proc_write(proc_handle, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret)
        }
        BREAK_ON_FAIL(ret);
        expected = sizeof(write_data) * write_count;
        expected = expected * SAMPLE_SIZE_PER_SECOND(out_sample_info) / SAMPLE_SIZE_PER_SECOND(in_sample_info);
        ESP_LOGI(TAG, "Get %d expected %d", (int)writer.total_write, (int)expected);
        audio_render_proc_close(proc_handle);

        // Reverse configuration and retry
        esp_audio_render_sample_info_t tmp = in_sample_info;
        in_sample_info = out_sample_info;
        out_sample_info = tmp;
        writer.total_write = 0;

        ret = audio_render_proc_open(proc_handle, &in_sample_info, &out_sample_info);
        BREAK_ON_FAIL(ret);
        BREAK_ON_FAIL(proc_handle == NULL);
        audio_render_proc_set_writer(proc_handle, render_test_write_hdlr, &writer);
        for (int i = 0; i < write_count; i++) {
            ret = audio_render_proc_write(proc_handle, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret)
        }
        BREAK_ON_FAIL(ret);
        ret = audio_render_proc_close(proc_handle);
        BREAK_ON_FAIL(ret);
        expected = sizeof(write_data) * write_count;
        expected = expected * SAMPLE_SIZE_PER_SECOND(out_sample_info) / SAMPLE_SIZE_PER_SECOND(in_sample_info);
        ESP_LOGI(TAG, "Get %d expected %d", (int)writer.total_write, (int)expected);
        success = true;

    } while (0);
    if (proc_handle) {
        audio_render_proc_destroy(proc_handle);
    }
    destroy_default_pool(pool);
    return success ? 0 : -1;
}

static int render_post_write_hdlr(uint8_t *pcm_data, uint32_t len, void *ctx)
{
    render_out_res_t *res = (render_out_res_t*)ctx;
    res->total_write += len;
    res->write_count++;
    return 0;
}

static int render_event_hdlr(esp_audio_render_event_type_t event_type, void *ctx)
{
    render_out_res_t *res = (render_out_res_t*)ctx;
    if (event_type == ESP_AUDIO_RENDER_EVENT_TYPE_OPENED) {
        res->is_open = true;
    } else if (event_type == ESP_AUDIO_RENDER_EVENT_TYPE_CLOSED) {
        res->is_close = true;
    }
    return 0;
}

int audio_render_one_stream_no_proc(int write_count)
{
    render_out_res_t res = {};
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = 1,
        .out_sample_info = {
            .sample_rate = 48000,
            .bits_per_sample = 16,
            .channel = 2,
        },
        .out_writer = render_post_write_hdlr,
        .out_ctx = &res,
    };
    create_default_pool((esp_gmf_pool_handle_t*)&cfg.pool);
    esp_audio_render_err_t ret;
    esp_audio_render_handle_t render = NULL;
    bool success = false;

    esp_audio_render_sample_info_t in_sample_info = {
        .sample_rate = 48000,
        .bits_per_sample = 16,
        .channel = 2,
    };
    uint8_t write_data[256*3];
    uint64_t expected;
    do {
        ret = esp_audio_render_create(&cfg, &render);
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_set_event_cb(render, render_event_hdlr, &res);
        BREAK_ON_FAIL(ret);

        esp_audio_render_stream_handle_t stream = NULL;
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_FIRST_STREAM, &stream);
        BREAK_ON_FAIL(stream == NULL);

        ret = esp_audio_render_stream_open(stream, &in_sample_info);
        BREAK_ON_FAIL(ret);
        for (int i = 0; i < write_count; i++) {
            ret = esp_audio_render_stream_write(stream, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_stream_close(stream);
        // Verify result
        if (res.is_open == false || res.is_close == false) {
            ESP_LOGE(TAG, "Failed to verify open:%d close:%d", res.is_open, res.is_close);
            break;
        }
        expected = sizeof(write_data) * write_count;
        ESP_LOGI(TAG, "Expected %d actually render %d", (int)expected, (int)res.total_write);
        if (res.total_write != expected) {
            ESP_LOGE(TAG, "Render output not as expected");
            break;
        }

        // Now test to use fixed sample_rate
        memset(&res, 0, sizeof(res));
        esp_audio_render_sample_info_t out_info = {
            .sample_rate = 16000,
            .bits_per_sample = 16,
            .channel = 1,
        };
        ret = esp_audio_render_set_out_sample_info(render, &out_info);
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_stream_open(stream, &in_sample_info);
        BREAK_ON_FAIL(ret);
        for (int i = 0; i < write_count; i++) {
            ret = esp_audio_render_stream_write(stream, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        ret = esp_audio_render_stream_close(stream);
        // Verify result
        if (res.is_open == false || res.is_close == false) {
            ESP_LOGE(TAG, "Failed to verify open:%d close:%d", res.is_open, res.is_close);
            break;
        }
        expected = sizeof(write_data) * write_count;
        expected = expected * SAMPLE_SIZE_PER_SECOND(out_info) / SAMPLE_SIZE_PER_SECOND(in_sample_info);
        ESP_LOGI(TAG, "Expected %d actually render %d", (int)expected, (int)res.total_write);
        if (res.total_write != expected) {
            ESP_LOGE(TAG, "Render output not as expected");
            break;
        }
        success = true;
    } while (0);
    if (render) {
        esp_audio_render_destroy(render);
    }
    destroy_default_pool((esp_gmf_pool_handle_t)cfg.pool);
    return success ? 0 : -1;
}

int audio_render_one_stream_with_proc(int write_count)
{
    render_out_res_t res = {};
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = 1,
        .out_sample_info = {
            .sample_rate = 48000,
            .bits_per_sample = 16,
            .channel = 2,
        },
        .out_writer = render_post_write_hdlr,
        .out_ctx = &res,
    };
    create_default_pool((esp_gmf_pool_handle_t*)&cfg.pool);
    esp_audio_render_err_t ret;
    esp_audio_render_handle_t render = NULL;
    bool success = false;
    esp_audio_render_sample_info_t in_sample_info = {
        .sample_rate = 48000,
        .bits_per_sample = 16,
        .channel = 2,
    };
    uint8_t write_data[256*3];
    uint64_t expected;
    esp_audio_render_proc_type_t procs[] = {
        ESP_AUDIO_RENDER_PROC_ALC,
        ESP_AUDIO_RENDER_PROC_FADE,
    };
    do {
        ret = esp_audio_render_create(&cfg, &render);
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_set_event_cb(render, render_event_hdlr, &res);
        BREAK_ON_FAIL(ret);

        esp_audio_render_stream_handle_t stream = NULL;
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_FIRST_STREAM, &stream);
        BREAK_ON_FAIL(stream == NULL);

        // Need set only once
        ret = esp_audio_render_stream_add_proc(stream, procs, sizeof(procs)/sizeof(procs[0]));
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < sizeof(procs)/sizeof(procs[0]); i++) {
            esp_gmf_element_handle_t element = NULL;
            ret = esp_audio_render_stream_get_element(stream, procs[i], &element);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_stream_open(stream, &in_sample_info);
        BREAK_ON_FAIL(ret);
        // Verify get element is OK
        for (int i = 0; i < sizeof(procs)/sizeof(procs[0]); i++) {
            esp_gmf_element_handle_t element = NULL;
            ret = esp_audio_render_stream_get_element(stream, procs[i], &element);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < write_count; i++) {
            ret = esp_audio_render_stream_write(stream, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        ret = esp_audio_render_stream_close(stream);
        // Verify result
        if (res.is_open == false || res.is_close == false) {
            ESP_LOGE(TAG, "Failed to verify open:%d close:%d", res.is_open, res.is_close);
            break;
        }
        expected = sizeof(write_data) * write_count;
        ESP_LOGI(TAG, "Expected %d actually render %d", (int)expected, (int)res.total_write);
        if (res.total_write != expected) {
            ESP_LOGE(TAG, "Render output not as expected");
            break;
        }

        // Now test to use fixed sample_rate
        memset(&res, 0, sizeof(res));
        esp_audio_render_sample_info_t out_info = {
            .sample_rate = 16000,
            .bits_per_sample = 16,
            .channel = 1,
        };
        ret = esp_audio_render_set_out_sample_info(render, &out_info);
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_stream_open(stream, &in_sample_info);
        BREAK_ON_FAIL(ret);
        for (int i = 0; i < write_count; i++) {
            ret = esp_audio_render_stream_write(stream, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        ret = esp_audio_render_stream_close(stream);
        // Verify result
        if (res.is_open == false || res.is_close == false) {
            ESP_LOGE(TAG, "Failed to verify open:%d close:%d", res.is_open, res.is_close);
            break;
        }
        expected = sizeof(write_data) * write_count;
        expected = expected * SAMPLE_SIZE_PER_SECOND(out_info) / SAMPLE_SIZE_PER_SECOND(in_sample_info);
        ESP_LOGI(TAG, "Expected %d actually render %d", (int)expected, (int)res.total_write);
        if (res.total_write != expected) {
            ESP_LOGE(TAG, "Render output not as expected");
            break;
        }
        success = true;
    } while (0);
    if (render) {
        esp_audio_render_destroy(render);
    }
    destroy_default_pool((esp_gmf_pool_handle_t)cfg.pool);
    return success ? 0 : -1;
}

int audio_render_one_stream_with_enc_post(int write_count)
{
    render_out_res_t res = {};
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = 1,
        .out_sample_info = {
            .sample_rate = 48000,
            .bits_per_sample = 16,
            .channel = 2,
        },
        .out_writer = render_post_write_hdlr,
        .out_ctx = &res,
    };
    create_default_pool((esp_gmf_pool_handle_t*)&cfg.pool);
    esp_audio_render_err_t ret;
    esp_audio_render_handle_t render = NULL;
    bool success = false;
    esp_audio_render_sample_info_t in_sample_info = {
        .sample_rate = 48000,
        .bits_per_sample = 16,
        .channel = 2,
    };
    uint8_t write_data[256*3];
    uint64_t expected;
    esp_audio_render_proc_type_t procs[] = {
        ESP_AUDIO_RENDER_PROC_ALC,
        ESP_AUDIO_RENDER_PROC_ENC,
    };
    esp_aac_enc_register();
    do {
        ret = esp_audio_render_create(&cfg, &render);
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_set_event_cb(render, render_event_hdlr, &res);
        BREAK_ON_FAIL(ret);

        esp_audio_render_stream_handle_t stream = NULL;
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_FIRST_STREAM, &stream);
        BREAK_ON_FAIL(stream == NULL);

        // Need set only once
        ret = esp_audio_render_stream_add_proc(stream, procs, sizeof(procs)/sizeof(procs[0]));
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < sizeof(procs)/sizeof(procs[0]); i++) {
            esp_gmf_element_handle_t element = NULL;
            ret = esp_audio_render_stream_get_element(stream, procs[i], &element);
            BREAK_ON_FAIL(ret);
            if (procs[i] == ESP_AUDIO_RENDER_PROC_ENC) {
                // Setup for encoder type
                esp_aac_enc_config_t aac_cfg = ESP_AAC_ENC_CONFIG_DEFAULT();
                esp_audio_enc_config_t enc_cfg = {
                    .type = ESP_AUDIO_TYPE_AAC,
                    .cfg = &aac_cfg,
                    .cfg_sz = sizeof(aac_cfg),
                };
                esp_gmf_audio_enc_reconfig(element, &enc_cfg);
                BREAK_ON_FAIL(ret);
            }
        }
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_stream_open(stream, &in_sample_info);
        BREAK_ON_FAIL(ret);
        // Verify get element is OK
        for (int i = 0; i < sizeof(procs)/sizeof(procs[0]); i++) {
            esp_gmf_element_handle_t element = NULL;
            ret = esp_audio_render_stream_get_element(stream, procs[i], &element);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < write_count; i++) {
            ret = esp_audio_render_stream_write(stream, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        ret = esp_audio_render_stream_close(stream);
        // Verify result
        if (res.is_open == false || res.is_close == false) {
            ESP_LOGE(TAG, "Failed to verify open:%d close:%d", res.is_open, res.is_close);
            break;
        }
        expected = sizeof(write_data) * write_count;
        expected = expected / SAMPLE_SIZE(in_sample_info) / AAC_FRAME_SAMPLES;
        ESP_LOGI(TAG, "Expected %d actually render %d", (int)expected, (int)res.write_count);
        if (res.write_count != expected) {
            ESP_LOGE(TAG, "Render output not as expected");
            //break;
        }

        // Now test to use fixed sample_rate
        memset(&res, 0, sizeof(res));
        esp_audio_render_sample_info_t out_info = {
            .sample_rate = 16000,
            .bits_per_sample = 16,
            .channel = 1,
        };
        ret = esp_audio_render_set_out_sample_info(render, &out_info);
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_stream_open(stream, &in_sample_info);
        BREAK_ON_FAIL(ret);
        for (int i = 0; i < write_count; i++) {
            ret = esp_audio_render_stream_write(stream, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        ret = esp_audio_render_stream_close(stream);
        // Verify result
        if (res.is_open == false || res.is_close == false) {
            ESP_LOGE(TAG, "Failed to verify open:%d close:%d", res.is_open, res.is_close);
            break;
        }
        expected = sizeof(write_data) * write_count;
        expected = expected / SAMPLE_SIZE(in_sample_info) / AAC_FRAME_SAMPLES;
        expected = expected * out_info.sample_rate / in_sample_info.sample_rate;
        ESP_LOGI(TAG, "Expected %d actually render %d", (int)expected, (int)res.write_count);
        if (res.write_count != expected) {
            ESP_LOGE(TAG, "Render output not as expected");
            //break;
        }
        success = true;
    } while (0);
    if (render) {
        esp_audio_render_destroy(render);
    }
    esp_audio_enc_unregister(ESP_AUDIO_TYPE_AAC);
    destroy_default_pool((esp_gmf_pool_handle_t)cfg.pool);
    return success ? 0 : -1;
}

int audio_render_dual_stream_no_proc(int write_count)
{
    render_out_res_t res = {};
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = 2,
        .out_sample_info = {
            .sample_rate = 48000,
            .bits_per_sample = 16,
            .channel = 2,
        },
        .out_writer = render_post_write_hdlr,
        .out_ctx = &res,
    };
    create_default_pool((esp_gmf_pool_handle_t*)&cfg.pool);
    esp_audio_render_err_t ret;
    esp_audio_render_handle_t render = NULL;
    bool success = false;
    esp_audio_render_sample_info_t in_sample_info = {
        .sample_rate = 48000,
        .bits_per_sample = 16,
        .channel = 2,
    };
    uint8_t write_data[256*3];
    uint64_t expected;
    do {
        ret = esp_audio_render_create(&cfg, &render);
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_set_event_cb(render, render_event_hdlr, &res);
        BREAK_ON_FAIL(ret);

        esp_audio_render_sample_info_t out_info = {
            .sample_rate = 16000,
            .bits_per_sample = 16,
            .channel = 1,
        };
        ret = esp_audio_render_set_out_sample_info(render, &out_info);
        BREAK_ON_FAIL(ret);

        esp_audio_render_stream_handle_t stream[2] = {NULL};
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_FIRST_STREAM, &stream[0]);
        BREAK_ON_FAIL(stream[0] == NULL);
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_SECOND_STREAM, &stream[1]);
        BREAK_ON_FAIL(stream[1] == NULL);

        int run_loop = 2;
        while (run_loop-- > 0) {
            memset(&res, 0, sizeof(res));
            for (int i = 0; i < 2; i++) {
                if (i == 0) {
                    esp_audio_render_mixer_gain_t mixer_gain = {
                        .initial_gain = 0.2,
                        .target_gain = 0.5,
                        .transition_time = 200,
                    };
                    ret = esp_audio_render_stream_set_mixer_gain(stream[i], &mixer_gain);
                    BREAK_ON_FAIL(ret);
                }
                ret = esp_audio_render_stream_open(stream[i], &in_sample_info);
                BREAK_ON_FAIL(ret);
            }
            BREAK_ON_FAIL(ret);

            for (int i = 0; i < write_count; i++) {
                for (int j = 0; j < 2; j++) {
                    ret = esp_audio_render_stream_write(stream[j], write_data, sizeof(write_data));
                    BREAK_ON_FAIL(ret);
                }
                BREAK_ON_FAIL(ret);
                if (i == write_count / 2) {
                    ret = esp_audio_render_stream_set_fade(stream[0], false);
                    BREAK_ON_FAIL(ret);
                    ret = esp_audio_render_stream_set_fade(stream[1], true);
                    BREAK_ON_FAIL(ret);
                }
            }
            BREAK_ON_FAIL(ret);
            vTaskDelay(pdMS_TO_TICKS(DEFAULT_MIXER_INTERVAL));
            for (int i = 0; i < 2; i++) {
                ret = esp_audio_render_stream_close(stream[i]);
                BREAK_ON_FAIL(ret);
            }
            BREAK_ON_FAIL(ret);
            // Verify result
            if (res.is_open == false || res.is_close == false) {
                ESP_LOGE(TAG, "Failed to verify open:%d close:%d", res.is_open, res.is_close);
                ret = -1;
                break;
            }
            expected = sizeof(write_data) * write_count;
            expected = expected * SAMPLE_SIZE_PER_SECOND(out_info) / SAMPLE_SIZE_PER_SECOND(in_sample_info);
            ESP_LOGI(TAG, "Expected %d actually render %d", (int)expected, (int)res.total_write);
            if (res.total_write < expected) {
                ESP_LOGW(TAG, "Render output not as expected");
                ret = -1;
                break;
            }
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        success = true;
    } while (0);
    if (render) {
        esp_audio_render_destroy(render);
    }
    destroy_default_pool((esp_gmf_pool_handle_t)cfg.pool);
    return success ? 0 : -1;
}

int audio_render_dual_stream_with_proc(int write_count)
{
    render_out_res_t res = {};
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = 2,
        .out_sample_info = {
            .sample_rate = 48000,
            .bits_per_sample = 16,
            .channel = 2,
        },
        .out_writer = render_post_write_hdlr,
        .out_ctx = &res,
    };
    create_default_pool((esp_gmf_pool_handle_t*)&cfg.pool);
    esp_audio_render_err_t ret;
    esp_audio_render_handle_t render = NULL;
    bool success = false;

    esp_audio_render_sample_info_t in_sample_info = {
        .sample_rate = 48000,
        .bits_per_sample = 16,
        .channel = 2,
    };
    uint8_t write_data[256*3];
    uint64_t expected;
    esp_audio_render_proc_type_t procs[] = {
        ESP_AUDIO_RENDER_PROC_ALC,
        ESP_AUDIO_RENDER_PROC_FADE,
    };
    do {
        ret = esp_audio_render_create(&cfg, &render);
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_set_event_cb(render, render_event_hdlr, &res);
        BREAK_ON_FAIL(ret);

        esp_audio_render_sample_info_t out_info = {
            .sample_rate = 16000,
            .bits_per_sample = 16,
            .channel = 1,
        };
        ret = esp_audio_render_set_out_sample_info(render, &out_info);
        BREAK_ON_FAIL(ret);

        // Add post process
        ret = esp_audio_render_add_mixed_proc(render, procs, sizeof(procs)/sizeof(procs[0]));
        BREAK_ON_FAIL(ret);

        esp_audio_render_stream_handle_t stream[2] = {NULL};
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_FIRST_STREAM, &stream[0]);
        BREAK_ON_FAIL(stream[0] == NULL);
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_SECOND_STREAM, &stream[1]);
        BREAK_ON_FAIL(stream[1] == NULL);

        for (int i = 0; i < 2; i++) {
            ret = esp_audio_render_stream_add_proc(stream[i], procs, sizeof(procs)/sizeof(procs[0]));
            BREAK_ON_FAIL(ret);
            ret = esp_audio_render_stream_open(stream[i], &in_sample_info);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);

        for (int i = 0; i < cfg.max_stream_num + 1; i++) {
            for (int j = 0; j < sizeof(procs)/sizeof(procs[0]); j++) {
                esp_gmf_element_handle_t element = NULL;
                if (i == cfg.max_stream_num) {
                     ret = esp_audio_render_get_mixed_element(render, procs[j], &element);
                } else {
                    ret = esp_audio_render_stream_get_element(stream[i],  procs[j], &element);
                }
                BREAK_ON_FAIL(ret);
            }
            BREAK_ON_FAIL(ret);
        }
        for (int i = 0; i < write_count; i++) {
            for (int j = 0; j < 2; j++) {
                ret = esp_audio_render_stream_write(stream[j], write_data, sizeof(write_data));
                BREAK_ON_FAIL(ret);
            }
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        vTaskDelay(pdMS_TO_TICKS(DEFAULT_MIXER_INTERVAL));
        for (int i = 0; i < 2; i++) {
            ret = esp_audio_render_stream_close(stream[i]);
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);
        // Verify result
        if (res.is_open == false || res.is_close == false) {
            ESP_LOGE(TAG, "Failed to verify open:%d close:%d", res.is_open, res.is_close);
            break;
        }
        expected = sizeof(write_data) * write_count;
        expected = expected * SAMPLE_SIZE_PER_SECOND(out_info) / SAMPLE_SIZE_PER_SECOND(in_sample_info);
        ESP_LOGI(TAG, "Expected %d actually render %d", (int)expected, (int)res.total_write);
        if (res.total_write < expected) {
            ESP_LOGE(TAG, "Render output not as expected");
            break;
        }
        success = true;
    } while (0);
    if (render) {
        esp_audio_render_destroy(render);
    }
    destroy_default_pool((esp_gmf_pool_handle_t)cfg.pool);
    return success ? 0 : -1;
}

static int render_post_verify_hdlr(uint8_t *pcm_data, uint32_t len, void *ctx)
{
    render_out_res_t *res = (render_out_res_t*)ctx;
    if (res->expect_filled < res->expect_num) {
        if (res->expect_filled == 0 && pcm_data[0] == 0) {
            // Skip data before verify
            return 0;
        }
        uint8_t expected = res->expect_samples[res->expect_filled];
        res->actual_samples[res->expect_filled] = expected;
        int16_t expected_sample = (expected << 8) | expected;
        int16_t *v = (int16_t*)pcm_data;
        for (int i = 0; i < len >> 1; i++) {
            if (*v != expected_sample && *v != expected_sample - 1) {
                uint8_t *cur = (uint8_t*)v;
                if (*cur != expected) {
                    res->actual_samples[res->expect_filled] = *cur;
                } else {
                    res->actual_samples[res->expect_filled] = *(cur + 1);
                }
                break;
            }
            v++;
        }
        res->expect_filled++;
    }
    vTaskDelay(pdMS_TO_TICKS(DEFAULT_MIXER_INTERVAL));
    return 0;
}

int audio_render_dual_stream_one_slow(int write_count)
{
    render_out_res_t res = {};
    // Force to use mixer
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = 2,
        .out_sample_info = {
            .sample_rate = 8000,
            .bits_per_sample = 16,
            .channel = 2,
        },
        .out_writer = render_post_verify_hdlr,
        .out_ctx = &res,
    };
    create_default_pool((esp_gmf_pool_handle_t*)&cfg.pool);
    esp_audio_render_err_t ret;
    esp_audio_render_handle_t render = NULL;
    bool success = false;
    // Force input and out sample info same to verify data
    esp_audio_render_sample_info_t in_sample_info = {
        .sample_rate = 8000,
        .bits_per_sample = 16,
        .channel = 2,
    };
    uint8_t expect_data[] = {32, 0, 0, 16, 18, 0, 48, 8, 16, 0, 20};
    uint8_t actual_data[ELEMS(expect_data)];
    res.expect_samples = expect_data;
    res.actual_samples = actual_data;
    res.expect_num = ELEMS(expect_data);

    int frame_size = DEFAULT_MIXER_INTERVAL * in_sample_info.sample_rate / 1000 * SAMPLE_SIZE(in_sample_info);
    uint8_t *write_data = (uint8_t*)calloc(1, frame_size);
    do {
        if (write_data == NULL) {
            BREAK_ON_FAIL(ESP_AUDIO_RENDER_ERR_NO_MEM);
        }
        ret = esp_audio_render_create(&cfg, &render);
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_set_event_cb(render, render_event_hdlr, &res);
        BREAK_ON_FAIL(ret);

        esp_audio_render_stream_handle_t stream[2] = {NULL};
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_FIRST_STREAM, &stream[0]);
        BREAK_ON_FAIL(stream[0] == NULL);
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_SECOND_STREAM, &stream[1]);
        BREAK_ON_FAIL(stream[1] == NULL);

        ret = esp_audio_render_stream_open(stream[0], &in_sample_info);
        BREAK_ON_FAIL(ret);
        ret = esp_audio_render_stream_open(stream[1], &in_sample_info);
        BREAK_ON_FAIL(ret);

        // Wait for enter stable status
        for (int i = 0; i < 20; i++) {
            ret = esp_audio_render_stream_write(stream[0], write_data, frame_size);
            BREAK_ON_FAIL(ret);
        }

        // Recursive to send data, write one extra data to let mixer finished
        for (int i = 0; i < res.expect_num + 1; i++) {
            // Stream 1 always has data
            memset(write_data, 0, frame_size);
            ret = esp_audio_render_stream_write(stream[1], write_data, frame_size);
            BREAK_ON_FAIL(ret);

            if (i < res.expect_num && expect_data[i]) {
                // Actual write data is 2 * expected for mixer will add gain to each stream
                memset(write_data, expect_data[i] * 2, frame_size);
                // Write first half
                ret = esp_audio_render_stream_write(stream[0], write_data, frame_size / 2);
                BREAK_ON_FAIL(ret);
                vTaskDelay(pdMS_TO_TICKS(DEFAULT_MIXER_INTERVAL / 2) - 1);
                // Write second half
                ret = esp_audio_render_stream_write(stream[0], write_data + frame_size / 2, frame_size / 2);
                BREAK_ON_FAIL(ret);
                vTaskDelay(pdMS_TO_TICKS(DEFAULT_MIXER_INTERVAL / 2));
            } else {
                // Not write data let mixer output 0 data
                vTaskDelay(pdMS_TO_TICKS(DEFAULT_MIXER_INTERVAL) - 1);
            }
        }

        ret = esp_audio_render_stream_close(stream[0]);
        BREAK_ON_FAIL(ret);
        ret = esp_audio_render_stream_close(stream[1]);
        BREAK_ON_FAIL(ret);
         // Verify result
        if (res.is_open == false || res.is_close == false) {
            ESP_LOGE(TAG, "Failed to verify open:%d close:%d", res.is_open, res.is_close);
            break;
        }
        bool meet = true;
        if (res.expect_filled != res.expect_num) {
            meet = false;
        } else {
            for (int i = 0; i < res.expect_num; i++) {
                if (res.expect_samples[i] != res.actual_samples[i]) {
                    meet = false;
                    break;
                }
            }
        }
        if (meet == false) {
            ESP_LOGE(TAG, "Expect filled %d/%d", res.expect_filled, res.expect_num);
            printf("Expect:");
            for (int i = 0; i < res.expect_filled; i++) {
                printf("%d ", res.expect_samples[i]);
            }
            printf("\n");
            printf("Actual:");
            for (int i = 0; i < res.expect_filled; i++) {
                printf("%d ", res.actual_samples[i]);
            }
            printf("\n");
            break;
        }
        ESP_LOGI(TAG, "All frames verify OK");
        success = true;
    } while (0);
    if (render) {
        esp_audio_render_destroy(render);
    }
    if (write_data) {
        free(write_data);
    }
    destroy_default_pool((esp_gmf_pool_handle_t)cfg.pool);
    return success ? 0 : -1;
}

int audio_render_with_no_pool(int write_count)
{
    render_out_res_t res = {};
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = 1,
        .out_sample_info = {
            .sample_rate = 48000,
            .bits_per_sample = 16,
            .channel = 2,
        },
        .out_writer = render_post_write_hdlr,
        .out_ctx = &res,
    };
    esp_audio_render_err_t ret;
    esp_audio_render_handle_t render = NULL;
    bool success = false;
    uint8_t write_data[256*3];
    uint64_t expected;
    do {
        ret = esp_audio_render_create(&cfg, &render);
        BREAK_ON_FAIL(ret);
        ret = esp_audio_render_set_event_cb(render, render_event_hdlr, &res);
        BREAK_ON_FAIL(ret);

        esp_audio_render_stream_handle_t stream = NULL;
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_FIRST_STREAM, &stream);
        BREAK_ON_FAIL(stream == NULL);
        esp_audio_render_proc_type_t procs[] = {
            ESP_AUDIO_RENDER_PROC_ALC,
            ESP_AUDIO_RENDER_PROC_ENC,
        };
        ret = esp_audio_render_stream_add_proc(stream, procs, sizeof(procs)/sizeof(procs[0]));
        if (ret == ESP_AUDIO_RENDER_ERR_OK) {
            BREAK_ON_FAIL(-1);
        }
        ret = esp_audio_render_stream_open(stream, &cfg.out_sample_info);
        BREAK_ON_FAIL(ret);
        for (int i = 0; i < write_count; i++) {
            ret = esp_audio_render_stream_write(stream, write_data, sizeof(write_data));
            BREAK_ON_FAIL(ret);
        }
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_stream_close(stream);
        // Verify result
        if (res.is_open == false || res.is_close == false) {
            ESP_LOGE(TAG, "Failed to verify open:%d close:%d", res.is_open, res.is_close);
            break;
        }
        expected = sizeof(write_data) * write_count;
        ESP_LOGI(TAG, "Expected %d actually render %d", (int)expected, (int)res.total_write);
        if (res.total_write != expected) {
            ESP_LOGE(TAG, "Render output not as expected");
            break;
        }
        success = true;
    } while (0);
    if (render) {
        esp_audio_render_destroy(render);
    }
    return success ? 0 : -1;
}


static int render_solo_verify_hdlr(uint8_t *pcm_data, uint32_t len, void *ctx)
{
    render_out_res_t *res = (render_out_res_t*)ctx;
    res->actual_samples[0] = pcm_data[0];
    res->actual_samples[1] = pcm_data[len - 1];
    vTaskDelay(pdMS_TO_TICKS(DEFAULT_MIXER_INTERVAL));
    return 0;
}

int audio_render_dual_stream_solo(int write_count)
{
    render_out_res_t res = {};
    // Force to use mixer
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = 2,
        .out_sample_info = {
            .sample_rate = 8000,
            .bits_per_sample = 16,
            .channel = 2,
        },
        .out_writer = render_solo_verify_hdlr,
        .out_ctx = &res,
    };
    create_default_pool((esp_gmf_pool_handle_t*)&cfg.pool);
    esp_audio_render_err_t ret;
    esp_audio_render_handle_t render = NULL;
    bool success = false;
    // Force input and out sample info same to verify data
    esp_audio_render_sample_info_t in_sample_info = {
        .sample_rate = 8000,
        .bits_per_sample = 16,
        .channel = 2,
    };
    uint8_t actual_data[2];
    res.actual_samples = actual_data;
    int frame_size = DEFAULT_MIXER_INTERVAL * in_sample_info.sample_rate / 1000 * SAMPLE_SIZE(in_sample_info);
    uint8_t *write_data = (uint8_t*)calloc(1, frame_size);
    do {
        if (write_data == NULL) {
            BREAK_ON_FAIL(ESP_AUDIO_RENDER_ERR_NO_MEM);
        }
        ret = esp_audio_render_create(&cfg, &render);
        BREAK_ON_FAIL(ret);

        ret = esp_audio_render_set_event_cb(render, render_event_hdlr, &res);
        BREAK_ON_FAIL(ret);

        esp_audio_render_stream_handle_t stream[2] = {NULL};
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_FIRST_STREAM, &stream[0]);
        BREAK_ON_FAIL(stream[0] == NULL);
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_SECOND_STREAM, &stream[1]);
        BREAK_ON_FAIL(stream[1] == NULL);

        ret = esp_audio_render_stream_open(stream[0], &in_sample_info);
        BREAK_ON_FAIL(ret);
        ret = esp_audio_render_stream_open(stream[1], &in_sample_info);
        BREAK_ON_FAIL(ret);
        uint8_t stream_value[2] = {16, 48};
        // Wait for enter stable status
        for (int i = 0; i < 20; i++) {
            ret = esp_audio_render_stream_write(stream[0], write_data, frame_size);
            BREAK_ON_FAIL(ret);
        }
        vTaskDelay(pdMS_TO_TICKS(DEFAULT_MIXER_INTERVAL));
        ret = esp_audio_render_set_solo_stream(render, ESP_AUDIO_RENDER_FIRST_STREAM);
        BREAK_ON_FAIL(ret);

        memset(write_data, stream_value[0], frame_size);
        ret = esp_audio_render_stream_write(stream[0], write_data, frame_size);
        BREAK_ON_FAIL(ret);
        memset(write_data, stream_value[1], frame_size);
        ret = esp_audio_render_stream_write(stream[1], write_data, frame_size);
        BREAK_ON_FAIL(ret);
        if (actual_data[0] != stream_value[0] || actual_data[1] != stream_value[0]) {
            ESP_LOGE(TAG, "First stream expect %d but get %d-%d", stream_value[0], actual_data[0], actual_data[1]);
            ret = -1;
            BREAK_ON_FAIL(ret);
        }

        ret = esp_audio_render_set_solo_stream(render, ESP_AUDIO_RENDER_SECOND_STREAM);
        BREAK_ON_FAIL(ret);
        memset(write_data, stream_value[1], frame_size);
        ret = esp_audio_render_stream_write(stream[1], write_data, frame_size);
        BREAK_ON_FAIL(ret);
        memset(write_data, stream_value[0], frame_size);
        ret = esp_audio_render_stream_write(stream[0], write_data, frame_size);
        BREAK_ON_FAIL(ret);
        if (actual_data[0] != stream_value[1] || actual_data[1] != stream_value[1]) {
            ESP_LOGE(TAG, "Second stream expect %d but get %d-%d", stream_value[1], actual_data[0], actual_data[1]);
            ret = -1;
            BREAK_ON_FAIL(ret);
        }

        ret = esp_audio_render_set_solo_stream(render, ESP_AUDIO_RENDER_ALL_STREAM);
        BREAK_ON_FAIL(ret);

        for (int j = 0; j < 2; j++) {
            for (int i = 0; i < 2; i++) {
                memset(write_data, stream_value[i], frame_size);
                ret = esp_audio_render_stream_write(stream[i], write_data, frame_size);
                BREAK_ON_FAIL(ret);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(DEFAULT_MIXER_INTERVAL));
        uint8_t expected = (stream_value[0] + stream_value[1]) / 2;
        #define IS_EXPECT(a, b) (a == b || a == b - 1)
        if (!IS_EXPECT(actual_data[0], expected) || !IS_EXPECT(actual_data[1], expected)) {
            ESP_LOGE(TAG, "Mixed stream expect %d but get %d-%d", expected, actual_data[0], actual_data[1]);
            ret = -1;
            BREAK_ON_FAIL(ret);
        }
        ret = esp_audio_render_stream_close(stream[0]);
        BREAK_ON_FAIL(ret);
        ret = esp_audio_render_stream_close(stream[1]);
        BREAK_ON_FAIL(ret);
         // Verify result
        if (res.is_open == false || res.is_close == false) {
            ESP_LOGE(TAG, "Failed to verify open:%d close:%d", res.is_open, res.is_close);
            break;
        }
        success = true;
    } while (0);
    if (render) {
        esp_audio_render_destroy(render);
    }
    if (write_data) {
        free(write_data);
    }
    destroy_default_pool((esp_gmf_pool_handle_t)cfg.pool);
    return success ? 0 : -1;
}
