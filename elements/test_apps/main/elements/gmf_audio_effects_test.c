/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "unity.h"
#include <string.h>
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "esp_gmf_element.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_pool.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_oal_thread.h"
#include "esp_gmf_port.h"
#include "esp_gmf_obj.h"
#include "esp_gmf_data_bus.h"
#include "esp_gmf_new_databus.h"

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
#include "esp_audio_simple_dec_default.h"
#include "esp_audio_enc_reg.h"
#include "esp_audio_dec_reg.h"

#include "esp_gmf_app_setup_peripheral.h"
#include "esp_audio_enc_default.h"
#include "esp_audio_dec_default.h"
#include "esp_audio_dec_reg.h"
#include "esp_gmf_audio_helper.h"
#include "esp_gmf_audio_methods_def.h"
#include "esp_gmf_method.h"
#include "esp_gmf_audio_param.h"
#include "gmf_audio_play_com.h"

#ifdef MEDIA_LIB_MEM_TEST
#include "media_lib_adapter.h"
#include "media_lib_mem_trace.h"
#endif  /* MEDIA_LIB_MEM_TEST */

static const char *TAG = "AUDIO_EFFECTS_ELEMENT_TEST";

#define AUDIO_REC_SAMPLE_RATE (16000)
#define AUDIO_REC_BITS        (16)
#define AUDIO_REC_CHANNELS    (2)

#define PIPELINE_BLOCK_BIT     BIT(0)
#define PIPELINE_BLOCK_RUN_BIT BIT(1)

#define ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT (4096)

static esp_err_t _pipeline_event(esp_gmf_event_pkt_t *event, void *ctx)
{
    // The warning messages are used to make the content more noticeable.
    ESP_LOGW(TAG, "CB: RECV Pipeline EVT: el:%s-%p, type:%x, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        if (ctx) {
            xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
        }
    }
    return 0;
}

esp_err_t _pipeline_event2(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGW(TAG, "CB: RECV Pipeline2 EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
    }
    return 0;
}

esp_err_t _pipeline_event3(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGW(TAG, "CB: RECV Pipeline3 EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
    }
    if (event->sub == ESP_GMF_EVENT_STATE_RUNNING) {
        if (ctx) {
            xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_RUN_BIT);
        }
    }
    return 0;
}

esp_err_t _pipeline_event4(esp_gmf_event_pkt_t *event, void *ctx)
{
    ESP_LOGW(TAG, "CB: RECV Pipeline4 EVT: el:%s-%p, type:%d, sub:%s, payload:%p, size:%d,%p",
             "OBJ_GET_TAG(event->from)", event->from, event->type, esp_gmf_event_get_state_str(event->sub),
             event->payload, event->payload_size, ctx);
    if ((event->sub == ESP_GMF_EVENT_STATE_STOPPED)
        || (event->sub == ESP_GMF_EVENT_STATE_FINISHED)
        || (event->sub == ESP_GMF_EVENT_STATE_ERROR)) {
        xEventGroupSetBits((EventGroupHandle_t)ctx, PIPELINE_BLOCK_BIT);
    }
    return 0;
}

static esp_gmf_err_io_t ae_acquire_read(void *handle, esp_gmf_data_bus_block_t *blk, int wanted_size, int block_ticks)
{
    if (blk->buf == NULL) {
        return ESP_FAIL;
    }
    blk->valid_size = wanted_size;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t ae_release_read(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    blk->valid_size = 0;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t ae_acquire_write(void *handle, esp_gmf_data_bus_block_t *blk, int wanted_size, int block_ticks)
{
    if (blk->buf) {
        return ESP_GMF_IO_OK;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t ae_release_write(void *handle, esp_gmf_data_bus_block_t *blk, int block_ticks)
{
    ESP_LOGE(TAG, "%s-%d,file_release_write, %d,%p", __func__, __LINE__, blk->valid_size, blk);
    return ESP_GMF_IO_OK;
}

TEST_CASE("AUDIO CODEC METHOD TEST", "[ESP_GMF_Effects]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */

    esp_gmf_port_handle_t in_port = NULL;
    esp_gmf_port_handle_t out_port = NULL;

    printf("\r\n///////////////////// ENC /////////////////////\r\n");
    esp_audio_enc_register_default();

    esp_audio_enc_config_t es_enc_cfg = DEFAULT_ESP_GMF_AUDIO_ENC_CONFIG();
    esp_g711_enc_config_t g711_enc_cfg = ESP_G711_ENC_CONFIG_DEFAULT();
    es_enc_cfg.type = ESP_AUDIO_TYPE_G711A;
    es_enc_cfg.cfg = &g711_enc_cfg;
    es_enc_cfg.cfg_sz = sizeof(esp_g711_enc_config_t);
    esp_gmf_element_handle_t enc_handle = NULL;
    esp_gmf_audio_enc_init(&es_enc_cfg, &enc_handle);

    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(enc_handle, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(enc_handle, out_port);

    // use method
    esp_gmf_method_t *method_head = NULL;
    esp_gmf_method_t *method = NULL;
    esp_gmf_element_get_method((esp_gmf_element_handle_t)enc_handle, (const esp_gmf_method_t **)&method_head);
    uint8_t buf[100] = {0};

    // reconfig_by_sound_info
    esp_gmf_info_sound_t enc_info = {0};
    enc_info.format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_LC3;
    enc_info.sample_rates = ESP_AUDIO_SAMPLE_RATE_48K;
    enc_info.channels = 2;
    enc_info.bits = 16;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_method_found(method_head, AMETHOD(ENCODER, RECONFIG_BY_SND_INFO), (const esp_gmf_method_t **)&method));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_args_set_value(method->args_desc, AMETHOD_ARG(ENCODER, RECONFIG_BY_SND_INFO, INFO), buf, (uint8_t *)&enc_info, sizeof(esp_gmf_info_sound_t)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method((esp_gmf_element_handle_t)enc_handle, AMETHOD(ENCODER, RECONFIG_BY_SND_INFO), buf, sizeof(buf)));
    esp_audio_enc_config_t *enccfg = OBJ_GET_CFG(enc_handle);
    esp_lc3_enc_config_t *lc3_cfg = (esp_lc3_enc_config_t *)enccfg->cfg;
    TEST_ASSERT_EQUAL(ESP_AUDIO_TYPE_LC3, enccfg->type);
    TEST_ASSERT_EQUAL(ESP_AUDIO_SAMPLE_RATE_48K, lc3_cfg->sample_rate);
    TEST_ASSERT_EQUAL(120, lc3_cfg->nbyte);

    // reconfig
    es_enc_cfg.type = ESP_AUDIO_TYPE_AAC;
    esp_aac_enc_config_t aac_enc_cfg = ESP_AAC_ENC_CONFIG_DEFAULT();
    es_enc_cfg.cfg = &aac_enc_cfg;
    es_enc_cfg.cfg_sz = sizeof(esp_aac_enc_config_t);
    method = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_method_found(method_head, AMETHOD(ENCODER, RECONFIG), (const esp_gmf_method_t **)&method));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_args_set_value(method->args_desc, AMETHOD_ARG(ENCODER, RECONFIG, CFG), buf, (uint8_t *)&es_enc_cfg, sizeof(esp_audio_enc_config_t)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method((esp_gmf_element_handle_t)enc_handle, AMETHOD(ENCODER, RECONFIG), buf, sizeof(buf)));
    enccfg = OBJ_GET_CFG(enc_handle);
    esp_aac_enc_config_t *aac_cfg = (esp_aac_enc_config_t *)enccfg->cfg;
    TEST_ASSERT_EQUAL(ESP_AUDIO_TYPE_AAC, enccfg->type);
    TEST_ASSERT_EQUAL(ESP_AUDIO_SAMPLE_RATE_44K, aac_cfg->sample_rate);

    // get frame size by cfg
    uint32_t insize_by_cfg = 0;
    uint32_t outsize_by_cfg = 0;
    esp_gmf_method_found(method_head, AMETHOD(ENCODER, GET_FRAME_SZ), (const esp_gmf_method_t **)&method);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method((esp_gmf_element_handle_t)enc_handle, AMETHOD(ENCODER, GET_FRAME_SZ), buf, sizeof(buf)));
    esp_gmf_args_extract_value(method->args_desc, AMETHOD_ARG(ENCODER, GET_FRAME_SZ, INSIZE), buf, sizeof(buf), (uint32_t *)&insize_by_cfg);
    esp_gmf_args_extract_value(method->args_desc, AMETHOD_ARG(ENCODER, GET_FRAME_SZ, OUTSIZE), buf, sizeof(buf), (uint32_t *)&outsize_by_cfg);

    esp_gmf_element_process_open(enc_handle, NULL);
    // test set config after process
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_FAIL, esp_gmf_element_exe_method((esp_gmf_element_handle_t)enc_handle, AMETHOD(ENCODER, RECONFIG), buf, sizeof(buf)));

    esp_gmf_element_process_running(enc_handle, NULL);
    // set bitrate
    esp_gmf_method_found(method_head, AMETHOD(ENCODER, SET_BITRATE), (const esp_gmf_method_t **)&method);
    uint32_t bitrate = 18000;
    esp_gmf_args_set_value(method->args_desc, AMETHOD_ARG(ENCODER, SET_BITRATE, BITRATE), buf, (uint8_t *)&bitrate, sizeof(uint32_t));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method((esp_gmf_element_handle_t)enc_handle, AMETHOD(ENCODER, SET_BITRATE), buf, sizeof(buf)));
    // get bitrate
    uint32_t out_bitrate = 0;
    esp_gmf_method_found(method_head, AMETHOD(ENCODER, GET_BITRATE), (const esp_gmf_method_t **)&method);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method((esp_gmf_element_handle_t)enc_handle, AMETHOD(ENCODER, GET_BITRATE), buf, sizeof(buf)));
    esp_gmf_args_extract_value(method->args_desc, AMETHOD_ARG(ENCODER, GET_BITRATE, BITRATE), buf, sizeof(buf), (uint32_t *)&out_bitrate);
    TEST_ASSERT_EQUAL(18000, out_bitrate);

    // get frame size by handle
    uint32_t insize_by_hd = 0;
    uint32_t outsize_by_hd = 0;
    esp_gmf_method_found(method_head, AMETHOD(ENCODER, GET_FRAME_SZ), (const esp_gmf_method_t **)&method);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method((esp_gmf_element_handle_t)enc_handle, AMETHOD(ENCODER, GET_FRAME_SZ), buf, sizeof(buf)));
    esp_gmf_args_extract_value(method->args_desc, AMETHOD_ARG(ENCODER, GET_FRAME_SZ, INSIZE), buf, sizeof(buf), (uint32_t *)&insize_by_hd);
    esp_gmf_args_extract_value(method->args_desc, AMETHOD_ARG(ENCODER, GET_FRAME_SZ, OUTSIZE), buf, sizeof(buf), (uint32_t *)&outsize_by_hd);
    TEST_ASSERT_EQUAL(insize_by_cfg, insize_by_hd);
    TEST_ASSERT_EQUAL(outsize_by_cfg, outsize_by_hd);

    esp_gmf_element_process_close(enc_handle, NULL);
    esp_gmf_obj_delete(enc_handle);

    esp_audio_enc_unregister_default();

    ESP_GMF_MEM_SHOW(TAG);
    printf("\r\n///////////////////// DEC /////////////////////\r\n");
    esp_audio_dec_register_default();
    esp_audio_simple_dec_register_default();
    esp_audio_simple_dec_cfg_t es_dec_cfg = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    esp_gmf_element_handle_t es_hd = NULL;
    esp_gmf_audio_dec_init(&es_dec_cfg, &es_hd);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(es_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(es_hd, out_port);
    // use method
    esp_gmf_element_get_method((esp_gmf_element_handle_t)es_hd, (const esp_gmf_method_t **)&method_head);

    // reconfig_by_sound_info
    esp_gmf_info_sound_t dec_info = {0};
    dec_info.format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_SBC;
    dec_info.sample_rates = ESP_AUDIO_SAMPLE_RATE_44K;
    dec_info.channels = 2;
    dec_info.bits = 16;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_method_found(method_head, AMETHOD(DECODER, RECONFIG_BY_SND_INFO), (const esp_gmf_method_t **)&method));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_args_set_value(method->args_desc, AMETHOD_ARG(DECODER, RECONFIG_BY_SND_INFO, INFO), buf, (uint8_t *)&dec_info, sizeof(esp_gmf_info_sound_t)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method((esp_gmf_element_handle_t)es_hd, AMETHOD(DECODER, RECONFIG_BY_SND_INFO), buf, sizeof(buf)));
    esp_audio_simple_dec_cfg_t *deccfg = OBJ_GET_CFG(es_hd);
    esp_sbc_dec_cfg_t *sbc_cfg = (esp_sbc_dec_cfg_t *)deccfg->dec_cfg;
    TEST_ASSERT_EQUAL(ESP_AUDIO_SIMPLE_DEC_TYPE_SBC, deccfg->dec_type);
    TEST_ASSERT_EQUAL(ESP_SBC_MODE_STD, sbc_cfg->sbc_mode);
    TEST_ASSERT_EQUAL(2, sbc_cfg->ch_num);
    // reconfig
    es_dec_cfg.dec_type = ESP_AUDIO_TYPE_LC3;
    esp_lc3_dec_cfg_t lc3_dec_cfg = ESP_LC3_DEC_CONFIG_DEFAULT();
    es_dec_cfg.dec_cfg = &lc3_dec_cfg;
    es_dec_cfg.cfg_size = sizeof(esp_lc3_dec_cfg_t);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_method_found(method_head, AMETHOD(DECODER, RECONFIG), (const esp_gmf_method_t **)&method));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_args_set_value(method->args_desc, AMETHOD_ARG(DECODER, RECONFIG, CFG), buf, (uint8_t *)&es_dec_cfg, sizeof(esp_audio_simple_dec_cfg_t)));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_exe_method((esp_gmf_element_handle_t)es_hd, AMETHOD(DECODER, RECONFIG), buf, sizeof(buf)));
    deccfg = OBJ_GET_CFG(es_hd);
    esp_lc3_dec_cfg_t *lc3_cfg2 = (esp_lc3_dec_cfg_t *)deccfg->dec_cfg;
    TEST_ASSERT_EQUAL(ESP_AUDIO_TYPE_LC3, deccfg->dec_type);
    TEST_ASSERT_EQUAL(120, lc3_cfg2->nbyte);

    esp_gmf_element_process_open(es_hd, NULL);
    esp_gmf_element_process_running(es_hd, NULL);
    esp_gmf_element_process_close(es_hd, NULL);
    esp_gmf_obj_delete(es_hd);
}

TEST_CASE("AUDIO EFFECTS PARAMETER TEST", "[ESP_GMF_Effects]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);

    esp_gmf_port_handle_t in_port = NULL;
    esp_gmf_port_handle_t out_port = NULL;

    printf("\r\n///////////////////// BIT CONVERT /////////////////////\r\n");
    esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    esp_gmf_element_handle_t bit_hd = NULL;
    esp_gmf_bit_cvt_init(&bit_cvt_cfg, &bit_hd);
    uint8_t dest_bits = 24;
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(bit_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(bit_hd, out_port);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_hd, dest_bits));
    esp_ae_bit_cvt_cfg_t *bit_cvt_info = (esp_ae_bit_cvt_cfg_t *)OBJ_GET_CFG(bit_hd);
    TEST_ASSERT_EQUAL(bit_cvt_info->dest_bits, dest_bits);
    esp_gmf_element_process_open(bit_hd, NULL);
    esp_gmf_element_process_running(bit_hd, NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_bits(bit_hd, 32));
    bit_cvt_info = (esp_ae_bit_cvt_cfg_t *)OBJ_GET_CFG(bit_hd);
    TEST_ASSERT_EQUAL(bit_cvt_info->dest_bits, 32);
    esp_gmf_element_process_close(bit_hd, NULL);
    esp_gmf_obj_delete(bit_hd);

    printf("\r\n///////////////////// CH CONVERT /////////////////////\r\n");
    esp_ae_ch_cvt_cfg_t ch_cvt_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    esp_gmf_element_handle_t ch_hd = NULL;
    esp_gmf_ch_cvt_init(&ch_cvt_cfg, &ch_hd);
    uint8_t dest_ch = 3;
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(ch_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(ch_hd, out_port);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_hd, dest_ch));
    esp_ae_ch_cvt_cfg_t *ch_cvt_info = (esp_ae_ch_cvt_cfg_t *)OBJ_GET_CFG(ch_hd);
    TEST_ASSERT_EQUAL(ch_cvt_info->dest_ch, dest_ch);
    esp_gmf_element_process_open(ch_hd, NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_ch(ch_hd, 6));
    esp_gmf_element_process_running(ch_hd, NULL);
    ch_cvt_info = (esp_ae_ch_cvt_cfg_t *)OBJ_GET_CFG(ch_hd);
    TEST_ASSERT_EQUAL(ch_cvt_info->dest_ch, 6);
    esp_gmf_element_process_close(ch_hd, NULL);
    esp_gmf_obj_delete(ch_hd);

    printf("\r\n///////////////////// RATE CVT /////////////////////\r\n");
    esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    esp_gmf_element_handle_t rate_hd = NULL;
    esp_gmf_rate_cvt_init(&rate_cvt_cfg, &rate_hd);
    uint32_t rate = 24000;
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(rate_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(rate_hd, out_port);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_hd, rate));
    esp_ae_rate_cvt_cfg_t *rate_info = (esp_ae_rate_cvt_cfg_t *)OBJ_GET_CFG(rate_hd);
    TEST_ASSERT_EQUAL(rate_info->dest_rate, rate);
    esp_gmf_element_process_open(rate_hd, NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_param_set_dest_rate(rate_hd, 32000));
    esp_gmf_element_process_running(rate_hd, NULL);
    rate_info = (esp_ae_rate_cvt_cfg_t *)OBJ_GET_CFG(rate_hd);
    TEST_ASSERT_EQUAL(rate_info->dest_rate, 32000);
    esp_gmf_element_process_close(rate_hd, NULL);
    esp_gmf_obj_delete(rate_hd);
}

TEST_CASE("Test methods for all effects", "[ESP_GMF_Effects]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */

    esp_gmf_port_handle_t in_port = NULL;
    esp_gmf_port_handle_t out_port = NULL;

    printf("\r\n///////////////////// ALC /////////////////////\r\n");
    esp_ae_alc_cfg_t alc_cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
    alc_cfg.channel = 2;
    esp_gmf_element_handle_t alc_hd = NULL;
    esp_gmf_alc_init(&alc_cfg, &alc_hd);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(alc_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(alc_hd, out_port);
    esp_gmf_element_process_open(alc_hd, NULL);
    int8_t gain[2] = {-1, -5};
    for (uint8_t i = 0; i < 2; i++) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_alc_set_gain(alc_hd, i, gain[i]));
    }
    esp_gmf_element_process_running(alc_hd, NULL);
    for (uint8_t i = 0; i < 2; i++) {
        int8_t gain_out = 0;
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_alc_get_gain(alc_hd, i, &gain_out));
        TEST_ASSERT_EQUAL(gain[i], gain_out);
    }
    esp_gmf_element_process_close(alc_hd, NULL);
    esp_gmf_obj_delete(alc_hd);

    printf("\r\n///////////////////// BIT CONVERT /////////////////////\r\n");
    esp_ae_bit_cvt_cfg_t bit_cvt_cfg = DEFAULT_ESP_GMF_BIT_CVT_CONFIG();
    esp_gmf_element_handle_t bit_hd = NULL;
    esp_gmf_bit_cvt_init(&bit_cvt_cfg, &bit_hd);
    uint8_t dest_bits = 24;
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(bit_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(bit_hd, out_port);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_bit_cvt_set_dest_bits(bit_hd, dest_bits));
    esp_gmf_element_process_open(bit_hd, NULL);
    esp_gmf_element_process_running(bit_hd, NULL);
    esp_ae_bit_cvt_cfg_t *bit_cvt_info = (esp_ae_bit_cvt_cfg_t *)OBJ_GET_CFG(bit_hd);
    TEST_ASSERT_EQUAL(bit_cvt_info->dest_bits, dest_bits);
    esp_gmf_element_process_close(bit_hd, NULL);
    esp_gmf_obj_delete(bit_hd);

    printf("\r\n///////////////////// CH CONVERT /////////////////////\r\n");
    esp_ae_ch_cvt_cfg_t ch_cvt_cfg = DEFAULT_ESP_GMF_CH_CVT_CONFIG();
    esp_gmf_element_handle_t ch_hd = NULL;
    esp_gmf_ch_cvt_init(&ch_cvt_cfg, &ch_hd);
    uint8_t dest_ch = 3;
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(ch_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(ch_hd, out_port);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_ch_cvt_set_dest_channel(ch_hd, dest_ch));
    esp_gmf_element_process_open(ch_hd, NULL);
    esp_gmf_element_process_running(ch_hd, NULL);
    esp_ae_ch_cvt_cfg_t *ch_cvt_info = (esp_ae_ch_cvt_cfg_t *)OBJ_GET_CFG(ch_hd);
    TEST_ASSERT_EQUAL(ch_cvt_info->dest_ch, dest_ch);
    esp_gmf_element_process_close(ch_hd, NULL);
    esp_gmf_obj_delete(ch_hd);

    printf("\r\n///////////////////// EQ /////////////////////\r\n");
    esp_ae_eq_cfg_t eq_cfg = DEFAULT_ESP_GMF_EQ_CONFIG();
    esp_gmf_element_handle_t eq_hd = NULL;
    esp_gmf_eq_init(&eq_cfg, &eq_hd);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(eq_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(eq_hd, out_port);
    esp_gmf_element_process_open(eq_hd, NULL);

    esp_ae_eq_filter_para_t para = {
        .filter_type = ESP_AE_EQ_FILTER_HIGH_SHELF,
        .fc = 11000,
        .q = 4.0,
        .gain = -3.5,
    };
    esp_ae_eq_filter_para_t para_out = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_eq_set_para(eq_hd, 4, &para));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_eq_enable_filter(eq_hd, 1, true));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_eq_enable_filter(eq_hd, 3, false));
    esp_gmf_element_process_running(eq_hd, NULL);
    esp_gmf_eq_get_para(eq_hd, 4, &para_out);
    TEST_ASSERT_EQUAL(para.filter_type, para_out.filter_type);
    TEST_ASSERT_EQUAL(para.fc, para_out.fc);
    TEST_ASSERT_EQUAL_FLOAT(para.q, para_out.q);
    TEST_ASSERT_EQUAL_FLOAT(para.gain, para_out.gain);
    esp_gmf_element_process_close(eq_hd, NULL);
    esp_gmf_obj_delete(eq_hd);

    printf("\r\n///////////////////// FADE /////////////////////\r\n");
    esp_ae_fade_cfg_t fade_cfg = DEFAULT_ESP_GMF_FADE_CONFIG();
    esp_gmf_element_handle_t fade_hd = NULL;
    esp_gmf_fade_init(&fade_cfg, &fade_hd);

    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(fade_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(fade_hd, out_port);
    esp_gmf_element_process_open(fade_hd, NULL);

    esp_ae_fade_mode_t mode = ESP_AE_FADE_MODE_FADE_OUT;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_fade_set_mode(fade_hd, mode));
    esp_gmf_element_process_running(fade_hd, NULL);
    esp_ae_fade_mode_t out_mode = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_fade_get_mode(fade_hd, &out_mode));
    TEST_ASSERT_EQUAL(mode, out_mode);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_fade_reset_weight(fade_hd));
    esp_gmf_element_process_running(fade_hd, NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_fade_get_mode(fade_hd, &out_mode));
    TEST_ASSERT_EQUAL(ESP_AE_FADE_MODE_FADE_IN, out_mode);
    esp_gmf_element_process_close(fade_hd, NULL);
    esp_gmf_obj_delete(fade_hd);

    printf("\r\n///////////////////// MIXER /////////////////////\r\n");
    esp_ae_mixer_cfg_t mixer_cfg = DEFAULT_ESP_GMF_MIXER_CONFIG();
    uint32_t mix_rate = 24000;
    uint8_t mix_ch = 4;
    uint8_t mix_bits = 24;
    esp_gmf_element_handle_t mixer_hd = NULL;
    esp_gmf_mixer_init(&mixer_cfg, &mixer_hd);

    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(mixer_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(mixer_hd, out_port);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_mixer_set_audio_info(mixer_hd, mix_rate, mix_bits, mix_ch));
    esp_ae_mixer_cfg_t *mix_info = (esp_ae_mixer_cfg_t *)OBJ_GET_CFG(mixer_hd);
    TEST_ASSERT_EQUAL(mix_info->sample_rate, mix_rate);
    TEST_ASSERT_EQUAL(mix_info->bits_per_sample, mix_bits);
    TEST_ASSERT_EQUAL(mix_info->channel, mix_ch);
    esp_gmf_element_process_open(mixer_hd, NULL);
    esp_ae_mixer_mode_t mixer_mode = ESP_AE_MIXER_MODE_FADE_DOWNWARD;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_mixer_set_mode(mixer_hd, 0, mixer_mode));
    esp_gmf_element_process_running(mixer_hd, NULL);
    esp_gmf_element_process_close(mixer_hd, NULL);
    esp_gmf_obj_delete(mixer_hd);

    printf("\r\n///////////////////// RATE CVT /////////////////////\r\n");
    esp_ae_rate_cvt_cfg_t rate_cvt_cfg = DEFAULT_ESP_GMF_RATE_CVT_CONFIG();
    esp_gmf_element_handle_t rate_hd = NULL;
    esp_gmf_rate_cvt_init(&rate_cvt_cfg, &rate_hd);
    uint32_t rate = 24000;
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(rate_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(rate_hd, out_port);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_rate_cvt_set_dest_rate(rate_hd, rate));
    esp_gmf_element_process_open(rate_hd, NULL);
    esp_gmf_element_process_running(rate_hd, NULL);
    esp_ae_rate_cvt_cfg_t *rate_info = (esp_ae_rate_cvt_cfg_t *)OBJ_GET_CFG(rate_hd);
    TEST_ASSERT_EQUAL(rate_info->dest_rate, rate);
    esp_gmf_element_process_close(rate_hd, NULL);
    esp_gmf_obj_delete(rate_hd);

    printf("\r\n///////////////////// SONIC /////////////////////\r\n");
    esp_ae_sonic_cfg_t sonic_cfg = DEFAULT_ESP_GMF_SONIC_CONFIG();
    esp_gmf_element_handle_t sonic_hd = NULL;
    esp_gmf_sonic_init(&sonic_cfg, &sonic_hd);

    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(sonic_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(sonic_hd, out_port);
    esp_gmf_element_process_open(sonic_hd, NULL);

    float speed = 1.7;
    float speed_out = 0.0;
    float pitch = 0.7;
    float pitch_out = 0.0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_sonic_set_speed(sonic_hd, speed));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_sonic_set_pitch(sonic_hd, pitch));
    esp_gmf_element_process_running(sonic_hd, NULL);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_sonic_get_speed(sonic_hd, &speed_out));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_sonic_get_pitch(sonic_hd, &pitch_out));
    TEST_ASSERT_EQUAL_FLOAT(speed, speed_out);
    TEST_ASSERT_EQUAL_FLOAT(pitch, pitch_out);

    esp_gmf_element_process_close(sonic_hd, NULL);
    esp_gmf_obj_delete(sonic_hd);

    printf("\r\n///////////////////// DEC /////////////////////\r\n");
    esp_mp3_dec_register();
    esp_audio_simple_dec_cfg_t es_dec_cfg = DEFAULT_ESP_GMF_AUDIO_DEC_CONFIG();
    es_dec_cfg.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3;
    esp_gmf_element_handle_t es_hd = NULL;
    esp_gmf_audio_dec_init(&es_dec_cfg, &es_hd);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(es_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(es_hd, out_port);
    esp_gmf_element_process_open(es_hd, NULL);
    esp_gmf_element_process_running(es_hd, NULL);
    esp_gmf_element_process_close(es_hd, NULL);
    esp_gmf_obj_delete(es_hd);
    esp_audio_dec_unregister(ESP_AUDIO_TYPE_MP3);

    printf("\r\n///////////////////// ENC /////////////////////\r\n");
    esp_aac_enc_register();
    esp_audio_enc_config_t es_enc_cfg = DEFAULT_ESP_GMF_AUDIO_ENC_CONFIG();
    esp_aac_enc_config_t aac_enc_cfg = ESP_AAC_ENC_CONFIG_DEFAULT();
    es_enc_cfg.type = ESP_AUDIO_TYPE_AAC;
    es_enc_cfg.cfg = &aac_enc_cfg;
    es_enc_cfg.cfg_sz = sizeof(esp_aac_enc_config_t);
    esp_gmf_element_handle_t enc_handle = NULL;
    esp_gmf_audio_enc_init(&es_enc_cfg, &enc_handle);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(enc_handle, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(enc_handle, out_port);
    esp_gmf_element_process_open(enc_handle, NULL);
    esp_gmf_element_process_running(enc_handle, NULL);
    esp_gmf_element_process_close(enc_handle, NULL);
    esp_gmf_obj_delete(enc_handle);
    esp_audio_enc_unregister(ESP_AUDIO_TYPE_AAC);

    printf("\r\n///////////////////// DEINTERLEAVE /////////////////////\r\n");
    esp_gmf_deinterleave_cfg deinterleave_cfg = DEFAULT_ESP_GMF_DEINTERLEAVE_CONFIG();
    esp_gmf_element_handle_t deinterleave_hd = NULL;
    esp_gmf_deinterleave_init(&deinterleave_cfg, &deinterleave_hd);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(deinterleave_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(deinterleave_hd, out_port);
    esp_gmf_element_process_open(deinterleave_hd, NULL);
    esp_gmf_element_process_running(deinterleave_hd, NULL);
    esp_gmf_element_process_close(deinterleave_hd, NULL);
    esp_gmf_obj_delete(deinterleave_hd);

    printf("\r\n///////////////////// INTERLEAVE /////////////////////\r\n");
    esp_gmf_interleave_cfg interleave_cfg = DEFAULT_ESP_GMF_INTERLEAVE_CONFIG();
    esp_gmf_element_handle_t interleave_hd = NULL;
    esp_gmf_interleave_init(&interleave_cfg, &interleave_hd);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(ae_acquire_read, ae_release_read, NULL, NULL, 100, 100);
    esp_gmf_element_register_in_port(interleave_hd, in_port);
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(ae_acquire_write, ae_release_write, NULL, NULL, 100, 100);
    esp_gmf_element_register_out_port(interleave_hd, out_port);
    esp_gmf_element_process_open(interleave_hd, NULL);
    esp_gmf_element_process_running(interleave_hd, NULL);
    esp_gmf_element_process_close(interleave_hd, NULL);
    esp_gmf_obj_delete(interleave_hd);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

TEST_CASE("Audio Effects Play, [FILE->dec->resample->bvt->cvt->alc->eq->fade->sonic->IIS]", "[ESP_GMF_Effects]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_POOL", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_setup_codec_dev(NULL);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
    EventGroupHandle_t pipe_sync_evt = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt, return);

#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);

    gmf_register_audio_all(pool);

    ESP_GMF_POOL_SHOW_ITEMS(pool);
    esp_gmf_pipeline_handle_t pipe = NULL;

    const char *name[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt", "aud_alc", "aud_eq", "aud_fade", "aud_sonic"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name, sizeof(name) / sizeof(char *), "io_codec_dev", &pipe));
    TEST_ASSERT_NOT_NULL(pipe);

    gmf_setup_pipeline_out_dev(pipe);
    esp_gmf_task_cfg_t cfg = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg.ctx = NULL;
    cfg.cb = NULL;
    esp_gmf_task_handle_t work_task = NULL;
    esp_gmf_task_init(&cfg, &work_task);
    TEST_ASSERT_NOT_NULL(work_task);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe, work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe, _pipeline_event, pipe_sync_evt));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_reset(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe, "/sdcard/test.mp3"));
    esp_gmf_element_handle_t dec_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe, "aud_dec", &dec_el));
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe));

    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_pause(pipe));
    vTaskDelay(1000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_resume(pipe));

    xEventGroupWaitBits(pipe_sync_evt, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    vEventGroupDelete(pipe_sync_evt);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

/***
// Test deinterleave with two pipeline
                                                   +-----+
                                          +- RB1 ->+ alc +-> RB3 -+
+-------------------------------------+   |        +-----+        |  +-----------------------+
|     Pipe: file-->dec-->deinterleave |-- +                       +--| interleave-->rate_cvt |
+---------------------+-------------- +   |        +-----+        |  +-----------------------+
                                          +- RB2 ->+ alc +-> RB4 -+
                                                   +-----+
***/
TEST_CASE("Audio Effects Data Weaver test", "[ESP_GMF_Effects]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
    esp_gmf_app_setup_codec_dev(NULL);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
    EventGroupHandle_t pipe_sync_evt1 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt1, return);
    EventGroupHandle_t pipe_sync_evt2 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt2, return);
    EventGroupHandle_t pipe_sync_evt3 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt3, return);
    EventGroupHandle_t pipe_sync_evt4 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt4, return);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);

    gmf_register_audio_all(pool);

    ESP_GMF_POOL_SHOW_ITEMS(pool);
    esp_gmf_pipeline_handle_t pipe1 = NULL;
    const char *name1[] = {"aud_dec", "aud_deintlv"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name1, sizeof(name1) / sizeof(char *), NULL, &pipe1));
    TEST_ASSERT_NOT_NULL(pipe1);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe1, "/sdcard/test.mp3"));
    esp_gmf_element_handle_t dec_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe1, "aud_dec", &dec_el));
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);

    esp_gmf_pipeline_handle_t pipe2 = NULL;
    const char *name2[] = {"aud_alc"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name2, sizeof(name2) / sizeof(char *), NULL, &pipe2));
    TEST_ASSERT_NOT_NULL(pipe2);

    esp_gmf_pipeline_handle_t pipe3 = NULL;
    const char *name3[] = {"aud_alc"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name3, sizeof(name3) / sizeof(char *), NULL, &pipe3));
    TEST_ASSERT_NOT_NULL(pipe3);

    esp_gmf_pipeline_handle_t pipe4 = NULL;
    const char *name4[] = {"aud_intlv", "aud_rate_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name4, sizeof(name4) / sizeof(char *), "io_codec_dev", &pipe4));
    TEST_ASSERT_NOT_NULL(pipe4);

    gmf_setup_pipeline_out_dev(pipe4);
    // create rb
    esp_gmf_db_handle_t db1 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db1));
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db1,
                                                               ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db1,
                                                             ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe1, "aud_deintlv", out_port, pipe2, "aud_alc", in_port));
    esp_gmf_db_handle_t db2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db2));
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db2,
                                         ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db2,
                                       ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe1, "aud_deintlv", out_port, pipe3, "aud_alc", in_port));
    esp_gmf_db_handle_t db3 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db3));
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db3,
                                         ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db3,
                                       ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe2, "aud_alc", out_port, pipe4, "aud_intlv", in_port));
    esp_gmf_db_handle_t db4 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db4));
    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db4,
                                         ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db4,
                                       ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 3000);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe3, "aud_alc", out_port, pipe4, "aud_intlv", in_port));

    esp_gmf_task_cfg_t cfg1 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg1.ctx = NULL;
    cfg1.cb = NULL;
    cfg1.thread.core = 0;
    cfg1.thread.prio = 3;
    cfg1.name = "aud_deintlv";
    esp_gmf_task_handle_t work_task1 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg1, &work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe1, work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe1, _pipeline_event, pipe_sync_evt1));

    esp_gmf_task_cfg_t cfg2 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg2.thread.core = 0;
    cfg2.thread.prio = 3;
    cfg2.name = "channel1";
    esp_gmf_task_handle_t work_task2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg2, &work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe2, work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe2, _pipeline_event2, pipe_sync_evt2));

    esp_gmf_task_cfg_t cfg3 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg3.ctx = NULL;
    cfg3.cb = NULL;
    cfg3.thread.core = 1;
    cfg3.thread.prio = 3;
    cfg3.name = "channel2";
    esp_gmf_task_handle_t work_task3 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg3, &work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe3, work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe3, _pipeline_event3, pipe_sync_evt3));

    esp_gmf_task_cfg_t cfg4 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg4.ctx = NULL;
    cfg4.cb = NULL;
    cfg4.thread.core = 0;
    cfg4.thread.prio = 3;
    cfg4.name = "aud_intlv";
    esp_gmf_task_handle_t work_task4 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg4, &work_task4));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe4, work_task4));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe4));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe4, _pipeline_event4, pipe_sync_evt4));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe4));

    vTaskDelay(2000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_pause(pipe4));

    vTaskDelay(1000 / portTICK_RATE_MS);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_resume(pipe4));

    xEventGroupWaitBits(pipe_sync_evt1, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt2, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt3, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt4, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe4));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task4));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe4));
    gmf_unregister_audio_all(pool);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_deinit(pool));
    vEventGroupDelete(pipe_sync_evt1);
    vEventGroupDelete(pipe_sync_evt2);
    vEventGroupDelete(pipe_sync_evt3);
    vEventGroupDelete(pipe_sync_evt4);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}

/***
// Test mix with two pipeline

+-------------------------------------+
|    Pipe1: file-->dec-->deinterleave |-+- RB1 -+
+---------------------+-------------- +         |
                                                |   +-------+
                                                +-- | mixer |
                                                |   +-------+
+-------------------------------------+         |
|    Pipe1: file-->dec-->deinterleave |-+- RB2 -+
+---------------------+-------------- +

***/
TEST_CASE("Audio mixer Play", "[ESP_GMF_Effects]")
{
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("ESP_GMF_PIPELINE", ESP_LOG_DEBUG);
    esp_log_level_set("ESP_GMF_POOL", ESP_LOG_DEBUG);
    ESP_GMF_MEM_SHOW(TAG);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_add_default_adapter();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_setup_codec_dev(NULL);
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
    EventGroupHandle_t pipe_sync_evt1 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt1, return);

    EventGroupHandle_t pipe_sync_evt2 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt2, return);

    EventGroupHandle_t pipe_sync_evt3 = xEventGroupCreate();
    ESP_GMF_NULL_CHECK(TAG, pipe_sync_evt3, return);

    esp_gmf_pool_handle_t pool = NULL;
    esp_gmf_pool_init(&pool);
    TEST_ASSERT_NOT_NULL(pool);
    gmf_register_audio_all(pool);

    ESP_GMF_POOL_SHOW_ITEMS(pool);
    esp_gmf_pipeline_handle_t pipe1 = NULL;
    const char *name1[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name1, sizeof(name1) / sizeof(char *), NULL, &pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe1, "/sdcard/test1.mp3"));
    esp_gmf_element_handle_t dec_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe1, "aud_dec", &dec_el));
    esp_gmf_info_sound_t info = {
        .format_id = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3,
    };
    esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);

    esp_gmf_pipeline_handle_t pipe2 = NULL;
    const char *name2[] = {"aud_dec", "aud_rate_cvt", "aud_ch_cvt", "aud_bit_cvt"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, "io_file", name2, sizeof(name2) / sizeof(char *), NULL, &pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_in_uri(pipe2, "/sdcard/test.mp3"));
    dec_el = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe2, "aud_dec", &dec_el));
    esp_gmf_audio_dec_reconfig_by_sound_info(dec_el, &info);

    esp_gmf_pipeline_handle_t pipe3 = NULL;
    const char *name3[] = {"aud_mixer"};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pool_new_pipeline(pool, NULL, name3, sizeof(name3) / sizeof(char *), "io_codec_dev", &pipe3));
    gmf_setup_pipeline_out_dev(pipe3);
    // create rb
    esp_gmf_db_handle_t db = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db));
    esp_gmf_db_handle_t db2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_db_new_ringbuf(10, 1024, &db2));

    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db,
                                                               ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db,
                                                             ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe1, "aud_bit_cvt", out_port, pipe3, "aud_mixer", in_port));

    out_port = NEW_ESP_GMF_PORT_OUT_BYTE(esp_gmf_db_acquire_write, esp_gmf_db_release_write, esp_gmf_db_deinit, db2,
                                         ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    in_port = NEW_ESP_GMF_PORT_IN_BYTE(esp_gmf_db_acquire_read, esp_gmf_db_release_read, esp_gmf_db_deinit, db2,
                                       ESP_GMF_PORT_PAYLOAD_LEN_DEFAULT, 300);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_connect_pipe(pipe2, "aud_bit_cvt", out_port, pipe3, "aud_mixer", in_port));

    esp_gmf_task_cfg_t cfg1 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg1.ctx = NULL;
    cfg1.cb = NULL;
    cfg1.thread.core = 1;
    cfg1.thread.prio = 10;
    cfg1.name = "stream1";
    esp_gmf_task_handle_t work_task1 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg1, &work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe1, work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe1, _pipeline_event, pipe_sync_evt1));

    esp_gmf_task_cfg_t cfg2 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg2.thread.core = 0;
    cfg2.thread.prio = 10;
    cfg2.name = "stream2";
    esp_gmf_task_handle_t work_task2 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg2, &work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe2, work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe2, _pipeline_event2, pipe_sync_evt2));

    esp_gmf_task_cfg_t cfg3 = DEFAULT_ESP_GMF_TASK_CONFIG();
    cfg3.ctx = NULL;
    cfg3.cb = NULL;
    cfg3.thread.core = 0;
    cfg3.thread.prio = 5;
    cfg3.name = "mix_process";
    esp_gmf_task_handle_t work_task3 = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_init(&cfg3, &work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_bind_task(pipe3, work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_loading_jobs(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_set_event(pipe3, _pipeline_event3, pipe_sync_evt3));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_run(pipe3));
    esp_gmf_element_handle_t mixer_hd = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_get_el_by_name(pipe3, "aud_mixer", &mixer_hd));
    esp_ae_mixer_mode_t mode = ESP_AE_MIXER_MODE_FADE_UPWARD;
    xEventGroupWaitBits(pipe_sync_evt3, PIPELINE_BLOCK_RUN_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    // set mode
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_mixer_set_mode(mixer_hd, 0, mode));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_mixer_set_mode(mixer_hd, 1, mode));

    xEventGroupWaitBits(pipe_sync_evt1, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(pipe_sync_evt2, PIPELINE_BLOCK_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe1));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_stop(pipe3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_task_deinit(work_task3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipe3));
    gmf_unregister_audio_all(pool);
    esp_gmf_pool_deinit(pool);
    vEventGroupDelete(pipe_sync_evt1);
    vEventGroupDelete(pipe_sync_evt2);
    vEventGroupDelete(pipe_sync_evt3);
#ifdef MEDIA_LIB_MEM_TEST
    media_lib_stop_mem_trace();
#endif  /* MEDIA_LIB_MEM_TEST */
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_teardown_codec_dev();
    vTaskDelay(1000 / portTICK_RATE_MS);
    ESP_GMF_MEM_SHOW(TAG);
}
