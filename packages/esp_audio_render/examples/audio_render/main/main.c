/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_log.h"
#include "esp_gmf_pool.h"
#include "esp_codec_dev.h"
#include "esp_audio_render.h"
#include "esp_audio_render_types.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_sonic.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_fade.h"
#include "esp_audio_dec_default.h"
#include "decode_pcm.h"
#include "esp_gmf_oal_sys.h"
#include "esp_timer.h"

#define TAG "MAIN"

#define AUDIO_RENDER_FIRST_URL   "https://dl.espressif.com/dl/audio/ff-16b-2c-44100hz.mp3"
#define AUDIO_RENDER_SECOND_URL  "https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.aac"
#define AUDIO_RENDER_THIRD_URL   "https://dl.espressif.com/dl/audio/ff-16b-2c-16000hz.mp3"
#define AUDIO_RENDER_FOURTH_URL  "https://dl.espressif.com/dl/audio/gs-16b-2c-44100hz.mp3"

#define AUDIO_RENDER_TEST_DURATION  (30000)

typedef struct {
    esp_audio_render_stream_handle_t  stream_handle;
    bool                              is_open;
    bool                              finished;
    bool                              exited;
} render_stream_t;

static int create_default_pool(esp_gmf_pool_handle_t *pool)
{
    *pool = NULL;
    if (esp_gmf_pool_init(pool) != ESP_GMF_ERR_OK) {
        return -1;
    }

    esp_gmf_element_handle_t el = NULL;
    esp_ae_ch_cvt_cfg_t ch_cvt_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    esp_gmf_ch_cvt_init(&ch_cvt_cfg, &el);
    esp_gmf_pool_register_element(*pool, el, NULL);

    esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    esp_gmf_bit_cvt_init(&bit_cvt_cfg, &el);
    esp_gmf_pool_register_element(*pool, el, NULL);

    esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    esp_gmf_rate_cvt_init(&rate_cvt_cfg, &el);
    esp_gmf_pool_register_element(*pool, el, NULL);

    esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
    esp_gmf_alc_init(&alc_cfg, &el);
    esp_gmf_pool_register_element(*pool, el, NULL);

    esp_ae_sonic_cfg_t sonic_cfg = DEFAULT_ESP_GMF_SONIC_CONFIG();
    esp_gmf_sonic_init(&sonic_cfg, &el);
    esp_gmf_pool_register_element(*pool, el, NULL);
    return 0;
}

static int music_info_hdlr(int sample_rate, uint8_t channel, uint8_t bits, void *ctx)
{
    render_stream_t *stream = (render_stream_t*)ctx;
    esp_audio_render_sample_info_t stream_info = {
        .sample_rate = sample_rate,
        .bits_per_sample = bits,
        .channel = channel,
    };
    // Open stream and set is_open flag if success
    int ret = esp_audio_render_stream_open(stream->stream_handle, &stream_info);
    if (ret == ESP_AUDIO_RENDER_ERR_OK) {
        // Can do extra setting to the process pipeline, take ALC as example
        esp_gmf_element_handle_t element = NULL;
        ret = esp_audio_render_stream_get_element(stream->stream_handle, ESP_AUDIO_RENDER_PROC_ALC, &element);
        ESP_LOGI(TAG, "Got ALC element:%p for stream %p", element, stream->stream_handle);
        if (element) {
            float gain = 1.0;
            for (int i = 0; i < channel; i++) {
                esp_gmf_alc_set_gain(element, i, gain);
            }
        }
        stream->is_open = true;
    }
    return ret;
}

static int music_data_hdlr(uint8_t *pcm, uint32_t len, void *ctx)
{
    render_stream_t *stream = (render_stream_t*)ctx;
    if (stream->finished) {
        return -1;
    }
    // Write stream data
    return esp_audio_render_stream_write(stream->stream_handle, pcm, len);
}

static int music_exited_hdlr(void *ctx)
{
    render_stream_t *stream = (render_stream_t*)ctx;
    stream->exited = true;
    return 0;
}

static int render_out_writer(uint8_t *pcm_data, uint32_t pcm_size, void *ctx)
{
    esp_codec_dev_handle_t codec_dev = (esp_codec_dev_handle_t)ctx;
    esp_codec_dev_write(codec_dev, pcm_data, pcm_size);
    return 0;
}

static int simple_audio_render_run(const char *url, esp_codec_dev_handle_t codec_dev, int duration)
{
    // Create GMF pool
    esp_gmf_pool_handle_t pool = NULL;
    create_default_pool(&pool);

    // Create audio render
    esp_audio_render_handle_t render = NULL;
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = 1,
        .out_writer = render_out_writer,
        .out_ctx = codec_dev,
        .pool = pool,
    };
    esp_audio_render_create(&cfg, &render);

    // Set render output fixed sample information and open devices
    // If codec device is kept open, no need open too often
    esp_audio_render_sample_info_t info = { .sample_rate = 16000, .bits_per_sample = 16, .channel = 2 };
    esp_audio_render_set_out_sample_info(render, &info);
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = info.sample_rate,
        .bits_per_sample = info.bits_per_sample,
        .channel = info.channel,
    };
    esp_codec_dev_open(codec_dev, &fs);

    render_stream_t first_stream = {};
    esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_FIRST_STREAM, &first_stream.stream_handle);

    // For there is only one stream post stream is same as input stream
    esp_audio_render_proc_type_t procs[] = { ESP_AUDIO_RENDER_PROC_ALC };
    esp_audio_render_stream_add_proc(first_stream.stream_handle, procs, sizeof(procs)/sizeof(procs[0]));

    // Decode async
    int ret = decode_pcm(url, music_info_hdlr, music_data_hdlr, music_exited_hdlr, &first_stream, 0);
    if (ret == 0) {
        uint32_t start = esp_timer_get_time() / 1000;
        uint32_t cur = start;
        while (cur < start + duration && first_stream.exited == false) {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            cur = esp_timer_get_time() / 1000;
        }
        first_stream.finished = true;
        while (!first_stream.exited) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
    }

    // Clear up
    if (first_stream.is_open) {
        esp_audio_render_stream_close(first_stream.stream_handle);
    }
    esp_audio_render_destroy(render);
    esp_codec_dev_close(codec_dev);
    esp_gmf_pool_deinit(pool);
    return 0;
}

static bool all_stream_exited(render_stream_t* stream, uint8_t src_num)
{
    for (int i = 0; i < src_num; i++) {
        if (stream[i].exited == false) {
            return false;
        }
    }
    return true;
}

static int audio_render_with_mixer_run(const char *url[], int src_num, esp_codec_dev_handle_t codec_dev, int duration)
{
    esp_gmf_pool_handle_t pool = NULL;
    create_default_pool(&pool);

    esp_audio_render_handle_t render = NULL;
    esp_audio_render_cfg_t cfg = {
        .max_stream_num = src_num,
        .out_writer = render_out_writer,
        .out_ctx = codec_dev,
        .pool = pool,
    };
    esp_audio_render_create(&cfg, &render);

    // Set render output fixed sample information and open devices
    // If codec device is kept open, no need open too often
    esp_audio_render_sample_info_t fixed = { .sample_rate = 16000, .bits_per_sample = 16, .channel = 2 };
    esp_audio_render_set_out_sample_info(render, &fixed);
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = fixed.sample_rate,
        .bits_per_sample = fixed.bits_per_sample,
        .channel = fixed.channel,
    };
    esp_codec_dev_open(codec_dev, &fs);

    // Add ALC into post stream
    esp_audio_render_proc_type_t post_procs[] = { ESP_AUDIO_RENDER_PROC_ALC };
    esp_audio_render_add_mixed_proc(render, post_procs, sizeof(post_procs)/sizeof(post_procs[0]));

    render_stream_t stream[src_num];
    memset(&stream, 0, sizeof(render_stream_t) * src_num);
    for (int i = 0; i < src_num; i++) {
        // Get stream handle
        esp_audio_render_stream_get(render, ESP_AUDIO_RENDER_STREAM_ID(i), &stream[i].stream_handle);
        // Add ALC into each stream
        esp_audio_render_proc_type_t procs[] = { ESP_AUDIO_RENDER_PROC_ALC };
        esp_audio_render_stream_add_proc(stream[i].stream_handle, procs, sizeof(procs)/sizeof(procs[0]));
        uint8_t core = i % 2;
        int ret = decode_pcm(url[i], music_info_hdlr, music_data_hdlr, music_exited_hdlr, &stream[i], core);
        if (ret != 0) {
            stream[i].exited = true;
        }
    }

    uint32_t start = esp_timer_get_time() / 1000;
    uint32_t cur = start;
    while (cur < start + duration) {
        //vTaskDelay(1000 / portTICK_PERIOD_MS);
        esp_gmf_oal_sys_get_real_time_stats(5000, false);
        cur = esp_timer_get_time() / 1000;
        if (all_stream_exited(stream, src_num)) {
            break;
        }
    }
    // Force stream to exited
    for (int i = 0; i < src_num; i++) {
        stream[i].finished = true;
    }
    // Wait for all stream exited
    while (all_stream_exited(stream, src_num) == false) {
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    // Clear up
    for (int i = 0; i < src_num; i++) {
        if (stream[i].is_open) {
            esp_audio_render_stream_close(stream[i].stream_handle);
        }
    }
    esp_audio_render_destroy(render);
    esp_codec_dev_close(codec_dev);
    esp_gmf_pool_deinit(pool);
    return 0;
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);
    // Prepare for codec device and wifi
    esp_gmf_app_setup_codec_dev(NULL);
    esp_codec_dev_set_out_vol(esp_gmf_app_get_playback_handle(), 70);
    esp_codec_dev_close(esp_gmf_app_get_playback_handle());
    esp_codec_dev_close(esp_gmf_app_get_record_handle());
    esp_gmf_app_wifi_connect();
    esp_audio_dec_register_default();

    simple_audio_render_run(AUDIO_RENDER_FIRST_URL, esp_gmf_app_get_playback_handle(), AUDIO_RENDER_TEST_DURATION);

    const char *mixed_urls[] = {
        AUDIO_RENDER_FIRST_URL,
        AUDIO_RENDER_SECOND_URL,
        AUDIO_RENDER_THIRD_URL,
        AUDIO_RENDER_FOURTH_URL,
        AUDIO_RENDER_FIRST_URL,
        AUDIO_RENDER_SECOND_URL,
        AUDIO_RENDER_THIRD_URL,
        AUDIO_RENDER_FOURTH_URL,
    };
    audio_render_with_mixer_run(mixed_urls, sizeof(mixed_urls)/sizeof(mixed_urls[0]),
                                esp_gmf_app_get_playback_handle(), AUDIO_RENDER_TEST_DURATION);
    esp_audio_dec_unregister_default();
    ESP_LOGI(TAG, "Audio render test finished");
}
