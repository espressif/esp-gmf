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
#include "esp_gmf_pool.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_thread.h"

#include "esp_gmf_audio_dec.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_dec_reg.h"
#include "esp_gmf_new_databus.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_audio_enc.h"
#include "esp_audio_dec_default.h"
#include "gmf_loader_setup_defaults.h"
#include "gmf_audio_play_com.h"

static const char *TAG = "GMF_AUDIO_PLAY_COM";

void play_pause_single_file( esp_gmf_pipeline_handle_t pipe, const char *uri)
{
    ESP_LOGW(TAG, "play_single_file: %s\r\n", uri);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, uri));

    esp_gmf_element_handle_t dec_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el));
    esp_audio_simple_dec_cfg_t *simple_cfg = (esp_audio_simple_dec_cfg_t *)OBJ_GET_CFG(dec_el);
    TEST_ASSERT_NOT_NULL(simple_cfg);
    uint32_t type = ESP_AUDIO_TYPE_UNSUPPORT;
    esp_gmf_audio_helper_get_audio_type_by_uri(uri, &type);
    esp_gmf_info_sound_t info = {
        .sample_rates = 48000,
        .channels = 2,
        .bits = 16,
        .format_id = type,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));
    ESP_GMF_MEM_SHOW(TAG);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));

    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_pause(pipe));
    vTaskDelay(1000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_resume(pipe));
}

void play_loop_single_file(esp_gmf_pipeline_handle_t pipe, esp_audio_type_t type)
{
    ESP_LOGW(TAG, "play_single_file: %d\r\n", type);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));

    // enc reconfig
    esp_gmf_element_handle_t enc_handle = NULL;
    esp_gmf_pipeline_get_el_by_name(pipe, "aud_enc", &enc_handle);
    esp_gmf_info_sound_t info = {0};
    info.format_id = type;
    if (type == ESP_AUDIO_TYPE_G711A || type == ESP_AUDIO_TYPE_G711U) {
        info.sample_rates = 8000;
        info.channels = 2;
        info.bits = 16;
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, "/sdcard/test_8000hz_16bit_2ch_10000ms.pcm"));
    } else {
        info.sample_rates = 48000;
        info.channels = 2;
        info.bits = 16;
        info.bitrate = 90000;
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, "/sdcard/test_48000hz_16bit_2ch_10000ms.pcm"));
    }
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_enc_reconfig_by_sound_info(enc_handle, &info));
    esp_gmf_pipeline_report_info(pipe, ESP_GMF_INFO_SOUND, &info, sizeof(info));

    // dec reconfig
    esp_gmf_element_handle_t dec_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info));

    ESP_GMF_MEM_SHOW(TAG);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));

    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_pause(pipe));
    vTaskDelay(1000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_resume(pipe));
}

void gmf_register_audio_all(esp_gmf_pool_handle_t pool)
{
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, gmf_loader_setup_io_default(pool));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, gmf_loader_setup_audio_codec_default(pool));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, gmf_loader_setup_audio_effects_default(pool));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, gmf_loader_setup_misc_default(pool));
}

void gmf_unregister_audio_all(esp_gmf_pool_handle_t pool)
{
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, gmf_loader_teardown_audio_effects_default(pool));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, gmf_loader_teardown_audio_codec_default(pool));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, gmf_loader_teardown_io_default(pool));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, gmf_loader_teardown_misc_default(pool));
}

void gmf_setup_pipeline_out_dev(esp_gmf_pipeline_handle_t pipe)
{
    esp_gmf_io_handle_t io_handle = NULL;
    esp_gmf_pipeline_get_out(pipe, &io_handle);
    TEST_ASSERT_NOT_NULL(io_handle);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_io_codec_dev_set_dev(io_handle, esp_gmf_app_get_playback_handle()));
}

void gmf_setup_pipeline_in_dev(esp_gmf_pipeline_handle_t pipe)
{
    esp_gmf_io_handle_t io_handle = NULL;
    esp_gmf_pipeline_get_in(pipe, &io_handle);
    TEST_ASSERT_NOT_NULL(io_handle);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_io_codec_dev_set_dev(io_handle, esp_gmf_app_get_record_handle()));
}
