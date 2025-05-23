/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_path_mngr.h"
#include "capture_gmf_mngr.h"
#include "esp_gmf_task.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  GMF capture path manager structure
 *
 * @note  This is an opaque type that represents the path manager
 *        The actual implementation is defined in the source file
 */
typedef struct gmf_capture_path_mngr_t gmf_capture_path_mngr_t;

/**
 * @brief  GMF capture path resource structure
 *
 * @note  This is an opaque type that represents a path resource
 *        The actual implementation is defined in the source file
 */
typedef struct gmf_capture_path_res_t gmf_capture_path_res_t;

/**
 * @brief  Callback function type for preparing all paths
 *
 * @param[in]  mngr  Path manager instance
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to prepare paths
 */
typedef esp_capture_err_t (*gmf_capture_prepare_all_path_cb)(gmf_capture_path_mngr_t *mngr);

/**
 * @brief  Callback function type for preparing a single path
 *
 * @param[in]  path  Path resource to prepare
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to prepare path
 */
typedef esp_capture_err_t (*gmf_capture_prepare_path_cb)(gmf_capture_path_res_t *path);

/**
 * @brief  Callback function type for stopping a single path
 *
 * @param[in]  path  Path resource to stop
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to stop path
 */
typedef esp_capture_err_t (*gmf_capture_stop_path_cb)(gmf_capture_path_res_t *path);

/**
 * @brief  Callback function type for releasing a single path
 *
 * @param[in]  path  Path resource to release
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to release path
 */
typedef esp_capture_err_t (*gmf_capture_release_path_cb)(gmf_capture_path_res_t *path);

/**
 * @brief  GMF capture path resource structure
 *
 * @note  This structure contains information about a single path resource,
 *        including its state and configuration
 */
struct gmf_capture_path_res_t {
    gmf_capture_path_mngr_t  *parent;          /*!< Parent path manager instance */
    uint8_t                   path;            /*!< Path type identifier */
    uint8_t                   configured : 1;  /*!< Whether path is configured */
    uint8_t                   negotiated : 1;  /*!< Whether path is negotiated */
    uint8_t                   enable     : 1;  /*!< Whether path is enabled */
    uint8_t                   started    : 1;  /*!< Whether path is started */
};

/**
 * @brief  GMF capture pipeline reference structure
 *
 * @note  Pipeline reference is used to find pipeline manager and related resources
 *        When receive pipeline event notify
 */
typedef struct {
    esp_capture_gmf_pipeline_t  *pipeline;   /*!< Pipeline */
    gmf_capture_path_mngr_t     *parent;     /*!< Reference to pipeline manager */
} gmf_capture_pipeline_ref_t;

/**
 * @brief  GMF capture path manager structure
 *
 * @note  This structure manages multiple capture paths and their resources,
 *        including pipelines, tasks, and path states.
 */
struct gmf_capture_path_mngr_t {
    esp_capture_pipeline_builder_if_t  *pipeline_builder;  /*!< Pipeline builder interface */
    esp_capture_stream_type_t           stream_type;       /*!< Stream type (audio/video) */
    esp_capture_path_cfg_t              cfg;               /*!< Path configuration */
    // Resources
    esp_capture_gmf_pipeline_t         *pipeline;          /*!< Array of pipeline information */
    gmf_capture_pipeline_ref_t         *pipeline_ref;      /*!< Array of pipeline references */
    esp_gmf_task_handle_t              *task;              /*!< Array of task handles */
    uint8_t                            *run_mask;          /*!< Array of run masks for each path */
    uint8_t                             pipeline_num;      /*!< Number of pipelines */
    gmf_capture_path_res_t             *res;               /*!< Array of path resources */
    uint8_t                             path_num;          /*!< Number of paths */
    int                                 res_size;          /*!< Size of resource array */
    bool                                started;           /*!< Whether manager is started */
};

/**
 * @brief  Open a new GMF capture path manager
 *
 * @param[in]  mngr         Pointer to path manager instance
 * @param[in]  stream_type  Type of stream (audio/video)
 * @param[in]  cfg          Path configuration
 * @param[in]  path_size    Size of path resource array
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to open path manager
 */
esp_capture_err_t gmf_capture_path_mngr_open(gmf_capture_path_mngr_t *mngr, esp_capture_stream_type_t stream_type,
                                             esp_capture_path_cfg_t *cfg, int path_size);

/**
 * @brief  Get path resource by path type
 *
 * @param[in]  mngr  Path manager instance
 * @param[in]  path  Path type to find
 *
 * @return
 *       - NULL    Path manager invalid or path type not existed
 *       - Others  Pointer to path resource
 */
gmf_capture_path_res_t *gmf_capture_path_mngr_get_path(gmf_capture_path_mngr_t *mngr, uint8_t path);

/**
 * @brief  Get path resource by index
 *
 * @param[in]  mngr  Path manager instance
 * @param[in]  idx   Index in resource array
 *
 * @return
 *       - NULL    Path manager invalid or index not existed
 *       - Others  Pointer to path resource
 */
gmf_capture_path_res_t *gmf_capture_path_mngr_get_idx(gmf_capture_path_mngr_t *mngr, uint8_t idx);

/**
 * @brief  Add a new path to the manager
 *
 * @param[in]  mngr  Path manager instance
 * @param[in]  path  Path type to add
 * @param[in]  sink  Sink configuration
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to add path
 */
esp_capture_err_t gmf_capture_path_mngr_add_path(gmf_capture_path_mngr_t *mngr, uint8_t path, esp_capture_stream_info_t *sink);

/**
 * @brief  Enable or disable a path
 *
 * @param[in]  mngr        Path manager instance
 * @param[in]  path        Path type to enable/disable
 * @param[in]  enable      true to enable, false to disable
 * @param[in]  prepare_cb  Callback for preparing path
 * @param[in]  stop_cb     Callback for stopping path
 * @param[in]  release_cb  Callback for releasing path
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to enable/disable path
 */
esp_capture_err_t gmf_capture_path_mngr_enable_path(gmf_capture_path_mngr_t *mngr, uint8_t path, bool enable,
                                                    gmf_capture_prepare_path_cb prepare_cb, gmf_capture_stop_path_cb stop_cb,
                                                    gmf_capture_release_path_cb release_cb);

/**
 * @brief  Start the path manager
 *
 * @param[in]  mngr         Path manager instance
 * @param[in]  prepare_all  Callback for preparing all paths
 * @param[in]  prepare_cb   Callback for preparing individual paths
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to start manager
 */
esp_capture_err_t gmf_capture_path_mngr_start(gmf_capture_path_mngr_t *mngr, gmf_capture_prepare_all_path_cb prepare_all,
                                              gmf_capture_prepare_path_cb prepare_cb);

/**
 * @brief  Handle frame reached event for a path
 *
 * @param[in]  res    Path resource
 * @param[in]  frame  Frame information
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to handle frame
 */
esp_capture_err_t gmf_capture_path_mngr_frame_reached(gmf_capture_path_res_t *res, esp_capture_stream_frame_t *frame);

/**
 * @brief  Stop the path manager
 *
 * @param[in]  mngr        Path manager instance
 * @param[in]  stop_cb     Callback for stopping paths
 * @param[in]  release_cb  Callback for releasing paths
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to stop manager
 */
esp_capture_err_t gmf_capture_path_mngr_stop(gmf_capture_path_mngr_t *mngr, gmf_capture_stop_path_cb stop_cb,
                                             gmf_capture_release_path_cb release_cb);

/**
 * @brief  Close the path manager
 *
 * @param[in]  mngr  Path manager instance
 *
 * @return
 *       - ESP_CAPTURE_ERR_OK  On success
 *       - Others              Failed to close manager
 */
esp_capture_err_t gmf_capture_path_mngr_close(gmf_capture_path_mngr_t *mngr);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
