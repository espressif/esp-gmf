/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "esp_gmf_audio_muxer.h"
#include "esp_muxer.h"
#include "esp_muxer_default.h"
#include "esp_gmf_element.h"
#include "esp_gmf_err.h"
#include "esp_gmf_pool.h"
#include "sdkconfig.h"

static const char *TAG = "GMF_SETUP_AUD_MUXER";

#ifdef CONFIG_GMF_AUDIO_MUXER_INIT
static esp_muxer_audio_codec_t get_muxer_audio_codec(void)
{
#ifdef CONFIG_GMF_AUDIO_MUXER_AUDIO_CODEC_AAC
    return ESP_MUXER_ADEC_AAC;
#elif defined(CONFIG_GMF_AUDIO_MUXER_AUDIO_CODEC_PCM)
    return ESP_MUXER_ADEC_PCM;
#elif defined(CONFIG_GMF_AUDIO_MUXER_AUDIO_CODEC_MP3)
    return ESP_MUXER_ADEC_MP3;
#elif defined(CONFIG_GMF_AUDIO_MUXER_AUDIO_CODEC_ADPCM)
    return ESP_MUXER_ADEC_ADPCM;
#elif defined(CONFIG_GMF_AUDIO_MUXER_AUDIO_CODEC_G711A)
    return ESP_MUXER_ADEC_G711_A;
#elif defined(CONFIG_GMF_AUDIO_MUXER_AUDIO_CODEC_G711U)
    return ESP_MUXER_ADEC_G711_U;
#elif defined(CONFIG_GMF_AUDIO_MUXER_AUDIO_CODEC_AMR_NB)
    return ESP_MUXER_ADEC_AMR_NB;
#elif defined(CONFIG_GMF_AUDIO_MUXER_AUDIO_CODEC_AMR_WB)
    return ESP_MUXER_ADEC_AMR_WB;
#elif defined(CONFIG_GMF_AUDIO_MUXER_AUDIO_CODEC_ALAC)
    return ESP_MUXER_ADEC_ALAC;
#elif defined(CONFIG_GMF_AUDIO_MUXER_AUDIO_CODEC_OPUS)
    return ESP_MUXER_ADEC_OPUS;
#else
    return ESP_MUXER_ADEC_NONE;
#endif  /* CONFIG_GMF_AUDIO_MUXER_AUDIO_CODEC_AAC */
}

static esp_muxer_type_t get_default_muxer_type(void)
{
#ifdef CONFIG_GMF_AUDIO_MUXER_DEFAULT_TYPE_TS
    return ESP_MUXER_TYPE_TS;
#elif defined(CONFIG_GMF_AUDIO_MUXER_DEFAULT_TYPE_MP4)
    return ESP_MUXER_TYPE_MP4;
#elif defined(CONFIG_GMF_AUDIO_MUXER_DEFAULT_TYPE_FLV)
    return ESP_MUXER_TYPE_FLV;
#elif defined(CONFIG_GMF_AUDIO_MUXER_DEFAULT_TYPE_WAV)
    return ESP_MUXER_TYPE_WAV;
#elif defined(CONFIG_GMF_AUDIO_MUXER_DEFAULT_TYPE_CAF)
    return ESP_MUXER_TYPE_CAF;
#elif defined(CONFIG_GMF_AUDIO_MUXER_DEFAULT_TYPE_OGG)
    return ESP_MUXER_TYPE_OGG;
#else
    return ESP_MUXER_TYPE_TS;
#endif  /* CONFIG_GMF_AUDIO_MUXER_DEFAULT_TYPE_TS */
}

static esp_gmf_err_t gmf_loader_setup_default_muxer(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;
    esp_gmf_element_handle_t hd = NULL;
    ret = esp_muxer_register_default();
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio muxers");
    // Get default muxer type for element initialization
    esp_muxer_type_t muxer_type = get_default_muxer_type();
    // Get output type from configuration
    esp_gmf_audio_muxer_output_type_t output_type;
#ifdef CONFIG_GMF_AUDIO_MUXER_OUTPUT_TYPE_STREAMING
    output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_STREAMING;
#else
    output_type = ESP_GMF_AUDIO_MUXER_OUTPUT_FILE;  // Default to streaming
#endif  /* CONFIG_GMF_AUDIO_MUXER_OUTPUT_TYPE_STREAMING */

    // Prepare muxer config
    esp_gmf_audio_muxer_cfg_t muxer_cfg = {
        .muxer_type = muxer_type,
        .output_type = output_type,
        .slice_duration = CONFIG_GMF_AUDIO_MUXER_SLICE_DURATION,
        .url_pattern = NULL,  // Use default url_pattern
        .url_ctx = NULL,      // Use default url_ctx
        .codec = get_muxer_audio_codec(),
    };

    ESP_LOGI(TAG, "Muxer config: type=%d, output_type=%d, codec=%d", muxer_type, output_type, muxer_cfg.codec);

    ret = esp_gmf_audio_muxer_init(&muxer_cfg, &hd);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to init audio muxer");

    ret = esp_gmf_pool_register_element(pool, hd, NULL);
    ESP_GMF_RET_ON_ERROR(TAG, ret, {esp_gmf_element_deinit(hd); return ret;}, "Failed to register element in pool");

    ESP_LOGI(TAG, "Audio muxer initialized, type: %d, codec: %d", muxer_type, muxer_cfg.codec);
    return ret;
}
#endif  /* CONFIG_GMF_AUDIO_MUXER_INIT */

esp_gmf_err_t gmf_loader_setup_audio_muxer_default(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
#ifdef CONFIG_GMF_AUDIO_MUXER_INIT
    return gmf_loader_setup_default_muxer(pool);
#else
    return ESP_GMF_ERR_OK;
#endif  /* CONFIG_GMF_AUDIO_MUXER_INIT */
}

esp_gmf_err_t gmf_loader_teardown_audio_muxer_default(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
#ifdef CONFIG_GMF_AUDIO_MUXER_INIT
    esp_muxer_unregister_default();
#endif  /* CONFIG_GMF_AUDIO_MUXER_INIT */
    return ESP_GMF_ERR_OK;
}
