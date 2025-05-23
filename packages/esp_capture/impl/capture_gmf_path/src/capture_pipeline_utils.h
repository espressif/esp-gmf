/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "capture_pipeline_builder.h"
#include "esp_capture_path_mngr.h"
#include "esp_gmf_pipeline.h"
#include "capture_os.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Safely free a pointer and set it to NULL
 *
 * @note  This macro checks if the pointer is not NULL before freeing it
 *        and ensures the pointer is set to NULL after freeing.
 */
#define SAFE_FREE(ptr) if (ptr) {  \
    capture_free(ptr);             \
    ptr = NULL;                    \
}

/**
 * @brief  Get the index of the first set bit in a path mask
 *
 * @note  This macro uses built-in function to find the position of the
 *        highest set bit in the path mask.
 */
#define GET_PATH_IDX(path_mask) (sizeof(int) * 8 - __builtin_clz(path_mask) - 1)

/**
 * @brief  Get the number of paths in a pipeline
 *
 * @param[in]  pipeline  Array of pipeline information
 * @param[in]  num       Number of pipelines in the array
 *
 * @return
 */
uint8_t capture_pipeline_get_path_num(esp_capture_gmf_pipeline_t *pipeline, uint8_t num);

/**
 * @brief  Check if a pipeline handle is a sink
 *
 * @param[in]  h  Pipeline handle to check
 *
 * @return
 *       - true   Pipeline is a sink
 *       - false  Pipeline is not a sink
 */
bool capture_pipeline_is_sink(esp_gmf_pipeline_handle_t h);

/**
 * @brief  Check if a pipeline handle is a source
 *
 * @param[in]  h          Pipeline handle to check
 * @param[in]  pipelines  Array of pipeline information
 * @param[in]  num        Number of pipelines in the array
 *
 * @return
 *       - true   Pipeline is a source
 *       - false  Pipeline is not a source
 */
bool capture_pipeline_is_src(esp_gmf_pipeline_handle_t h, esp_capture_gmf_pipeline_t *pipelines, uint8_t num);

/**
 * @brief  Get the pipeline information for a given handle
 *
 * @param[in]  h          Pipeline handle to find
 * @param[in]  pipelines  Array of pipeline information
 * @param[in]  num        Number of pipelines in the array
 *
 * @return
 *       - Pointer to the matching pipeline information, or NULL if not found
 */
esp_capture_gmf_pipeline_t *capture_pipeline_get_matched(esp_gmf_pipeline_handle_t h, esp_capture_gmf_pipeline_t *pipelines, uint8_t num);

/**
 * @brief  Sort pipelines by path mask
 *
 * @param[in]  pipeline  Array of pipeline information to sort
 * @param[in]  num       Number of pipelines in the array
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to sort pipelines
 */
esp_capture_err_t capture_pipeline_sort(esp_capture_gmf_pipeline_t *pipeline, uint8_t num);

/**
 * @brief  Verify pipeline configuration for a specific path
 *
 * @param[in]  pipelines  Array of pipeline information
 * @param[in]  num        Number of pipelines in the array
 * @param[in]  path       Path to verify
 *
 * @return
 *       - true   Pipeline configuration is valid
 *       - false  Pipeline configuration is invalid
 */
bool capture_pipeline_verify(esp_capture_gmf_pipeline_t *pipelines, uint8_t num, uint8_t path);

/**
 * @brief  Get element handle from pipeline by capability eight-CC
 *
 * @param[in]  pipeline  Pipeline handle
 * @param[in]  caps_cc   Capture eight-CC representation for the element
 *
 * @return
 *       - NULL    Not found element or invalid argument
 *       - Others  Element handle matched with input capability
 */
esp_gmf_element_handle_t capture_get_element_by_caps(esp_gmf_pipeline_handle_t pipeline, uint64_t caps_cc);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
