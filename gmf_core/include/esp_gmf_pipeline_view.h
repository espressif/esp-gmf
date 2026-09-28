/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include "esp_gmf_err.h"
#include "esp_gmf_node.h"
#include "esp_gmf_pipeline.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Pipeline view
 *
 *         A pipeline view is a named topology published by a provider for
 *         tools such as ESP Audio Studio. It lists pipelines by name and
 *         records a logical predecessor/successor relation between them.
 *
 * @note  A view connection is topology metadata only. It does not call
 *         `esp_gmf_pipeline_connect_pipe`, bind ports, or move data. The
 *         runtime data path stays in the pipeline itself. The view can
 *         describe that one named pipeline comes before another even when
 *         the pipeline handle is still NULL.
 */

/**
 * @brief  Named pipeline item in a pipeline view
 */
typedef struct esp_gmf_pipeline_view_item {
    esp_gmf_node_t             node;      /*!< List node */
    char                      *name;      /*!< Pipeline name owned by the view */
    esp_gmf_pipeline_handle_t  pipeline;  /*!< Current pipeline handle, or NULL */
} esp_gmf_pipeline_view_item_t;

/**
 * @brief  Logical predecessor/successor relation between two named pipelines
 *
 * @note  This is not a runtime data-path link. It does not connect ports
 *         or transfer data. Use `esp_gmf_pipeline_connect_pipe` for that.
 */
typedef struct esp_gmf_pipeline_view_connection {
    esp_gmf_node_t                node;  /*!< List node */
    esp_gmf_pipeline_view_item_t *from;  /*!< Predecessor pipeline item */
    esp_gmf_pipeline_view_item_t *to;    /*!< Successor pipeline item */
} esp_gmf_pipeline_view_connection_t;

/**
 * @brief  Named pipelines and their logical predecessor/successor relations
 */
typedef struct {
    esp_gmf_pipeline_view_item_t       *pipelines;    /*!< Pipeline item list */
    esp_gmf_pipeline_view_connection_t *connections;  /*!< Logical relation list */
} esp_gmf_pipeline_view_t;

/**
 * @brief  Get the currently published pipeline view of one provider
 *
 * @note  The returned view and pipeline handles are owned by the provider. The
 *        caller must use them synchronously and must not retain them.
 *
 * @param[out]  view  Published pipeline view
 *
 * @return
 *       - ESP_GMF_ERR_OK         On success
 *       - ESP_GMF_ERR_NOT_FOUND  No view is currently published
 *       - Others                 Provider error
 */
typedef esp_gmf_err_t (*esp_gmf_pipeline_view_get_func)(const esp_gmf_pipeline_view_t **view);

/**
 * @brief  Statically registered pipeline view provider connector
 */
typedef struct {
    esp_gmf_pipeline_view_get_func  get_view;  /*!< Get the published pipeline view */
} esp_gmf_pipeline_view_connector_t;

/**
 * @brief  Register a pipeline view connector at link time
 *
 * @param[in]  name      Connector symbol name
 * @param[in]  get_func  Provider view function
 */
#define ESP_GMF_PIPELINE_VIEW_CONNECTOR_REGISTER(name, get_func)             \
    static const esp_gmf_pipeline_view_connector_t name                      \
    __attribute__((section(".esp_gmf_pipeline_view_connectors"), used)) = {  \
        .get_view = (get_func), }

/**
 * @brief  Create an empty pipeline view
 *
 * @param[out]  view  Created pipeline view
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_MEMORY_LACK  Not enough memory
 */
esp_gmf_err_t esp_gmf_pipeline_view_init(esp_gmf_pipeline_view_t **view);

/**
 * @brief  Destroy a pipeline view without destroying its pipeline handles
 *
 * @param[in]  view  Pipeline view
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 */
esp_gmf_err_t esp_gmf_pipeline_view_deinit(esp_gmf_pipeline_view_t *view);

/**
 * @brief  Add a named pipeline to a view
 *
 * @param[in]  view      Pipeline view
 * @param[in]  name      Pipeline name unique within the view
 * @param[in]  pipeline  Current pipeline handle, or NULL
 *
 * @return
 *       - ESP_GMF_ERR_OK              On success
 *       - ESP_GMF_ERR_INVALID_ARG     Invalid argument
 *       - ESP_GMF_ERR_ALREADY_EXISTS  Pipeline name already exists
 *       - ESP_GMF_ERR_MEMORY_LACK     Not enough memory
 */
esp_gmf_err_t esp_gmf_pipeline_view_add_pipeline(esp_gmf_pipeline_view_t *view, const char *name,
                                                 esp_gmf_pipeline_handle_t pipeline);

/**
 * @brief  Replace the current handle of a named pipeline
 *
 * @param[in]  view      Pipeline view
 * @param[in]  name      Pipeline name
 * @param[in]  pipeline  Current pipeline handle, or NULL
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_NOT_FOUND    Pipeline name does not exist
 */
esp_gmf_err_t esp_gmf_pipeline_view_set_pipeline(esp_gmf_pipeline_view_t *view, const char *name,
                                                 esp_gmf_pipeline_handle_t pipeline);

/**
 * @brief  Remove a named pipeline and all of its logical relations
 *
 * @param[in]  view  Pipeline view
 * @param[in]  name  Pipeline name
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_NOT_FOUND    Pipeline name does not exist
 */
esp_gmf_err_t esp_gmf_pipeline_view_remove_pipeline(esp_gmf_pipeline_view_t *view, const char *name);

/**
 * @brief  Get the current handle of a named pipeline
 *
 * @param[in]   view      Pipeline view
 * @param[in]   name      Pipeline name
 * @param[out]  pipeline  Current pipeline handle, which can be NULL
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_NOT_FOUND    Pipeline name does not exist
 */
esp_gmf_err_t esp_gmf_pipeline_view_get_pipeline(esp_gmf_pipeline_view_t *view, const char *name,
                                                 esp_gmf_pipeline_handle_t *pipeline);

/**
 * @brief  Record a logical predecessor/successor relation between two named pipelines
 *
 * @note  This does not connect the pipelines on the data path. Use
 *         `esp_gmf_pipeline_connect_pipe` to bind ports and transfer data.
 *
 * @param[in]  view  Pipeline view
 * @param[in]  from  Predecessor pipeline name
 * @param[in]  to    Successor pipeline name
 *
 * @return
 *       - ESP_GMF_ERR_OK              On success
 *       - ESP_GMF_ERR_INVALID_ARG     Invalid argument
 *       - ESP_GMF_ERR_NOT_FOUND       Source or destination does not exist
 *       - ESP_GMF_ERR_ALREADY_EXISTS  Relation already exists
 *       - ESP_GMF_ERR_MEMORY_LACK     Not enough memory
 */
esp_gmf_err_t esp_gmf_pipeline_view_add_connection(esp_gmf_pipeline_view_t *view, const char *from, const char *to);

/**
 * @brief  Remove a logical predecessor/successor relation between two named pipelines
 *
 * @param[in]  view  Pipeline view
 * @param[in]  from  Predecessor pipeline name
 * @param[in]  to    Successor pipeline name
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_NOT_FOUND    Relation does not exist
 */
esp_gmf_err_t esp_gmf_pipeline_view_remove_connection(esp_gmf_pipeline_view_t *view, const char *from, const char *to);

/**
 * @brief  Iterate named pipelines in a view
 *
 * @param[in]      view      Pipeline view
 * @param[in,out]  iterator  Iterator initialized to NULL before the first call
 * @param[out]     item      Current read-only pipeline item
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_NOT_FOUND    End of the list
 */
esp_gmf_err_t esp_gmf_pipeline_view_iterate_pipeline(const esp_gmf_pipeline_view_t *view,
                                                     const void **iterator,
                                                     const esp_gmf_pipeline_view_item_t **item);

/**
 * @brief  Iterate statically registered pipeline view connectors
 *
 * @param[in,out]  iterator   Iterator initialized to NULL before the first call
 * @param[out]     connector  Current read-only connector
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_NOT_FOUND    End of the linker-provided connector array
 */
esp_gmf_err_t esp_gmf_pipeline_view_iterate_connector(const void **iterator,
                                                      const esp_gmf_pipeline_view_connector_t **connector);

/**
 * @brief  Iterate logical predecessor/successor relations in a view
 *
 * @param[in]      view        Pipeline view
 * @param[in,out]  iterator    Iterator initialized to NULL before the first call
 * @param[out]     connection  Current read-only relation
 *
 * @return
 *       - ESP_GMF_ERR_OK           On success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid argument
 *       - ESP_GMF_ERR_NOT_FOUND    End of the list
 */
esp_gmf_err_t esp_gmf_pipeline_view_iterate_connection(const esp_gmf_pipeline_view_t *view, const void **iterator,
                                                       const esp_gmf_pipeline_view_connection_t **connection);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
