/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "esp_gmf_pool.h"
#include "esp_audio_simple_player_private.h"

static const char *TAG = "ASP_ADVANCE";

esp_gmf_err_t esp_audio_simple_player_get_pool(esp_asp_handle_t handle, esp_gmf_pool_handle_t *pool)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, pool, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    *pool = player->pool;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_audio_simple_player_register_io(esp_asp_handle_t handle, esp_gmf_io_handle_t io)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    int ret = esp_gmf_pool_register_io(player->pool, io, NULL);
    return ret;
}

esp_gmf_err_t esp_audio_simple_player_register_el(esp_asp_handle_t handle, esp_gmf_element_handle_t element)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    int ret = esp_gmf_pool_register_element(player->pool, element, NULL);
    return ret;
}

esp_gmf_err_t esp_audio_simple_player_set_pipeline(esp_asp_handle_t handle, const char *in_name,
                                                     const char *el_name[], int num_of_el_name, const char *out_name)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    int ret = esp_gmf_pool_new_pipeline(player->pool, in_name, el_name, num_of_el_name, out_name, &player->pipe);
    return ret;
}

esp_gmf_err_t esp_audio_simple_player_get_pipeline(esp_asp_handle_t handle, esp_gmf_pipeline_handle_t *pipe)
{
    ESP_GMF_NULL_CHECK(TAG, handle, { return ESP_GMF_ERR_INVALID_ARG;});
    ESP_GMF_NULL_CHECK(TAG, pipe, { return ESP_GMF_ERR_INVALID_ARG;});
    esp_audio_simple_player_t *player = (esp_audio_simple_player_t *)handle;
    *pipe = player->pipe;
    return ESP_GMF_ERR_OK;
}
