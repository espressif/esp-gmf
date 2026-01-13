/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_audio_types.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_element.h"
#include "esp_board_manager_includes.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_io_embed_flash.h"
#include "esp_gmf_new_databus.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_port.h"
#include "gmf_loader_setup_defaults.h"
#include "esp_embed_tone.h"
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_ALC)
#include "esp_gmf_alc.h"
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_ALC) */
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_EQ)
#include "esp_gmf_eq.h"
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_EQ) */
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_FADE)
#include "esp_gmf_fade.h"
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_FADE) */
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_SONIC)
#include "esp_gmf_sonic.h"
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_SONIC) */
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_DRC)
#include "esp_gmf_drc.h"
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_DRC) */
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MBC)
#include "esp_gmf_mbc.h"
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MBC) */
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MIXER)
#include "esp_gmf_mixer.h"
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MIXER) */

#define TAG                     "PIPELINE_AUDIO_EFFECTS"
#define PIPELINE_STOP_BIT       (BIT(0))
#define EFFECT_SAMPLE_RATE      (48000)
#define EFFECT_CHANNELS         (2)
#define EFFECT_BITS             (16)
#define PLAYBACK_DEFAULT_VOLUME 80

typedef struct {
    esp_gmf_element_handle_t  mixer_el;
    EventGroupHandle_t        evt_ctx;
} mixer_demo_ctx_t;

static esp_codec_dev_handle_t playback_handle = NULL;

static esp_err_t pipeline_event_handler(esp_gmf_event_pkt_t *event, void *ctx)
{
    if ((event == NULL) || (ctx == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGD(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%x, sub:%s, payload:%p, size:%d, ctx:%p",
             OBJ_GET_TAG(event->from), event->from, event->type,
             esp_gmf_event_get_state_str(event->sub), event->payload,
             event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_STOP_BIT);
    }
    return ESP_OK;
}

#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MIXER)
static esp_err_t tone_pipeline_event_handler(esp_gmf_event_pkt_t *event, void *ctx)
{
    if ((event == NULL) || (ctx == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    mixer_demo_ctx_t *mixer_demo_ctx = (mixer_demo_ctx_t *)ctx;
    ESP_LOGD(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%x, sub:%s, payload:%p, size:%d, ctx:%p",
             OBJ_GET_TAG(event->from), event->from, event->type,
             esp_gmf_event_get_state_str(event->sub), event->payload,
             event->payload_size, ctx);
    if (event->sub == ESP_GMF_EVENT_STATE_OPENING) {
        for (int i = 0; i < CONFIG_GMF_AUDIO_EFFECT_MIXER_SRC_NUM; i++) {
            esp_gmf_mixer_set_mode(mixer_demo_ctx->mixer_el, i, ESP_AE_MIXER_MODE_FADE_DOWNWARD);
        }
    }
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        for (int i = 0; i < CONFIG_GMF_AUDIO_EFFECT_MIXER_SRC_NUM; i++) {
            esp_gmf_mixer_set_mode(mixer_demo_ctx->mixer_el, i, ESP_AE_MIXER_MODE_FADE_UPWARD);
        }
    }
    return ESP_OK;
}
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MIXER) */

static int playback_peripheral_init(void)
{
    int ret = ESP_OK;
    ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to init audio DAC");
    dev_audio_codec_handles_t *play_dev_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&play_dev_handle);
    ESP_GMF_NULL_CHECK(TAG, play_dev_handle, return ESP_GMF_ERR_NOT_FOUND);
    playback_handle = play_dev_handle->codec_dev;
    ESP_GMF_NULL_CHECK(TAG, playback_handle, return ESP_GMF_ERR_NOT_FOUND);
    ret = esp_codec_dev_set_out_vol(playback_handle, PLAYBACK_DEFAULT_VOLUME);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to set output volume");
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = EFFECT_SAMPLE_RATE,
        .channel = EFFECT_CHANNELS,
        .bits_per_sample = EFFECT_BITS,
    };
    ret = esp_codec_dev_open(playback_handle, &fs);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to open playback codec");
    return ESP_OK;
}

static int playback_peripheral_deinit(esp_codec_dev_handle_t play_handle)
{
    int ret = ESP_OK;
    if (play_handle != NULL) {
        ret = esp_codec_dev_close(play_handle);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to close playback codec");
    }
    ret = esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to deinit audio DAC");
    return ESP_OK;
}

static esp_gmf_err_t reconfig_decoder_by_uri(esp_gmf_pipeline_handle_t pipe, const char *uri)
{
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_err_t ret = esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to get decoder element");
    esp_gmf_info_sound_t dec_info = {0};
    ret = esp_gmf_audio_helper_get_audio_type_by_uri(uri, &dec_info.format_id);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to get audio type by URI");
    return esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &dec_info);
}

static void apply_effect_parameter(esp_gmf_element_handle_t effect_el, const char *effect_name)
{
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_ALC)
    if (strcmp(effect_name, "aud_alc") == 0) {
        ret = esp_gmf_alc_set_gain(effect_el, 0, -12);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to set ALC gain");
    }
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_ALC) */
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_SONIC)
    if (strcmp(effect_name, "aud_sonic") == 0) {
        ret = esp_gmf_sonic_set_speed(effect_el, 0.8f);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to set Sonic speed");
        ret = esp_gmf_sonic_set_pitch(effect_el, 1.2f);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to set Sonic pitch");
    }
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_SONIC) */
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_FADE)
    if (strcmp(effect_name, "aud_fade") == 0) {
        esp_ae_fade_mode_t mode;
        ret = esp_gmf_fade_get_mode(effect_el, &mode);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to get Fade mode");
        mode = (mode == ESP_AE_FADE_MODE_FADE_IN) ? ESP_AE_FADE_MODE_FADE_OUT : ESP_AE_FADE_MODE_FADE_IN;
        ret = esp_gmf_fade_set_mode(effect_el, mode);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to set Fade mode");
    }
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_FADE) */
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_EQ)
    if (strcmp(effect_name, "aud_eq") == 0) {
        for (int i = 0; i < CONFIG_GMF_EQ_FILTER_NUM; i++) {
            esp_ae_eq_filter_para_t para = {0};
            esp_gmf_err_t ret = esp_gmf_eq_get_para(effect_el, i, &para);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to get EQ para");
            para.gain = -6.0f;
            ret = esp_gmf_eq_set_para(effect_el, i, &para);
            ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to set EQ gain");
        }
    }
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_EQ) */
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_DRC)
    if (strcmp(effect_name, "aud_drc") == 0) {
        esp_ae_drc_curve_point points[3] = {
            {.x = 0, .y = -10},
            {.x = -40, .y = -40},
            {.x = -100, .y = -100},
        };
        ret = esp_gmf_drc_set_points(effect_el, points, 3);
        ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to set DRC points");
    }
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_DRC) */
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MBC)
    if (strcmp(effect_name, "aud_mbc") == 0) {
        for (int band = 0; band < 4; band++) {
            ret = esp_gmf_mbc_set_bypass(effect_el, band, false);
            ESP_GMF_RET_ON_ERROR(TAG, ret, return, "Failed to enable MBC band %d", band);
        }
    }
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MBC) */
}

#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MIXER)
static void run_mixer_demo(void)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);

    ESP_LOGI(TAG, "[ 1 ] Prepare GMF pool");
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    gmf_loader_setup_io_default(pool);
    gmf_loader_setup_audio_codec_default(pool);
    gmf_loader_setup_audio_effects_default(pool);

    ESP_LOGI(TAG, "[ 2 ] Build mixer pipelines");
    esp_gmf_pipeline_handle_t pipe_music = NULL;
    const char *music_chain[] = {"aud_dec"};
    esp_gmf_pool_new_pipeline(pool, "io_embed_flash", music_chain, sizeof(music_chain) / sizeof(music_chain[0]),
                              NULL, &pipe_music);

    esp_gmf_pipeline_handle_t pipe_tone = NULL;
    const char *tone_chain[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    esp_gmf_pool_new_pipeline(pool, "io_embed_flash", tone_chain, sizeof(tone_chain) / sizeof(tone_chain[0]),
                              NULL, &pipe_tone);

    esp_gmf_pipeline_handle_t pipe_mix = NULL;
    const char *mix_chain[] = {"aud_mixer"};
    esp_gmf_pool_new_pipeline(pool, NULL, mix_chain, sizeof(mix_chain) / sizeof(mix_chain[0]),
                              "io_codec_dev", &pipe_mix);
    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(pipe_mix), playback_handle);

    ESP_LOGI(TAG, "[ 2.1 ] Set audio url to play for music pipeline");
    esp_gmf_io_handle_t music_in = NULL;
    esp_gmf_pipeline_get_in(pipe_music, &music_in);
    esp_gmf_io_embed_flash_set_context(music_in, (embed_item_info_t *)&g_esp_embed_tone[0], 2);
    esp_gmf_pipeline_set_in_uri(pipe_music, esp_embed_tone_url[ESP_EMBED_TONE_MANLOUD_48000_2_16_10_WAV]);

    esp_gmf_err_t ret = reconfig_decoder_by_uri(pipe_music, esp_embed_tone_url[ESP_EMBED_TONE_MANLOUD_48000_2_16_10_WAV]);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to reconfig music decoder");

    esp_gmf_io_handle_t tone_in = NULL;
    esp_gmf_pipeline_get_in(pipe_tone, &tone_in);
    esp_gmf_io_embed_flash_set_context(tone_in, (embed_item_info_t *)&g_esp_embed_tone[0], 2);
    esp_gmf_pipeline_set_in_uri(pipe_tone, esp_embed_tone_url[ESP_EMBED_TONE_TONE_MP3]);

    ret = reconfig_decoder_by_uri(pipe_tone, esp_embed_tone_url[ESP_EMBED_TONE_TONE_MP3]);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to reconfig tone decoder");

    esp_gmf_element_handle_t mixer_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_mix, "aud_mixer", &mixer_el);
    for (int i = 0; i < CONFIG_GMF_AUDIO_EFFECT_MIXER_SRC_NUM; i++) {
        esp_gmf_mixer_set_mode(mixer_el, i, ESP_AE_MIXER_MODE_FADE_UPWARD);
    }

    ESP_LOGI(TAG, "[ 2.2 ] Connect pipelines via ring buffers");
    esp_gmf_port_handle_t out_port = NULL;
    esp_gmf_port_handle_t in_port = NULL;
    esp_gmf_db_handle_t rb_music = NULL;
    esp_gmf_db_handle_t rb_tone = NULL;
    esp_gmf_db_new_ringbuf(10, 1024, &rb_music);
    esp_gmf_db_new_ringbuf(10, 1024, &rb_tone);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write,
                                         esp_gmf_db_deinit, rb_music, 4096, ESP_GMF_MAX_DELAY);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read,
                                       esp_gmf_db_deinit, rb_music, 4096, 5);
    esp_gmf_pipeline_connect_pipe(pipe_music, "aud_dec", out_port, pipe_mix, "aud_mixer", in_port);

    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write,
                                         esp_gmf_db_deinit, rb_tone, 4096, ESP_GMF_MAX_DELAY);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read,
                                       esp_gmf_db_deinit, rb_tone, 4096, 0);
    esp_gmf_pipeline_connect_pipe(pipe_tone, "aud_bit_cvt", out_port, pipe_mix, "aud_mixer", in_port);

    ESP_LOGI(TAG, "[ 2.3 ] Prepare tasks");
    esp_gmf_task_handle_t music_task = NULL;
    esp_gmf_task_handle_t tone_task = NULL;
    esp_gmf_task_handle_t mix_task = NULL;
    esp_gmf_task_cfg_t cfg1 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg1.thread.core = 1;
    cfg1.thread.prio = 10;
    cfg1.name = "base_stream";
    esp_gmf_task_init(&cfg1, &music_task);
    esp_gmf_pipeline_bind_task(pipe_music, music_task);
    esp_gmf_pipeline_loading_jobs(pipe_music);

    esp_gmf_task_cfg_t cfg2 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg2.thread.core = 0;
    cfg2.thread.prio = 10;
    cfg2.name = "tone_stream";
    esp_gmf_task_init(&cfg2, &tone_task);
    esp_gmf_pipeline_bind_task(pipe_tone, tone_task);
    esp_gmf_pipeline_loading_jobs(pipe_tone);

    esp_gmf_task_cfg_t cfg3 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg3.thread.core = 0;
    cfg3.thread.prio = 11;
    cfg3.name = "mixer_stream";
    esp_gmf_task_init(&cfg3, &mix_task);
    esp_gmf_pipeline_bind_task(pipe_mix, mix_task);
    esp_gmf_pipeline_loading_jobs(pipe_mix);

    ESP_LOGI(TAG, "[ 2.4 ] Create event groups and set event handler for each pipeline");
    EventGroupHandle_t evt_music = xEventGroupCreate();
    EventGroupHandle_t evt_tone = xEventGroupCreate();
    EventGroupHandle_t evt_mix = xEventGroupCreate();
    static mixer_demo_ctx_t mixer_demo_ctx = {0};
    mixer_demo_ctx.mixer_el = mixer_el;
    mixer_demo_ctx.evt_ctx = evt_tone;
    esp_gmf_pipeline_set_event(pipe_music, pipeline_event_handler, evt_music);
    esp_gmf_pipeline_set_event(pipe_tone, tone_pipeline_event_handler, &mixer_demo_ctx);
    esp_gmf_pipeline_set_event(pipe_mix, pipeline_event_handler, evt_mix);

    esp_gmf_info_sound_t info = {
        .sample_rates = EFFECT_SAMPLE_RATE,
        .bits = EFFECT_BITS,
        .channels = EFFECT_CHANNELS,
    };
    esp_gmf_pipeline_report_info(pipe_mix, ESP_GMF_INFO_SOUND, &info, sizeof(info));
    ESP_LOGI(TAG, "[ 3 ] Start mixer pipelines");
    esp_gmf_pipeline_run(pipe_music);
    esp_gmf_pipeline_run(pipe_mix);
    // Delay 2s to let music and mixer start first, then insert tone stream to hear the mixing effect
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_gmf_pipeline_run(pipe_tone);

    xEventGroupWaitBits(evt_music, PIPELINE_STOP_BIT, pdTRUE, pdFALSE, portMAX_DELAY);

    esp_gmf_pipeline_stop(pipe_tone);
    esp_gmf_pipeline_stop(pipe_music);
    esp_gmf_pipeline_stop(pipe_mix);

    ESP_LOGI(TAG, "[ 4 ] Tear down mixer pipelines");
    esp_gmf_task_deinit(music_task);
    esp_gmf_pipeline_destroy(pipe_music);
    esp_gmf_task_deinit(tone_task);
    esp_gmf_pipeline_destroy(pipe_tone);
    esp_gmf_task_deinit(mix_task);
    esp_gmf_pipeline_destroy(pipe_mix);

    vEventGroupDelete(evt_music);
    vEventGroupDelete(evt_tone);
    vEventGroupDelete(evt_mix);

    gmf_loader_teardown_audio_effects_default(pool);
    gmf_loader_teardown_audio_codec_default(pool);
    gmf_loader_teardown_io_default(pool);
    esp_gmf_pool_deinit(pool);
    ESP_LOGI(TAG, "Mixer demo finished");
}
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MIXER) */

static void run_single_effect_demo(const char *effect_name)
{
    esp_log_level_set(TAG, ESP_LOG_INFO);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;

    ESP_LOGI(TAG, "[ 1 ] Prepare GMF pool");
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    gmf_loader_setup_io_default(pool);
    gmf_loader_setup_audio_codec_default(pool);
    gmf_loader_setup_audio_effects_default(pool);

    ESP_LOGI(TAG, "[ 2 ] Build pipeline: io_embed_flash -> aud_dec -> effect -> io_codec_dev");

    const char *effect_chain[] = {"aud_dec", effect_name};
    esp_gmf_pipeline_handle_t pipe = NULL;
    esp_gmf_pool_new_pipeline(pool, "io_embed_flash", effect_chain, sizeof(effect_chain) / sizeof(effect_chain[0]),
                              "io_codec_dev", &pipe);
    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(pipe), playback_handle);

    ESP_LOGI(TAG, "[ 2.1 ] Set audio url to play");
    esp_gmf_pipeline_set_in_uri(pipe, esp_embed_tone_url[ESP_EMBED_TONE_MANLOUD_48000_2_16_10_WAV]);
    esp_gmf_io_handle_t in_io = NULL;
    esp_gmf_pipeline_get_in(pipe, &in_io);
    esp_gmf_io_embed_flash_set_context(in_io, (embed_item_info_t *)&g_esp_embed_tone[0], ESP_EMBED_TONE_URL_MAX);

    ret = reconfig_decoder_by_uri(pipe, esp_embed_tone_url[ESP_EMBED_TONE_MANLOUD_48000_2_16_10_WAV]);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to reconfig decoder");

    ESP_LOGI(TAG, "[ 2.2 ] Create gmf task, bind task to pipeline and load linked element jobs to the bind task");
    esp_gmf_task_cfg_t task_cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_init(&task_cfg, &work_task);
    esp_gmf_pipeline_bind_task(pipe, work_task);
    esp_gmf_pipeline_loading_jobs(pipe);
    EventGroupHandle_t pipe_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_evt, return);
    esp_gmf_pipeline_set_event(pipe, pipeline_event_handler, pipe_evt);

    esp_gmf_element_handle_t effect_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, effect_name, &effect_el);
#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MBC)
    if (strcmp(effect_name, "aud_mbc") == 0) {
        for (int band = 0; band < 4; band++) {
            esp_gmf_mbc_set_bypass(effect_el, band, true);
        }
    }
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MBC) */
    ESP_LOGI(TAG, "[ 3 ] Start playback and wait 4 s before enabling %s", effect_name);
    esp_gmf_pipeline_run(pipe);
    // Wait 4 seconds to play original audio first, then apply effect parameters to make the change audible
    vTaskDelay(pdMS_TO_TICKS(4000));
    ESP_LOGI(TAG, "Applying %s parameters", effect_name);
    apply_effect_parameter(effect_el, effect_name);

    xEventGroupWaitBits(pipe_evt, PIPELINE_STOP_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    esp_gmf_pipeline_stop(pipe);

    ESP_LOGI(TAG, "[ 4 ] Tear down pipeline");
    esp_gmf_task_deinit(work_task);
    esp_gmf_pipeline_destroy(pipe);
    vEventGroupDelete(pipe_evt);
    gmf_loader_teardown_audio_effects_default(pool);
    gmf_loader_teardown_audio_codec_default(pool);
    gmf_loader_teardown_io_default(pool);
    esp_gmf_pool_deinit(pool);
}

void app_main(void)
{
    int ret = playback_peripheral_init();
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return, "Failed to init playback peripheral");

#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MIXER)
    run_mixer_demo();
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MIXER) */

#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_ALC)
    run_single_effect_demo("aud_alc");
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_ALC) */

#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_SONIC)
    run_single_effect_demo("aud_sonic");
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_SONIC) */

#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_EQ)
    run_single_effect_demo("aud_eq");
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_EQ) */

#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_FADE)
    run_single_effect_demo("aud_fade");
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_FADE) */

#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_DRC)
    run_single_effect_demo("aud_drc");
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_DRC) */

#if defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MBC)
    run_single_effect_demo("aud_mbc");
#endif  /* defined(CONFIG_GMF_AUDIO_EFFECT_INIT_MBC) */

    playback_peripheral_deinit(playback_handle);
    ESP_LOGI(TAG, "Effect demo finished");
}
