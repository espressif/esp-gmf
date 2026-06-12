/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_gmf_err.h"

#include "esp_gmf_pool.h"
#include "gmf_loader_setup_defaults.h"

static const char *TAG = "GMF_LOADER";

esp_gmf_err_t gmf_loader_setup_all_defaults(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;

    ret = gmf_loader_setup_io_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register io");

    ret = gmf_loader_setup_audio_codec_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register codec");

    ret = gmf_loader_setup_audio_effects_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register effects");

    ret = gmf_loader_setup_ai_audio_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register ai audio");

    ret = gmf_loader_setup_audio_muxer_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register audio muxer");

    ret = gmf_loader_setup_video_codec_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register video codec");

    ret = gmf_loader_setup_video_effects_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register video effects");

    ret = gmf_loader_setup_misc_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to register misc");

    return ret;
}

esp_gmf_err_t gmf_loader_teardown_all_defaults(esp_gmf_pool_handle_t pool)
{
    ESP_GMF_NULL_CHECK(TAG, pool, return ESP_GMF_ERR_INVALID_ARG);
    esp_gmf_err_t ret = ESP_GMF_ERR_OK;

    ret = gmf_loader_teardown_io_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to teardown io");

    ret = gmf_loader_teardown_audio_codec_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to teardown audio codec");

    ret = gmf_loader_teardown_audio_effects_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to teardown audio effects");

    ret = gmf_loader_teardown_ai_audio_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to teardown ai audio");

    ret = gmf_loader_teardown_audio_muxer_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to teardown audio muxer");

    ret = gmf_loader_teardown_video_codec_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to teardown video codec");

    ret = gmf_loader_teardown_video_effects_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to teardown video effects");

    ret = gmf_loader_teardown_misc_default(pool);
    ESP_GMF_RET_ON_ERROR(TAG, ret, return ret, "Failed to teardown misc");

    return ret;
}
