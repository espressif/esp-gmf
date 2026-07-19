/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include "esp_gmf_err.h"
#include "esp_gmf_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Create and publish the Audio Render pipeline topology
 *
 * @note  When @p pipeline_num is greater than one, the last pipeline is the
 *        mixed pipeline and all preceding stream pipelines connect to it.
 * @note  Built only when `CONFIG_ESP_AUDIO_RENDER_PIPELINE_VIEW` is enabled.
 *
 * @param[in]  pipeline_num  Number of Audio Render pipelines
 *
 * @return
 *       - ESP_GMF_ERR_OK              On success
 *       - ESP_GMF_ERR_INVALID_ARG     Invalid argument
 *       - ESP_GMF_ERR_MEMORY_LACK     Not enough memory
 *       - ESP_GMF_ERR_ALREADY_EXISTS  Audio Render pipeline view already exists
 *       - Others                      Pipeline view error
 */
esp_gmf_err_t audio_render_pipeline_view_init(uint8_t pipeline_num);

/**
 * @brief  Update the current Audio Render GMF pipeline handles
 *
 * @param[in]  pipelines     Ordered pipeline handle array; an entry can be NULL
 * @param[in]  pipeline_num  Number of entries in @p pipelines
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - Others                   Pipeline view error
 */
esp_gmf_err_t audio_render_pipeline_view_update(const esp_gmf_pipeline_handle_t pipelines[], uint8_t pipeline_num);

/**
 * @brief  Remove the published Audio Render pipeline view
 */
void audio_render_pipeline_view_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
