/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * Test API C++ compilation only, not as a example reference
 */
#include "unity.h"
#include "esp_gmf_io_file.h"
#include "esp_gmf_io_http.h"
#include "esp_gmf_io_embed_flash.h"
#include "esp_gmf_io_i2s_pdm.h"

extern "C" void test_cxx_build(void)
{
    file_io_cfg_t file_config = FILE_IO_CFG_DEFAULT();
    TEST_ASSERT_EQUAL(esp_gmf_io_file_init(&file_config, NULL), ESP_GMF_ERR_INVALID_ARG);

    http_io_cfg_t http_config = HTTP_STREAM_CFG_DEFAULT();
    TEST_ASSERT_EQUAL(esp_gmf_io_http_init(&http_config, NULL), ESP_GMF_ERR_INVALID_ARG);

    embed_flash_io_cfg_t embed_flash_config = EMBED_FLASH_CFG_DEFAULT();
    TEST_ASSERT_EQUAL(esp_gmf_io_embed_flash_init(&embed_flash_config, NULL), ESP_GMF_ERR_INVALID_ARG);

    i2s_pdm_io_cfg_t i2s_pdm_config = ESP_GMF_IO_I2S_PDM_CFG_DEFAULT();
    TEST_ASSERT_EQUAL(esp_gmf_io_i2s_pdm_init(&i2s_pdm_config, NULL), ESP_GMF_ERR_INVALID_ARG);
}
