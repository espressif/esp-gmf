/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "unity.h"
#include <string.h>

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

#include "esp_gmf_io_embed_flash.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_io_file.h"
#include "esp_gmf_io_i2s_pdm.h"

#include "esp_gmf_copier.h"

void test_esp_gmf_alc_if()
{
    esp_ae_alc_cfg_t config = {.channel = 2};
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_alc_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_alc_init(&config, &handle), ESP_GMF_ERR_OK);
    // Set gain function test
    TEST_ASSERT_EQUAL(esp_gmf_alc_set_gain(NULL, 0, 10), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_alc_set_gain(handle, config.channel + 1, 10), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_alc_set_gain(handle, config.channel - 1, -10), ESP_GMF_ERR_OK);
    // Get gain function test
    int8_t gain = 0;
    TEST_ASSERT_EQUAL(esp_gmf_alc_get_gain(NULL, 0, &gain), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_alc_get_gain(handle, 0, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_alc_get_gain(handle, config.channel + 1, &gain), ESP_GMF_ERR_INVALID_ARG);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_alc_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_bit_cvt_if()
{
    esp_ae_bit_cvt_cfg_t config = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_bit_cvt_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_bit_cvt_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_bit_cvt_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_ch_cvt_if()
{
    esp_ae_ch_cvt_cfg_t config = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_ch_cvt_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_ch_cvt_init(&config, &handle), ESP_GMF_ERR_OK);
    // Set dest channel function test
    TEST_ASSERT_EQUAL(esp_gmf_ch_cvt_set_dest_channel(NULL, 1), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_ch_cvt_set_dest_channel(handle, 1), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_ch_cvt_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_deinterleave_if()
{
    esp_gmf_deinterleave_cfg config = DEFAULT_ESP_GMF_DEINTERLEAVE_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_deinterleave_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_deinterleave_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_deinterleave_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_eq_if()
{
    esp_ae_eq_cfg_t config = DEFAULT_ESP_GMF_EQ_CONFIG();
    esp_gmf_obj_handle_t handle;
    esp_ae_eq_filter_para_t para = {0};
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_eq_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_eq_init(&config, &handle), ESP_GMF_ERR_OK);
    // Set para function test
    TEST_ASSERT_EQUAL(esp_gmf_eq_set_para(NULL, 0, &para), ESP_GMF_ERR_INVALID_ARG);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_eq_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_fade_if()
{
    esp_ae_fade_cfg_t config = DEFAULT_ESP_GMF_FADE_CONFIG();
    esp_gmf_obj_handle_t handle;
    esp_ae_fade_mode_t mode = ESP_AE_FADE_MODE_INVALID;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_fade_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_fade_init(&config, &handle), ESP_GMF_ERR_OK);
    // Set mode function test
    TEST_ASSERT_EQUAL(esp_gmf_fade_set_mode(NULL, 0), ESP_GMF_ERR_INVALID_ARG);
    // Get mode function test
    TEST_ASSERT_EQUAL(esp_gmf_fade_get_mode(NULL, &mode), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_fade_get_mode(handle, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_fade_get_mode(handle, &mode), ESP_GMF_ERR_OK);
    // Set weight function test
    TEST_ASSERT_EQUAL(esp_gmf_fade_reset_weight(NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_fade_reset_weight(handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_fade_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_interleave_if()
{
    esp_gmf_interleave_cfg config = DEFAULT_ESP_GMF_INTERLEAVE_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_interleave_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_interleave_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_interleave_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_mixer_if()
{
    esp_ae_mixer_cfg_t config = DEFAULT_ESP_GMF_MIXER_CONFIG();
    esp_gmf_obj_handle_t handle;
    esp_ae_mixer_mode_t mode = ESP_AE_MIXER_MODE_INVALID;
    uint32_t sample_rate = 48000;
    uint8_t channel = 2;
    uint8_t bits = 16;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_mixer_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mixer_init(&config, &handle), ESP_GMF_ERR_OK);
    // Set mode function test
    TEST_ASSERT_EQUAL(esp_gmf_mixer_set_mode(NULL, 0, mode), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mixer_set_mode(handle, config.src_num + 1, mode), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mixer_set_mode(handle, 0, mode), ESP_GMF_ERR_OK);
    // Set audio info function test
    TEST_ASSERT_EQUAL(esp_gmf_mixer_set_audio_info(NULL, sample_rate, channel, bits), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mixer_set_audio_info(handle, sample_rate, channel, bits), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_mixer_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_rate_cvt_if()
{
    esp_ae_rate_cvt_cfg_t config = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    esp_gmf_obj_handle_t handle;
    uint32_t sample_rate = 48000;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_rate_cvt_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_rate_cvt_init(&config, &handle), ESP_GMF_ERR_OK);
    // Set mode function test
    TEST_ASSERT_EQUAL(esp_gmf_rate_cvt_set_dest_rate(NULL, sample_rate), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_rate_cvt_set_dest_rate(handle, sample_rate), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_rate_cvt_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_sonic_if()
{
    esp_ae_sonic_cfg_t config = DEFAULT_ESP_GMF_SONIC_CONFIG();
    esp_gmf_obj_handle_t handle;
    float speed = 1.0;
    float pitch = 1.0;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_sonic_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_init(&config, &handle), ESP_GMF_ERR_OK);
    // Set speed function test
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_speed(NULL, speed), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_speed(handle, speed), ESP_GMF_ERR_OK);
    // Get speed function test
    TEST_ASSERT_EQUAL(esp_gmf_sonic_get_speed(NULL, &speed), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_get_speed(handle, &speed), ESP_GMF_ERR_OK);
    // Set pitch function test
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_pitch(NULL, pitch), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_set_pitch(handle, pitch), ESP_GMF_ERR_OK);
    // Get pitch function test
    TEST_ASSERT_EQUAL(esp_gmf_sonic_get_pitch(NULL, &pitch), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_sonic_get_pitch(handle, &pitch), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_sonic_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_dec_if()
{
    esp_audio_simple_dec_cfg_t config = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_audio_dec_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_audio_dec_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_audio_dec_init(NULL, &handle), ESP_GMF_ERR_OK);
    esp_audio_simple_dec_cfg_t *cfg = OBJ_GET_CFG(handle);
    TEST_ASSERT_NOT_EQUAL(NULL, cfg);
    TEST_ASSERT_EQUAL(cfg->dec_type, ESP_AUDIO_SIMPLE_DEC_TYPE_NONE);
    TEST_ASSERT_EQUAL(cfg->dec_cfg, NULL);
    TEST_ASSERT_EQUAL(cfg->cfg_size, 0);
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_enc_if()
{
    esp_audio_enc_config_t config = DEFAULT_ESP_GMF_AUDIO_ENC_CONFIG();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_audio_enc_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_audio_enc_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    // Test for config is NULL, will create a default config
    TEST_ASSERT_EQUAL(esp_gmf_audio_enc_init(NULL, &handle), ESP_GMF_ERR_OK);
    esp_audio_enc_config_t *cfg = OBJ_GET_CFG(handle);
    TEST_ASSERT_NOT_EQUAL(NULL, cfg);
    TEST_ASSERT_EQUAL(cfg->type, ESP_AUDIO_TYPE_UNSUPPORT);
    TEST_ASSERT_EQUAL(cfg->cfg, NULL);
    TEST_ASSERT_EQUAL(cfg->cfg_sz, 0);
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_io_embed_flash_if()
{
    embed_flash_io_cfg_t config = EMBED_FLASH_CFG_DEFAULT();
    esp_gmf_obj_handle_t handle;
    const embed_item_info_t context = {0};
    int max_num = 3;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_io_embed_flash_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_io_embed_flash_init(&config, &handle), ESP_GMF_ERR_OK);
    // Set context function test
    TEST_ASSERT_EQUAL(esp_gmf_io_embed_flash_set_context(NULL, &context, max_num), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_io_embed_flash_set_context(handle, NULL, max_num), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_io_embed_flash_set_context(handle, &context, max_num), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_io_file_if()
{
    file_io_cfg_t config = FILE_IO_CFG_DEFAULT();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_io_file_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    config.dir = ESP_GMF_IO_DIR_NONE;
    TEST_ASSERT_EQUAL(esp_gmf_io_file_init(&config, &handle), ESP_GMF_ERR_NOT_SUPPORT);
    config.dir = ESP_GMF_IO_DIR_READER;
    TEST_ASSERT_EQUAL(esp_gmf_io_file_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_io_http_if()
{
    http_io_cfg_t config = HTTP_STREAM_CFG_DEFAULT();
    esp_gmf_obj_handle_t handle;
    const char *cert = "test";
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_io_http_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    config.dir = ESP_GMF_IO_DIR_NONE;
    TEST_ASSERT_EQUAL(esp_gmf_io_http_init(&config, &handle), ESP_GMF_ERR_NOT_SUPPORT);
    config.dir = ESP_GMF_IO_DIR_READER;
    TEST_ASSERT_EQUAL(esp_gmf_io_http_init(&config, &handle), ESP_GMF_ERR_OK);
    // Reset function test
    TEST_ASSERT_EQUAL(esp_gmf_io_http_reset(NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_io_http_reset(handle), ESP_GMF_ERR_OK);
    // Set server cert function test
    TEST_ASSERT_EQUAL(esp_gmf_io_http_set_server_cert(NULL, cert), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_io_http_set_server_cert(handle, cert), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_io_i2s_if()
{
    i2s_pdm_io_cfg_t config = ESP_GMF_IO_I2S_PDM_CFG_DEFAULT();
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_io_i2s_pdm_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    config.dir = ESP_GMF_IO_DIR_NONE;
    TEST_ASSERT_EQUAL(esp_gmf_io_i2s_pdm_init(&config, &handle), ESP_GMF_ERR_NOT_SUPPORT);
    config.dir = ESP_GMF_IO_DIR_READER;
    TEST_ASSERT_EQUAL(esp_gmf_io_i2s_pdm_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_copier_if()
{
    esp_gmf_copier_cfg_t config;
    esp_gmf_obj_handle_t handle;
    // Initialize function test
    TEST_ASSERT_EQUAL(esp_gmf_copier_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_copier_init(&config, &handle), ESP_GMF_ERR_OK);
    // Deinitialize function test
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

TEST_CASE("Test element if check", "[ESP_GMF_IF_CHECK]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    test_esp_gmf_alc_if();
    test_esp_gmf_bit_cvt_if();
    test_esp_gmf_ch_cvt_if();
    test_esp_gmf_deinterleave_if();
    test_esp_gmf_eq_if();
    test_esp_gmf_fade_if();
    test_esp_gmf_interleave_if();
    test_esp_gmf_mixer_if();
    test_esp_gmf_rate_cvt_if();
    test_esp_gmf_sonic_if();
    test_esp_gmf_dec_if();
    test_esp_gmf_enc_if();
    test_esp_gmf_io_embed_flash_if();
    test_esp_gmf_io_file_if();
    test_esp_gmf_io_http_if();
    test_esp_gmf_io_i2s_if();
    test_esp_gmf_copier_if();
}
