/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <esp_gmf_pipeline.h>
#include <esp_gmf_pool.h>
#include <esp_gmf_element.h>
#include "audio_render_proc.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Audio render pipeline configuration
 */
typedef struct {
    esp_audio_render_sample_info_t  in_sample_info;   /*!< Input audio sample information */
    esp_audio_render_sample_info_t  out_sample_info;  /*!< Output audio sample information */
    esp_gmf_port_handle_t           in_port;          /*!< Input port */
    esp_gmf_port_handle_t           out_port;         /*!< Output port */
    esp_gmf_pool_handle_t           pool;             /*!< GMF element pool */
    esp_gmf_element_handle_t       *proc_elements;    /*!< Pre-created process element */
    uint8_t                         proc_num;         /*!< Pre-created element handle */
} audio_render_pipeline_cfg_t;

/**
 * @brief  Create element from GMF element pool by processor type
 *
 * @param[in]  pool       GMF pool
 * @param[in]  proc_type  Processor type
 *
 * @return
 *       - NULL    Processor element not found or no resource
 *       - Others  Processor element handle
 */
esp_gmf_element_handle_t audio_render_create_element(esp_gmf_pool_handle_t pool, esp_audio_render_proc_type_t proc_type);

/**
 * @brief  Open audio render pipeline
 *
 * @param[in]   pipe_cfg   Pipeline configuration
 * @param[out]  pipeline   Pipeline handle to store
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK           On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG  Invalid input argument
 *       - ESP_AUDIO_RENDER_ERR_NO_RESOURCE  No resources
 */
esp_audio_render_err_t audio_render_pipeline_open(audio_render_pipeline_cfg_t *pipe_cfg,
                                                  esp_gmf_pipeline_handle_t *pipeline);

/**
 * @brief  Get element handle by process type from pipeline
 *
 * @param[in]  pipeline   Pipeline handle
 * @param[in]  proc_type  Processor type
 *
 * @return
 *       - NULL    Element not found in pipeline
 *       - Others  Element handle for the processor type
 */
esp_gmf_element_handle_t audio_render_pipeline_get_element(esp_gmf_pipeline_handle_t pipeline,
                                                           esp_audio_render_proc_type_t proc_type);

/**
 * @brief  Close audio processor pipeline
 *
 * @param[in]  pipeline   Pipeline handle
 * @param[in]  kept_elements  Elements to be kept in pipeline
 * @param[in]  kept_num       Element number to be kept
 *
 * @return
 *       - ESP_AUDIO_RENDER_ERR_OK           On success
 *       - ESP_AUDIO_RENDER_ERR_INVALID_ARG  Invalid input argument
 */
esp_audio_render_err_t audio_render_pipeline_close(esp_gmf_pipeline_handle_t pipeline,
                                                   esp_gmf_element_handle_t *kept_elements, uint8_t kept_num);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
