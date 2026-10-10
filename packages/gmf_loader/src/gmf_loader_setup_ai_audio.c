/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_afe_config.h"

#include "esp_gmf_element.h"
#include "esp_gmf_err.h"
#include "esp_gmf_pool.h"

#if CONFIG_GMF_AI_AUDIO_INIT_AEC
#include "esp_gmf_aec.h"
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_AEC */

#if CONFIG_GMF_AI_AUDIO_INIT_WN
#include "esp_gmf_wn.h"
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_WN */

#ifdef CONFIG_GMF_AI_AUDIO_INIT_AFE
#include "esp_gmf_afe.h"
#include "esp_gmf_afe_manager.h"
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_AFE */

#if CONFIG_GMF_AI_AUDIO_INIT_VAD
#include "esp_gmf_vad.h"
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_VAD */

#if CONFIG_GMF_AI_AUDIO_INIT_NS
#include "esp_gmf_ns.h"
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_NS */

#if CONFIG_GMF_AI_AUDIO_INIT_DOA
#include "esp_gmf_doa.h"
#include "esp_gmf_obj.h"
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_DOA */

#include "esp_gmf_ai_audio_helper.h"

#if CONFIG_GMF_AI_AUDIO_LOAD_CH_ALLOCATION_FROM_BOARD
#include "esp_codec_dev.h"
#include "dev_audio_codec.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#endif  /* CONFIG_GMF_AI_AUDIO_LOAD_CH_ALLOCATION_FROM_BOARD */

typedef struct {
    uint32_t  setup_cnt;
#if defined(CONFIG_GMF_AI_AUDIO_INIT_WN) || defined(CONFIG_GMF_AI_AUDIO_INIT_AFE)
    srmodel_list_t *models;
#endif  /* defined(CONFIG_GMF_AI_AUDIO_INIT_WN) || defined(CONFIG_GMF_AI_AUDIO_INIT_AFE) */
#ifdef  CONFIG_GMF_AI_AUDIO_INIT_AFE
    esp_gmf_afe_manager_handle_t  afe_manager;
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_AFE */
    char  ch_allocation[ESP_GMF_AI_AUDIO_SR_FORMAT_MAX_CH + 1];
    bool  ch_allocation_from_board;
} gmf_ai_audio_ctx_t;

static const char *TAG = "GMF_SETUP_AI";
static gmf_ai_audio_ctx_t *ai_audio_ctx = NULL;

#define GMF_LOADER_CH_ALLOC(cfg)  ((ai_audio_ctx && ai_audio_ctx->ch_allocation_from_board) ? ai_audio_ctx->ch_allocation : (cfg))

#if CONFIG_GMF_AI_AUDIO_LOAD_CH_ALLOCATION_FROM_BOARD
static bool gmf_loader_load_ch_allocation_from_board(char *sr_format, size_t sr_format_size)
{
    dev_audio_codec_handles_t *adc = NULL;
    esp_err_t err = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_AUDIO_ADC, (void **)&adc);
    char label[ESP_GMF_AI_AUDIO_SR_FORMAT_MAX_CH * 16];
    if (err != ESP_OK || adc == NULL || adc->codec_dev == NULL ||
        esp_codec_dev_get_data_layout_label(adc->codec_dev, label, sizeof(label)) != ESP_CODEC_DEV_OK ||
        label[0] == '\0') {
        ESP_LOGW(TAG, "Data layout label unavailable, keep configured channel allocation");
        return false;
    }
    if (esp_gmf_ai_audio_ch_layout_to_sr_format(label, sr_format, sr_format_size) != ESP_GMF_ERR_OK) {
        ESP_LOGW(TAG, "Data layout label conversion failed, keep configured channel allocation");
        return false;
    }
    ESP_LOGI(TAG, "Channel allocation from data layout label \"%s\": %s", label, sr_format);
    return true;
}
#endif  /* CONFIG_GMF_AI_AUDIO_LOAD_CH_ALLOCATION_FROM_BOARD */

#ifdef CONFIG_GMF_AI_AUDIO_INIT_AEC
static esp_gmf_err_t gmf_loader_setup_default_aec(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t hd = NULL;
    esp_gmf_aec_cfg_t gmf_aec_cfg = {
        .filter_len = CONFIG_GMF_AI_AUDIO_AEC_FILTER_LEN,
        .type = CONFIG_GMF_AI_AUDIO_AEC_TYPE,
        .mode = CONFIG_GMF_AI_AUDIO_AEC_MODE,
        .input_format = (char *)GMF_LOADER_CH_ALLOC(CONFIG_GMF_AI_AUDIO_AEC_CH_ALLOCATION),
    };
    ret = esp_gmf_aec_init(&gmf_aec_cfg, &hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init audio aec");
    ret = esp_gmf_pool_register_element(pool, hd, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(hd); return ret;}, "Failed to register element in pool");
    return ret;
}
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_AEC */

#if CONFIG_GMF_AI_AUDIO_INIT_WN
static esp_gmf_err_t gmf_loader_setup_default_wn(esp_gmf_pool_handle_t pool, gmf_ai_audio_ctx_t *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t hd = NULL;
    esp_gmf_wn_cfg_t gmf_wn_cfg = {
        .input_format = (char *)GMF_LOADER_CH_ALLOC(CONFIG_GMF_AI_AUDIO_WN_CH_ALLOCATION),
    };
    gmf_wn_cfg.det_mode = CONFIG_GMF_AI_AUDIO_WN_DET_MODE;
    gmf_wn_cfg.models = ctx->models;
    ret = esp_gmf_wn_init(&gmf_wn_cfg, &hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init audio wn");
    ret = esp_gmf_pool_register_element(pool, hd, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(hd); return ret;}, "Failed to register element in pool");
    return ret;
}
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_WN */

#ifdef CONFIG_GMF_AI_AUDIO_INIT_AFE
static esp_gmf_err_t gmf_loader_setup_default_afe(esp_gmf_pool_handle_t pool, gmf_ai_audio_ctx_t *ctx)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t hd = NULL;
    esp_gmf_afe_cfg_t gmf_afe_cfg = DEFAULT_GMF_AFE_CFG(ctx->afe_manager, NULL, NULL, ctx->models);
#if defined(CONFIG_GMF_AI_AUDIO_VOICE_COMMAND_ENABLE)
    gmf_afe_cfg.vcmd_detect_en = true;
#else
    gmf_afe_cfg.vcmd_detect_en = false;
#endif  /* defined(CONFIG_GMF_AI_AUDIO_VOICE_COMMAND_ENABLE) */
    ret = esp_gmf_afe_init(&gmf_afe_cfg, &hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init audio afe");
    ret = esp_gmf_pool_register_element(pool, hd, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(hd); return ret;}, "Failed to register element in pool");

    return ret;
}
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_AFE */

#if CONFIG_GMF_AI_AUDIO_INIT_VAD
static esp_gmf_err_t gmf_loader_setup_default_vad(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t hd = NULL;
    esp_gmf_vad_cfg_t gmf_vad_cfg = ESP_GMF_VAD_CFG_DEFAULT();
    gmf_vad_cfg.sample_rate = CONFIG_GMF_AI_AUDIO_VAD_ELEMENT_SAMPLE_RATE;
    gmf_vad_cfg.frame_ms = CONFIG_GMF_AI_AUDIO_VAD_ELEMENT_FRAME_MS;
    gmf_vad_cfg.vad_mode = CONFIG_GMF_AI_AUDIO_VAD_ELEMENT_MODE;
    gmf_vad_cfg.partition_label = CONFIG_GMF_AI_AUDIO_MODEL_PARTITION;
    ret = esp_gmf_vad_init(&gmf_vad_cfg, &hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init audio vad");
    ret = esp_gmf_pool_register_element(pool, hd, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_obj_delete(hd); return ret;}, "Failed to register element in pool");
    return ret;
}
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_VAD */

#if CONFIG_GMF_AI_AUDIO_INIT_NS
static esp_gmf_err_t gmf_loader_setup_default_ns(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t hd = NULL;
    esp_gmf_ns_cfg_t gmf_ns_cfg = ESP_GMF_NS_CFG_DEFAULT();
    gmf_ns_cfg.sample_rate = CONFIG_GMF_AI_AUDIO_NS_ELEMENT_SAMPLE_RATE;
    gmf_ns_cfg.frame_ms = CONFIG_GMF_AI_AUDIO_NS_ELEMENT_FRAME_MS;
    gmf_ns_cfg.model_name = CONFIG_GMF_AI_AUDIO_NS_ELEMENT_MODEL_NAME;
    gmf_ns_cfg.partition_label = CONFIG_GMF_AI_AUDIO_MODEL_PARTITION;
    ret = esp_gmf_ns_init(&gmf_ns_cfg, &hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init audio ns");
    ret = esp_gmf_pool_register_element(pool, hd, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_obj_delete(hd); return ret;}, "Failed to register element in pool");
    return ret;
}
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_NS */

#if CONFIG_GMF_AI_AUDIO_INIT_DOA
static esp_gmf_err_t gmf_loader_setup_default_doa(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t hd = NULL;
    esp_gmf_doa_cfg_t gmf_doa_cfg = {
        .sample_rate = CONFIG_GMF_AI_AUDIO_DOA_ELEMENT_SAMPLE_RATE,
        .resolution = CONFIG_GMF_AI_AUDIO_DOA_ELEMENT_RESOLUTION,
        .d_mics = CONFIG_GMF_AI_AUDIO_DOA_ELEMENT_D_MICS_MM / 1000.0f,
        .frame_ms = CONFIG_GMF_AI_AUDIO_DOA_ELEMENT_FRAME_MS,
        .input_format = GMF_LOADER_CH_ALLOC(CONFIG_GMF_AI_AUDIO_DOA_ELEMENT_CH_ALLOCATION),
        .result_callback = NULL,
        .ctx = NULL,
    };
    ret = esp_gmf_doa_init(&gmf_doa_cfg, &hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init audio doa");
    ret = esp_gmf_pool_register_element(pool, hd, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_obj_delete(hd); return ret;}, "Failed to register element in pool");
    return ret;
}
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_DOA */

esp_gmf_err_t gmf_loader_setup_ai_audio_default(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;

    if (ai_audio_ctx == NULL) {
        ai_audio_ctx = esp_gmf_oal_calloc(1, sizeof(gmf_ai_audio_ctx_t));
        ESP_GMF_MEM_CHECK(TAG, ai_audio_ctx, return ESP_GMF_ERR_MEMORY_LACK;);
        ai_audio_ctx->setup_cnt = 0;
#if defined(CONFIG_GMF_AI_AUDIO_INIT_WN) || defined(CONFIG_GMF_AI_AUDIO_INIT_AFE)
        ai_audio_ctx->models = esp_srmodel_init(CONFIG_GMF_AI_AUDIO_MODEL_PARTITION);
#endif  /* defined(CONFIG_GMF_AI_AUDIO_INIT_WN) || defined(CONFIG_GMF_AI_AUDIO_INIT_AFE) */
#if CONFIG_GMF_AI_AUDIO_LOAD_CH_ALLOCATION_FROM_BOARD
        ai_audio_ctx->ch_allocation_from_board = gmf_loader_load_ch_allocation_from_board(
            ai_audio_ctx->ch_allocation, sizeof(ai_audio_ctx->ch_allocation));
#endif  /* CONFIG_GMF_AI_AUDIO_LOAD_CH_ALLOCATION_FROM_BOARD */
#ifdef CONFIG_GMF_AI_AUDIO_INIT_AFE
        esp_gmf_err_t ret = ESP_GMF_ERR_OK;
        afe_config_t *afe_cfg = afe_config_init(GMF_LOADER_CH_ALLOC(CONFIG_GMF_AI_AUDIO_AFE_CH_ALLOCATION),
                                                ai_audio_ctx->models,
                                                AFE_TYPE_SR,
                                                AFE_MODE_HIGH_PERF);
#if defined(CONFIG_GMF_AI_AUDIO_AFE_VAD_ENABLE)
        afe_cfg->vad_init = true;
        afe_cfg->vad_mode = CONFIG_GMF_AI_AUDIO_VAD_MODE;
        afe_cfg->vad_min_speech_ms = CONFIG_GMF_AI_AUDIO_VAD_MIN_SPEECH;
        afe_cfg->vad_min_noise_ms = CONFIG_GMF_AI_AUDIO_VAD_MIN_NOISE;
#else
        afe_cfg->vad_init = false;
#endif  /* defined(CONFIG_GMF_AI_AUDIO_AFE_VAD_ENABLE) */
#if defined(CONFIG_GMF_AI_AUDIO_WAKEUP_ENABLE)
        afe_cfg->wakenet_init = true;
#else
        afe_cfg->wakenet_init = false;
#endif  /* defined(CONFIG_GMF_AI_AUDIO_WAKEUP_ENABLE) */

#if defined(CONFIG_GMF_AI_AUDIO_AFE_AEC_ENABLE)
        afe_cfg->aec_init = true;
#else
        afe_cfg->aec_init = false;
#endif  /* defined(CONFIG_GMF_AI_AUDIO_AFE_AEC_ENABLE) */
        esp_gmf_afe_manager_cfg_t afe_manager_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(afe_cfg, NULL, NULL, NULL, NULL);
        afe_manager_cfg.feed_task_setting.core = CONFIG_GMF_AI_AUDIO_FEED_TASK_CORE_ID;
        afe_manager_cfg.feed_task_setting.prio = CONFIG_GMF_AI_AUDIO_FEED_TASK_PRIORITY;
        afe_manager_cfg.feed_task_setting.stack_size = CONFIG_GMF_AI_AUDIO_FEED_TASK_STACK_SIZE;
        afe_manager_cfg.fetch_task_setting.core = CONFIG_GMF_AI_AUDIO_FETCH_TASK_CORE_ID;
        afe_manager_cfg.fetch_task_setting.prio = CONFIG_GMF_AI_AUDIO_FETCH_TASK_PRIORITY;
        afe_manager_cfg.fetch_task_setting.stack_size = CONFIG_GMF_AI_AUDIO_FETCH_TASK_STACK_SIZE;
        ret = esp_gmf_afe_manager_create(&afe_manager_cfg, &ai_audio_ctx->afe_manager);
        ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to create afe manager");
        afe_config_free(afe_cfg);
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_AFE */
    }
    ai_audio_ctx->setup_cnt++;

#ifdef CONFIG_GMF_AI_AUDIO_INIT_AEC
    ret = gmf_loader_setup_default_aec(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register aec");
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_AEC */

#ifdef CONFIG_GMF_AI_AUDIO_INIT_WN
    ret = gmf_loader_setup_default_wn(pool, ai_audio_ctx);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register wn");
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_WN */

#ifdef CONFIG_GMF_AI_AUDIO_INIT_AFE
    ret = gmf_loader_setup_default_afe(pool, ai_audio_ctx);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register afe");
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_AFE */

#ifdef CONFIG_GMF_AI_AUDIO_INIT_VAD
    ret = gmf_loader_setup_default_vad(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register vad");
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_VAD */

#ifdef CONFIG_GMF_AI_AUDIO_INIT_NS
    ret = gmf_loader_setup_default_ns(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register ns");
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_NS */

#ifdef CONFIG_GMF_AI_AUDIO_INIT_DOA
    ret = gmf_loader_setup_default_doa(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register doa");
#endif  /* CONFIG_GMF_AI_AUDIO_INIT_DOA */

    return ret;
}

esp_gmf_err_t gmf_loader_teardown_ai_audio_default(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);

    if (ai_audio_ctx == NULL || ai_audio_ctx->setup_cnt == 0) {
        ESP_LOGI(TAG, "AI audio context not initialized");
        return ESP_GMF_ERR_INVALID_STATE;
    }
    if ((--ai_audio_ctx->setup_cnt) == 0) {
#if defined(CONFIG_GMF_AI_AUDIO_INIT_AFE)
        if (ai_audio_ctx->afe_manager) {
            esp_gmf_afe_manager_destroy(ai_audio_ctx->afe_manager);
            ai_audio_ctx->afe_manager = NULL;
        }
#endif  /* defined(CONFIG_GMF_AI_AUDIO_INIT_AFE) */
#if defined(CONFIG_GMF_AI_AUDIO_INIT_WN) || defined(CONFIG_GMF_AI_AUDIO_INIT_AFE)
        if (ai_audio_ctx->models) {
            esp_srmodel_deinit(ai_audio_ctx->models);
            ai_audio_ctx->models = NULL;
        }
#endif  /* defined(CONFIG_GMF_AI_AUDIO_INIT_WN) || defined(CONFIG_GMF_AI_AUDIO_INIT_AFE) */
        esp_gmf_oal_free(ai_audio_ctx);
        ai_audio_ctx = NULL;
    } else {
        ESP_LOGW(TAG, "AI audio context still in use");
    }

    return ESP_GMF_ERR_OK;
}
