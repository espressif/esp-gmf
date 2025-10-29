/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * Test API C++ compilation only, not as a example reference
 */
#include "unity.h"
#include "esp_gmf_pool.h"

#include "esp_audio_simple_player.h"
#include "esp_audio_simple_player_advance.h"

extern "C" void test_cxx_build(void)
{
    esp_asp_cfg_t cfg = {
        .in = { .cb = NULL, .user_ctx = NULL },
        .out = { .cb = NULL, .user_ctx = NULL },
        .task_prio = 5,
        .task_stack = 1024,
        .task_core = 0,
        .task_stack_in_ext = 1,
        .prev = NULL,
        .prev_ctx = NULL,
    };
    esp_asp_handle_t handle = NULL;
    esp_gmf_err_t err = esp_audio_simple_player_new(&cfg, &handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    TEST_ASSERT_NOT_NULL(handle);
    err = esp_audio_simple_player_set_event(handle, NULL, NULL);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt", "aud_alc"};
    err = esp_audio_simple_player_set_pipeline(handle, NULL, name, 5);
    TEST_ASSERT_EQUAL(ESP_OK, err);
    err = esp_audio_simple_player_destroy(handle);
    TEST_ASSERT_EQUAL(ESP_OK, err);
}
