/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_gmf_pool.h"
#include "esp_gmf_err.h"
#include "esp_log.h"

// GMF Audio Elements
#include "esp_audio_enc_default.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_simple_dec_default.h"
#include "esp_gmf_audio_dec.h"
#include "esp_gmf_audio_enc.h"
#include "esp_gmf_rate_cvt.h"
#include "esp_gmf_ch_cvt.h"
#include "esp_gmf_bit_cvt.h"
#include "esp_gmf_aec.h"

// GMF IO Types
#include "esp_gmf_io_codec_dev.h"
#include "esp_gmf_io_file.h"
#include "esp_gmf_io_bt.h"

#include "dev_audio_codec.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"

static const char *TAG = "POOL_INIT";

/**
 * @brief  Register GMF pool with required elements and IO types
 *
 * @param[in]  pool  GMF pool handle to initialize
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid pool handle
 *       - ESP_GMF_ERR_MEMORY_LACK  Memory allocation failed
 */
esp_gmf_err_t pool_reg(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);

    ESP_LOGI(TAG, "Registering GMF pool");

    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t element = NULL;
    esp_gmf_io_handle_t io = NULL;

    // ========== Register GMF Audio Elements ==========
    ret = esp_audio_dec_register_default();
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio decoders");
    ret = esp_audio_simple_dec_register_default();
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio simple decoders");
    ret = esp_audio_enc_register_default();
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio encoders");

    // 1. Audio Decoder (aud_dec)
    esp_audio_simple_dec_cfg_t dec_cfg = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    ret = esp_gmf_audio_dec_init(&dec_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init audio decoder");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio decoder");
    ESP_LOGD(TAG, "Registered: aud_dec");

    // 2. Audio Encoder (aud_enc)
    esp_audio_enc_config_t enc_cfg = DEFAULT_ESP_GMF_AUDIO_ENC_CONFIG();
    ret = esp_gmf_audio_enc_init(&enc_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init audio encoder");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio encoder");
    ESP_LOGD(TAG, "Registered: aud_enc");

    // 3. Audio Rate Converter (aud_rate_cvt)
    esp_ae_rate_cvt_cfg_t rate_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    ret = esp_gmf_rate_cvt_init(&rate_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init rate converter");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register rate converter");
    ESP_LOGD(TAG, "Registered: aud_rate_cvt");

    // 4. Audio Channel Converter (aud_ch_cvt)
    esp_ae_ch_cvt_cfg_t ch_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    ret = esp_gmf_ch_cvt_init(&ch_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init channel converter");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register channel converter");
    ESP_LOGD(TAG, "Registered: aud_ch_cvt");

    // 5. Audio Bit Converter (aud_bit_cvt)
    esp_ae_bit_cvt_cfg_t bit_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    ret = esp_gmf_bit_cvt_init(&bit_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init bit converter");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register bit converter");
    ESP_LOGD(TAG, "Registered: aud_bit_cvt");

    esp_gmf_aec_cfg_t aec_cfg = {
#if CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4
        .filter_len = 4,
#else
        .filter_len = 2,
#endif  /* CONFIG_IDF_TARGET_ESP32S3 || CONFIG_IDF_TARGET_ESP32P4 */
        .type = AFE_TYPE_VC_8K,
        .mode = AFE_MODE_HIGH_PERF,
        .input_format = "RM",
    };
    ret = esp_gmf_aec_init(&aec_cfg, &element);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init AEC");
    ret = esp_gmf_pool_register_element(pool, element, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register AEC");
    ESP_LOGI(TAG, "Registered: aud_aec");

    // ========== Register GMF IO Types ==========
    dev_audio_codec_handles_t *dac_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_DAC, (void **)&dac_handle);
    dev_audio_codec_handles_t *adc_handle = NULL;
    esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&adc_handle);

    // 1. Codec Device IO - Reader
    codec_dev_io_cfg_t codec_rx_cfg = ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT();
    codec_rx_cfg.dir = ESP_GMF_IO_DIR_READER;
    codec_rx_cfg.dev = adc_handle->codec_dev;
    ret = esp_gmf_io_codec_dev_init(&codec_rx_cfg, &io);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init codec dev reader");
    ret = esp_gmf_pool_register_io(pool, io, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register codec dev reader");
    ESP_LOGD(TAG, "Registered: io_codec_dev (reader)");

    // 2. Codec Device IO - Writer
    codec_dev_io_cfg_t codec_tx_cfg = ESP_GMF_IO_CODEC_DEV_CFG_DEFAULT();
    codec_tx_cfg.dir = ESP_GMF_IO_DIR_WRITER;
    codec_tx_cfg.dev = dac_handle->codec_dev;
    ret = esp_gmf_io_codec_dev_init(&codec_tx_cfg, &io);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init codec dev writer");
    ret = esp_gmf_pool_register_io(pool, io, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register codec dev writer");
    ESP_LOGD(TAG, "Registered: io_codec_dev (writer)");

    // 3. File IO - Reader
    file_io_cfg_t file_rx_cfg = FILE_IO_CFG_DEFAULT();
    file_rx_cfg.dir = ESP_GMF_IO_DIR_READER;
    ret = esp_gmf_io_file_init(&file_rx_cfg, &io);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init file reader");
    ret = esp_gmf_pool_register_io(pool, io, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register file reader");
    ESP_LOGD(TAG, "Registered: io_file (reader)");

    // 4. File IO - Writer
    file_io_cfg_t file_tx_cfg = FILE_IO_CFG_DEFAULT();
    file_tx_cfg.dir = ESP_GMF_IO_DIR_WRITER;
    ret = esp_gmf_io_file_init(&file_tx_cfg, &io);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init file writer");
    ret = esp_gmf_pool_register_io(pool, io, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register file writer");
    ESP_LOGD(TAG, "Registered: io_file (writer)");

    // 5. Bluetooth IO - Reader and Writer
    bt_io_cfg_t bt_rx_cfg = ESP_GMF_BT_IO_CFG_DEFAULT();
    bt_rx_cfg.dir = ESP_GMF_IO_DIR_READER;
    bt_rx_cfg.stream = NULL;
    ret = esp_gmf_io_bt_init(&bt_rx_cfg, &io);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init BT reader");
    ret = esp_gmf_pool_register_io(pool, io, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register BT reader");
    ESP_LOGD(TAG, "Registered: io_bt (reader)");

    bt_io_cfg_t bt_tx_cfg = ESP_GMF_BT_IO_CFG_DEFAULT();
    bt_tx_cfg.dir = ESP_GMF_IO_DIR_WRITER;
    bt_tx_cfg.stream = NULL;
    ret = esp_gmf_io_bt_init(&bt_tx_cfg, &io);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init BT writer");
    ret = esp_gmf_pool_register_io(pool, io, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register BT writer");
    ESP_LOGD(TAG, "Registered: io_bt (writer)");

    ESP_LOGI(TAG, "GMF pool initialization completed successfully");

    return ESP_GMF_ERR_OK;
}
