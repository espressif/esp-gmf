/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "unity.h"
#include <string.h>
#include <stdbool.h>

#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_fade.h"
#include "esp_gmf_mixer.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_drc.h"
#include "esp_gmf_mbc.h"
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

void test_esp_gmf_drc_if()
{
    esp_ae_drc_cfg_t config = DEFAULT_ESP_GMF_DRC_CONFIG();
    esp_gmf_obj_handle_t handle;
    uint16_t attack_time = 25;
    uint16_t release_time = 90;
    uint16_t hold_time = 6;
    float makeup_gain = 2.5f;
    float knee_width = 3.0f;
    esp_ae_drc_curve_point points[3] = {
        {.x = 0.0f,  .y = -10.0f},
        {.x = -30.0f, .y = -35.0f},
        {.x = -60.0f, .y = -60.0f},
    };

    TEST_ASSERT_EQUAL(esp_gmf_drc_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_drc_init(&config, &handle), ESP_GMF_ERR_OK);

    TEST_ASSERT_EQUAL(esp_gmf_drc_set_attack_time(NULL, attack_time), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_drc_set_attack_time(handle, attack_time), ESP_GMF_ERR_OK);
    uint16_t attack_read = 0;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_attack_time(NULL, &attack_read), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_attack_time(handle, &attack_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(attack_time, attack_read);

    TEST_ASSERT_EQUAL(esp_gmf_drc_set_release_time(handle, release_time), ESP_GMF_ERR_OK);
    uint16_t release_read = 0;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_release_time(handle, &release_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(release_time, release_read);

    TEST_ASSERT_EQUAL(esp_gmf_drc_set_hold_time(handle, hold_time), ESP_GMF_ERR_OK);
    uint16_t hold_read = 0;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_hold_time(handle, &hold_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(hold_time, hold_read);

    TEST_ASSERT_EQUAL(esp_gmf_drc_set_makeup_gain(handle, makeup_gain), ESP_GMF_ERR_OK);
    float makeup_read = 0.0f;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_makeup_gain(handle, &makeup_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(makeup_gain, makeup_read);

    TEST_ASSERT_EQUAL(esp_gmf_drc_set_knee_width(handle, knee_width), ESP_GMF_ERR_OK);
    float knee_read = 0.0f;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_knee_width(handle, &knee_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(knee_width, knee_read);
    TEST_ASSERT_EQUAL(esp_gmf_drc_set_points(handle, points, 0), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_drc_set_points(handle, points, 7), ESP_GMF_ERR_INVALID_ARG);

    TEST_ASSERT_EQUAL(esp_gmf_drc_set_points(handle, points, 3), ESP_GMF_ERR_OK);
    uint8_t point_num = 0;
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_point_num(handle, &point_num), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(3, point_num);
    esp_ae_drc_curve_point out_points[3] = {0};
    TEST_ASSERT_EQUAL(esp_gmf_drc_get_points(handle, out_points), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(points[1].x, out_points[1].x);
    TEST_ASSERT_EQUAL(points[1].y, out_points[1].y);

    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_drc_init(NULL, &handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_NOT_EQUAL(NULL, OBJ_GET_CFG(handle));
    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
}

void test_esp_gmf_mbc_if()
{
    esp_ae_mbc_config_t config = DEFAULT_ESP_GMF_MBC_CONFIG();
    esp_gmf_obj_handle_t handle;
    esp_ae_mbc_para_t para = {
        .threshold = -18.0f,
        .ratio = 2.5f,
        .makeup_gain = 4.0f,
        .attack_time = 5,
        .release_time = 120,
        .hold_time = 12,
        .knee_width = 1.5f,
    };
    uint32_t fc_value = 750;

    TEST_ASSERT_EQUAL(esp_gmf_mbc_init(&config, NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_init(&config, &handle), ESP_GMF_ERR_OK);

    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_para(NULL, 0, &para), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_para(handle, 0, &para), ESP_GMF_ERR_OK);
    esp_ae_mbc_para_t para_out = {0};
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_para(handle, 0, &para_out), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(para.threshold, para_out.threshold);
    TEST_ASSERT_EQUAL(para.ratio, para_out.ratio);
    TEST_ASSERT_EQUAL(para.attack_time, para_out.attack_time);
    TEST_ASSERT_EQUAL(para.release_time, para_out.release_time);
    TEST_ASSERT_EQUAL(para.hold_time, para_out.hold_time);
    TEST_ASSERT_EQUAL(para.knee_width, para_out.knee_width);
    TEST_ASSERT_EQUAL(para.makeup_gain, para_out.makeup_gain);

    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_fc(handle, 0, fc_value), ESP_GMF_ERR_OK);
    uint32_t fc_read = 0;
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_fc(handle, 0, &fc_read), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(fc_value, fc_read);

    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_solo(handle, 0, true), ESP_GMF_ERR_OK);
    bool solo_state = false;
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_solo(handle, 0, &solo_state), ESP_GMF_ERR_OK);
    TEST_ASSERT_TRUE(solo_state);

    TEST_ASSERT_EQUAL(esp_gmf_mbc_set_bypass(handle, 0, true), ESP_GMF_ERR_OK);
    bool bypass_state = false;
    TEST_ASSERT_EQUAL(esp_gmf_mbc_get_bypass(handle, 0, &bypass_state), ESP_GMF_ERR_OK);
    TEST_ASSERT_TRUE(bypass_state);

    TEST_ASSERT_EQUAL(esp_gmf_obj_delete(handle), ESP_GMF_ERR_OK);
    TEST_ASSERT_EQUAL(esp_gmf_mbc_init(NULL, &handle), ESP_GMF_ERR_OK);
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
    // Reset function test
    TEST_ASSERT_EQUAL(esp_gmf_fade_reset(NULL), ESP_GMF_ERR_INVALID_ARG);
    TEST_ASSERT_EQUAL(esp_gmf_fade_reset(handle), ESP_GMF_ERR_OK);
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
    test_esp_gmf_drc_if();
    test_esp_gmf_mbc_if();
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
