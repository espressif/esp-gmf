/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_board_manager_includes.h"
#include "esp_codec_dev.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_audio_param.h"
#include "esp_gmf_element.h"
#include "esp_gmf_howl.h"
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_io_file.h"
#include "esp_gmf_mixer.h"
#include "esp_gmf_new_databus.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_port.h"
#include "gmf_loader_setup_defaults.h"

#define PIPELINE_FINISH_BIT      BIT(0)
#define PIPELINE_ERROR_BIT       BIT(1)
#define PLAYBACK_DEFAULT_VOLUME  80
#define RECORD_DEFAULT_GAIN      32
#define HOWL_SAMPLE_RATE         16000
#define HOWL_CHANNELS            1
#define HOWL_BITS                16
#define HOWL_MIXER_SRC_NUM       2
#define HOWL_MUSIC_URI           "/sdcard/test.mp3"

static const char *TAG = "PIPELINE_HOWL";

static bool is_lyrat_mini_v1_1(void)
{
    esp_board_info_t board_info = {0};
    if (esp_board_manager_get_board_info(&board_info) != ESP_OK || board_info.name == NULL) {
        return false;
    }
    return strcmp(board_info.name, "lyrat_mini_v1_1") == 0;
}

static esp_err_t pipeline_event_handler(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGI(TAG, "CB: RECV Pipeline EVT: el: %s-%p, type: %x, sub: %s, payload: %p, size: %d, ctx:%p",
             OBJ_GET_TAG(event->from), event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    EventGroupHandle_t evt = (EventGroupHandle_t)ctx;
    if (event->sub == ESP_GMF_EVENT_STATE_FINISHED) {
        xEventGroupSetBits(evt, PIPELINE_FINISH_BIT);
    } else if (event->sub == ESP_GMF_EVENT_STATE_ERROR) {
        xEventGroupSetBits(evt, PIPELINE_ERROR_BIT);
    }
    return ESP_GMF_ERR_OK;
}

static int playback_peripheral_init(esp_codec_dev_handle_t *out_handle)
{
    int ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init audio DAC");
        return ret;
    }
    dev_audio_codec_handles_t *play_dev_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&play_dev_handle);
    if (play_dev_handle == NULL || play_dev_handle->codec_dev == NULL) {
        ESP_LOGE(TAG, "Failed to get playback handle");
        return ESP_ERR_NOT_FOUND;
    }
    *out_handle = play_dev_handle->codec_dev;
    ret = esp_codec_dev_set_out_vol(*out_handle, PLAYBACK_DEFAULT_VOLUME);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set output volume");
        return ret;
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = HOWL_SAMPLE_RATE,
        .channel = HOWL_CHANNELS,
        .bits_per_sample = HOWL_BITS,
    };
    ret = esp_codec_dev_open(*out_handle, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open playback codec");
        return ret;
    }
    return ESP_OK;
}

static int record_peripheral_init(esp_codec_dev_handle_t *in_handle)
{
    int ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init audio ADC");
        return ret;
    }
    dev_audio_codec_handles_t *rec_dev_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&rec_dev_handle);
    if (rec_dev_handle == NULL || rec_dev_handle->codec_dev == NULL) {
        ESP_LOGE(TAG, "Failed to get record handle");
        return ESP_ERR_NOT_FOUND;
    }
    *in_handle = rec_dev_handle->codec_dev;
    ret = esp_codec_dev_set_in_gain(*in_handle, RECORD_DEFAULT_GAIN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set microphone gain");
        return ret;
    }
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = HOWL_SAMPLE_RATE,
        .channel = HOWL_CHANNELS,
        .bits_per_sample = HOWL_BITS,
    };
    if (is_lyrat_mini_v1_1() && fs.channel == 1) {
        fs.channel = 2;
        fs.channel_mask = 0x02;
    }
    ret = esp_codec_dev_open(*in_handle, &fs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open record codec");
        return ret;
    }
    return ESP_OK;
}

static void playback_peripheral_deinit(esp_codec_dev_handle_t handle)
{
    if (handle != NULL) {
        esp_codec_dev_close(handle);
    }
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_DAC);
}

static void record_peripheral_deinit(esp_codec_dev_handle_t handle)
{
    if (handle != NULL) {
        esp_codec_dev_close(handle);
    }
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_AUDIO_ADC);
}

static esp_gmf_err_t reconfig_decoder_by_uri(esp_gmf_pipeline_handle_t pipe, const char *uri)
{
    esp_gmf_element_handle_t dec_el = NULL;
    esp_gmf_err_t ret = esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to get decoder element");
    esp_gmf_info_sound_t dec_info = {0};
    ret = esp_gmf_audio_helper_get_audio_type_by_uri(uri, &dec_info.format_id);
    ESP_GMF_RET_ON_NOT_OK(TAG, ret, return ret, "Failed to parse uri format");
    return esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &dec_info);
}

void app_main(void)
{
    esp_log_level_set("*", ESP_LOG_INFO);

    esp_codec_dev_handle_t playback_handle = NULL;
    esp_codec_dev_handle_t record_handle = NULL;
    ESP_LOGI(TAG, "[ 1 ] Init peripherals");
    playback_peripheral_init(&playback_handle);
    record_peripheral_init(&record_handle);
    esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    ESP_LOGI(TAG, "[ 2 ] Register pool");
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    gmf_loader_setup_io_default(pool);
    gmf_loader_setup_audio_codec_default(pool);
    gmf_loader_setup_audio_effects_default(pool);
    ESP_GMF_POOL_SHOW_ITEMS(pool);

    ESP_LOGI(TAG, "[ 3 ] Build pipelines");
    esp_gmf_pipeline_handle_t pipe_music = NULL;
    esp_gmf_pipeline_handle_t pipe_mic = NULL;
    esp_gmf_pipeline_handle_t pipe_mix = NULL;
    const char *music_chain[] = {"aud_dec", "aud_rate_cvt", "aud_bit_cvt", "aud_ch_cvt"};
    const char *mic_chain[] = {"aud_howl"};
    const char *mix_chain[] = {"aud_mixer"};
    esp_gmf_pool_new_pipeline(pool, "io_file", music_chain, sizeof(music_chain) / sizeof(music_chain[0]),
                              NULL, &pipe_music);
    esp_gmf_pool_new_pipeline(pool, "io_codec_dev", mic_chain, sizeof(mic_chain) / sizeof(mic_chain[0]),
                              NULL, &pipe_mic);
    esp_gmf_pool_new_pipeline(pool, NULL, mix_chain, sizeof(mix_chain) / sizeof(mix_chain[0]),
                              "io_codec_dev", &pipe_mix);

    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_IN_INSTANCE(pipe_mic), record_handle);
    esp_gmf_io_codec_dev_set_dev(ESP_GMF_PIPELINE_GET_OUT_INSTANCE(pipe_mix), playback_handle);

    ESP_LOGI(TAG, "[ 3.1 ] Config music source and decoder");
    const char *uri = HOWL_MUSIC_URI;
    esp_gmf_pipeline_set_in_uri(pipe_music, uri);
    reconfig_decoder_by_uri(pipe_music, uri);

    esp_gmf_element_handle_t rate_cvt = NULL;
    esp_gmf_element_handle_t bit_cvt = NULL;
    esp_gmf_element_handle_t ch_cvt = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_music, "aud_rate_cvt", &rate_cvt);
    esp_gmf_pipeline_get_el_by_name(pipe_music, "aud_bit_cvt", &bit_cvt);
    esp_gmf_pipeline_get_el_by_name(pipe_music, "aud_ch_cvt", &ch_cvt);
    esp_gmf_audio_param_set_dest_rate(rate_cvt, HOWL_SAMPLE_RATE);
    esp_gmf_audio_param_set_dest_bits(bit_cvt, HOWL_BITS);
    esp_gmf_audio_param_set_dest_ch(ch_cvt, HOWL_CHANNELS);

    ESP_LOGI(TAG, "[ 3.2 ] Config howl parameters");
    esp_gmf_element_handle_t howl_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_mic, "aud_howl", &howl_el);
    esp_ae_howl_cfg_t *howl_cfg = (esp_ae_howl_cfg_t *)OBJ_GET_CFG(howl_el);
    ESP_GMF_NULL_CHECK(TAG, howl_cfg, return);
    howl_cfg->papr_th = (float)(CONFIG_GMF_AUDIO_EFFECT_HOWL_PAPR_X10 / 10);
    howl_cfg->phpr_th = (float)(CONFIG_GMF_AUDIO_EFFECT_HOWL_PHPR_X10 / 10);
    howl_cfg->pnpr_th = (float)(CONFIG_GMF_AUDIO_EFFECT_HOWL_PNPR_X10 / 10);
    howl_cfg->imsd_th = (float)(CONFIG_GMF_AUDIO_EFFECT_HOWL_IMSD_X10 / 10);
    howl_cfg->enable_imsd = CONFIG_GMF_AUDIO_EFFECT_HOWL_ENABLE_IMSD;

    ESP_LOGI(TAG, "[ 3.3 ] Config mixer parameters");
    esp_gmf_element_handle_t mixer_el = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe_mix, "aud_mixer", &mixer_el);
    esp_ae_mixer_cfg_t *mixer_cfg = (esp_ae_mixer_cfg_t *)OBJ_GET_CFG(mixer_el);
    ESP_GMF_NULL_CHECK(TAG, mixer_cfg, return);
    if ((mixer_cfg->src_info == NULL) || (mixer_cfg->src_num < HOWL_MIXER_SRC_NUM)) {
        esp_gmf_oal_free(mixer_cfg->src_info);
        mixer_cfg->src_info = esp_gmf_oal_calloc(HOWL_MIXER_SRC_NUM, sizeof(esp_ae_mixer_info_t));
        ESP_GMF_NULL_CHECK(TAG, mixer_cfg->src_info, return);
        mixer_cfg->src_num = HOWL_MIXER_SRC_NUM;
    }
    mixer_cfg->src_info[0].weight1 = (float)(CONFIG_GMF_AUDIO_EFFECT_MIXER_SRC1_WEIGHT1 / 100);
    mixer_cfg->src_info[0].weight2 = (float)(CONFIG_GMF_AUDIO_EFFECT_MIXER_SRC1_WEIGHT2 / 100);
    mixer_cfg->src_info[0].transit_time = CONFIG_GMF_AUDIO_EFFECT_MIXER_SRC1_TRANSIT_TIME;
    mixer_cfg->src_info[1].weight1 = (float)(CONFIG_GMF_AUDIO_EFFECT_MIXER_SRC2_WEIGHT1 / 100);
    mixer_cfg->src_info[1].weight2 = (float)(CONFIG_GMF_AUDIO_EFFECT_MIXER_SRC2_WEIGHT2 / 100);
    mixer_cfg->src_info[1].transit_time = CONFIG_GMF_AUDIO_EFFECT_MIXER_SRC2_TRANSIT_TIME;
    esp_gmf_mixer_set_mode(mixer_el, 0, ESP_AE_MIXER_MODE_FADE_UPWARD);
    esp_gmf_mixer_set_mode(mixer_el, 1, ESP_AE_MIXER_MODE_FADE_UPWARD);

    ESP_LOGI(TAG, "[ 3.4 ] Connect music/mic pipelines to mixer");
    esp_gmf_port_handle_t out_port = NULL;
    esp_gmf_port_handle_t in_port = NULL;
    esp_gmf_db_handle_t rb_music = NULL;
    esp_gmf_db_handle_t rb_mic = NULL;
    esp_gmf_db_new_ringbuf(10, 1024, &rb_music);
    esp_gmf_db_new_ringbuf(10, 1024, &rb_mic);

    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write,
                                         esp_gmf_db_deinit, rb_music, 4096, ESP_GMF_MAX_DELAY);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read,
                                       esp_gmf_db_deinit, rb_music, 4096, 1);
    esp_gmf_pipeline_connect_pipe(pipe_music, "aud_ch_cvt", out_port, pipe_mix, "aud_mixer", in_port);

    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write,
                                         esp_gmf_db_deinit, rb_mic, 4096, ESP_GMF_MAX_DELAY);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read,
                                       esp_gmf_db_deinit, rb_mic, 4096, 0);
    esp_gmf_pipeline_connect_pipe(pipe_mic, "aud_howl", out_port, pipe_mix, "aud_mixer", in_port);

    ESP_LOGI(TAG, "[ 4 ] Create tasks and load jobs");
    esp_gmf_task_handle_t task_music = NULL;
    esp_gmf_task_handle_t task_mic = NULL;
    esp_gmf_task_handle_t task_mix = NULL;
    esp_gmf_task_cfg_t cfg_music = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg_music.name = "howl_music";
    cfg_music.thread.core = 0;
    cfg_music.thread.prio = 10;
    esp_gmf_task_init(&cfg_music, &task_music);
    esp_gmf_pipeline_bind_task(pipe_music, task_music);
    esp_gmf_pipeline_loading_jobs(pipe_music);

    esp_gmf_task_cfg_t cfg_mic = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg_mic.name = "howl_mic";
    cfg_mic.thread.core = 1;
    cfg_mic.thread.prio = 10;
    esp_gmf_task_init(&cfg_mic, &task_mic);
    esp_gmf_pipeline_bind_task(pipe_mic, task_mic);
    esp_gmf_pipeline_loading_jobs(pipe_mic);

    esp_gmf_task_cfg_t cfg_mix = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg_mix.name = "howl_mix";
    cfg_mix.thread.core = 0;
    cfg_mix.thread.prio = 11;
    esp_gmf_task_init(&cfg_mix, &task_mix);
    esp_gmf_pipeline_bind_task(pipe_mix, task_mix);
    esp_gmf_pipeline_loading_jobs(pipe_mix);

    EventGroupHandle_t evt_music = xEventGroupCreate();
    EventGroupHandle_t evt_mic = xEventGroupCreate();
    EventGroupHandle_t evt_mix = xEventGroupCreate();

    esp_gmf_pipeline_set_event(pipe_music, pipeline_event_handler, evt_music);
    esp_gmf_pipeline_set_event(pipe_mic, pipeline_event_handler, evt_mic);
    esp_gmf_pipeline_set_event(pipe_mix, pipeline_event_handler, evt_mix);

    esp_gmf_info_sound_t audio_info = {
        .sample_rates = HOWL_SAMPLE_RATE,
        .channels = HOWL_CHANNELS,
        .bits = HOWL_BITS,
    };
    esp_gmf_pipeline_report_info(pipe_mic, ESP_GMF_INFO_SOUND, &audio_info, sizeof(audio_info));
    esp_gmf_pipeline_report_info(pipe_mix, ESP_GMF_INFO_SOUND, &audio_info, sizeof(audio_info));

    ESP_LOGI(TAG, "[ 5 ] Run howl pipelines");
    esp_gmf_pipeline_run(pipe_mix);
    esp_gmf_pipeline_run(pipe_music);
    esp_gmf_pipeline_run(pipe_mic);

    ESP_LOGI(TAG, "[ 6 ] Wait music end then stop all pipelines");
    EventBits_t bits = xEventGroupWaitBits(evt_music, PIPELINE_FINISH_BIT | PIPELINE_ERROR_BIT,
                                           pdTRUE, pdFALSE, portMAX_DELAY);
    bool demo_ok = ((bits & PIPELINE_FINISH_BIT) != 0) && ((bits & PIPELINE_ERROR_BIT) == 0);
    esp_gmf_pipeline_stop(pipe_music);
    esp_gmf_pipeline_stop(pipe_mic);
    esp_gmf_pipeline_stop(pipe_mix);

    ESP_LOGI(TAG, "[ 7 ] Destroy resources");
    esp_gmf_task_deinit(task_music);
    esp_gmf_task_deinit(task_mic);
    esp_gmf_task_deinit(task_mix);
    esp_gmf_pipeline_destroy(pipe_music);
    esp_gmf_pipeline_destroy(pipe_mic);
    esp_gmf_pipeline_destroy(pipe_mix);
    gmf_loader_teardown_audio_effects_default(pool);
    gmf_loader_teardown_audio_codec_default(pool);
    gmf_loader_teardown_io_default(pool);
    esp_gmf_pool_deinit(pool);
    esp_board_manager_deinit_device_by_name(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    playback_peripheral_deinit(playback_handle);
    record_peripheral_deinit(record_handle);

    if (demo_ok) {
        ESP_LOGI(TAG, "Howl demo finished");
    } else {
        if (bits & PIPELINE_ERROR_BIT) {
            ESP_LOGE(TAG, "Music pipeline reported error, abort demo");
        } else {
            ESP_LOGE(TAG, "Music pipeline did not finish normally, abort demo");
        }
    }
}
