/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_pipeline_view.h"

static esp_gmf_pipeline_view_item_t *find_pipeline(esp_gmf_pipeline_view_t *view, const char *name)
{
    esp_gmf_pipeline_view_item_t *item = view->pipelines;
    while (item != NULL) {
        if (strcmp(item->name, name) == 0) {
            return item;
        }
        item = (esp_gmf_pipeline_view_item_t *)item->node.next;
    }
    return NULL;
}

static void append_node(esp_gmf_node_t **root, esp_gmf_node_t *node)
{
    if (*root == NULL) {
        *root = node;
    } else {
        esp_gmf_node_add_last(*root, node);
    }
}

esp_gmf_err_t esp_gmf_pipeline_view_init(esp_gmf_pipeline_view_t **view)
{
    if (view == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_pipeline_view_t *new_view = esp_gmf_oal_calloc(1, sizeof(*new_view));
    if (new_view == NULL) {
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    *view = new_view;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pipeline_view_deinit(esp_gmf_pipeline_view_t *view)
{
    if (view == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_pipeline_view_connection_t *connection = view->connections;
    while (connection != NULL) {
        esp_gmf_pipeline_view_connection_t *next =
            (esp_gmf_pipeline_view_connection_t *)connection->node.next;
        esp_gmf_oal_free(connection);
        connection = next;
    }
    esp_gmf_pipeline_view_item_t *item = view->pipelines;
    while (item != NULL) {
        esp_gmf_pipeline_view_item_t *next =
            (esp_gmf_pipeline_view_item_t *)item->node.next;
        esp_gmf_oal_free(item->name);
        esp_gmf_oal_free(item);
        item = next;
    }
    esp_gmf_oal_free(view);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pipeline_view_add_pipeline(esp_gmf_pipeline_view_t *view, const char *name,
                                                 esp_gmf_pipeline_handle_t pipeline)
{
    if (view == NULL || name == NULL || name[0] == '\0') {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (find_pipeline(view, name) != NULL) {
        return ESP_GMF_ERR_ALREADY_EXISTS;
    }
    esp_gmf_pipeline_view_item_t *item = esp_gmf_oal_calloc(1, sizeof(*item));
    if (item == NULL) {
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    item->name = esp_gmf_oal_strdup(name);
    if (item->name == NULL) {
        esp_gmf_oal_free(item);
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    item->pipeline = pipeline;
    append_node((esp_gmf_node_t **)&view->pipelines, &item->node);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pipeline_view_set_pipeline(esp_gmf_pipeline_view_t *view, const char *name,
                                                 esp_gmf_pipeline_handle_t pipeline)
{
    if (view == NULL || name == NULL || name[0] == '\0') {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_pipeline_view_item_t *item = find_pipeline(view, name);
    if (item == NULL) {
        return ESP_GMF_ERR_NOT_FOUND;
    }
    item->pipeline = pipeline;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pipeline_view_remove_pipeline(esp_gmf_pipeline_view_t *view, const char *name)
{
    if (view == NULL || name == NULL || name[0] == '\0') {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_pipeline_view_item_t *item = find_pipeline(view, name);
    if (item == NULL) {
        return ESP_GMF_ERR_NOT_FOUND;
    }
    esp_gmf_pipeline_view_connection_t *connection = view->connections;
    while (connection != NULL) {
        esp_gmf_pipeline_view_connection_t *next =
            (esp_gmf_pipeline_view_connection_t *)connection->node.next;
        if (connection->from == item || connection->to == item) {
            esp_gmf_node_t *root = (esp_gmf_node_t *)view->connections;
            esp_gmf_node_del_at(&root, &connection->node);
            view->connections = (esp_gmf_pipeline_view_connection_t *)root;
            esp_gmf_oal_free(connection);
        }
        connection = next;
    }
    esp_gmf_node_t *root = (esp_gmf_node_t *)view->pipelines;
    esp_gmf_node_del_at(&root, &item->node);
    view->pipelines = (esp_gmf_pipeline_view_item_t *)root;
    esp_gmf_oal_free(item->name);
    esp_gmf_oal_free(item);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pipeline_view_get_pipeline(esp_gmf_pipeline_view_t *view, const char *name,
                                                 esp_gmf_pipeline_handle_t *pipeline)
{
    if (view == NULL || name == NULL || name[0] == '\0' || pipeline == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_pipeline_view_item_t *item = find_pipeline(view, name);
    if (item == NULL) {
        *pipeline = NULL;
        return ESP_GMF_ERR_NOT_FOUND;
    }
    *pipeline = item->pipeline;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pipeline_view_add_connection(esp_gmf_pipeline_view_t *view, const char *from,
                                                   const char *to)
{
    if (view == NULL || from == NULL || from[0] == '\0' || to == NULL || to[0] == '\0') {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_pipeline_view_item_t *from_item = find_pipeline(view, from);
    esp_gmf_pipeline_view_item_t *to_item = find_pipeline(view, to);
    if (from_item == NULL || to_item == NULL) {
        return ESP_GMF_ERR_NOT_FOUND;
    }
    for (esp_gmf_pipeline_view_connection_t *connection = view->connections;
         connection != NULL;
         connection = (esp_gmf_pipeline_view_connection_t *)connection->node.next) {
        if (connection->from == from_item && connection->to == to_item) {
            return ESP_GMF_ERR_ALREADY_EXISTS;
        }
    }
    esp_gmf_pipeline_view_connection_t *connection = esp_gmf_oal_calloc(1, sizeof(*connection));
    if (connection == NULL) {
        return ESP_GMF_ERR_MEMORY_LACK;
    }
    connection->from = from_item;
    connection->to = to_item;
    append_node((esp_gmf_node_t **)&view->connections, &connection->node);
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pipeline_view_remove_connection(esp_gmf_pipeline_view_t *view, const char *from,
                                                      const char *to)
{
    if (view == NULL || from == NULL || from[0] == '\0' || to == NULL || to[0] == '\0') {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    esp_gmf_pipeline_view_item_t *from_item = find_pipeline(view, from);
    esp_gmf_pipeline_view_item_t *to_item = find_pipeline(view, to);
    if (from_item == NULL || to_item == NULL) {
        return ESP_GMF_ERR_NOT_FOUND;
    }
    esp_gmf_pipeline_view_connection_t *connection = view->connections;
    while (connection != NULL) {
        if (connection->from == from_item && connection->to == to_item) {
            esp_gmf_node_t *root = (esp_gmf_node_t *)view->connections;
            esp_gmf_node_del_at(&root, &connection->node);
            view->connections = (esp_gmf_pipeline_view_connection_t *)root;
            esp_gmf_oal_free(connection);
            return ESP_GMF_ERR_OK;
        }
        connection = (esp_gmf_pipeline_view_connection_t *)connection->node.next;
    }
    return ESP_GMF_ERR_NOT_FOUND;
}

esp_gmf_err_t esp_gmf_pipeline_view_iterate_pipeline(const esp_gmf_pipeline_view_t *view,
                                                     const void **iterator,
                                                     const esp_gmf_pipeline_view_item_t **item)
{
    if (view == NULL || iterator == NULL || item == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    const esp_gmf_pipeline_view_item_t *current = *iterator;
    current = current == NULL
                  ? view->pipelines
                  : (const esp_gmf_pipeline_view_item_t *)current->node.next;
    if (current == NULL) {
        *item = NULL;
        return ESP_GMF_ERR_NOT_FOUND;
    }
    *iterator = current;
    *item = current;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pipeline_view_iterate_connector(
    const void **iterator, const esp_gmf_pipeline_view_connector_t **connector)
{
    if (iterator == NULL || connector == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    extern const esp_gmf_pipeline_view_connector_t _esp_gmf_pipeline_view_connectors_start;
    extern const esp_gmf_pipeline_view_connector_t _esp_gmf_pipeline_view_connectors_end;
    const esp_gmf_pipeline_view_connector_t *current = *iterator;
    current = current == NULL ? &_esp_gmf_pipeline_view_connectors_start : current + 1;
    if (current >= &_esp_gmf_pipeline_view_connectors_end) {
        *connector = NULL;
        return ESP_GMF_ERR_NOT_FOUND;
    }
    *iterator = current;
    *connector = current;
    return ESP_GMF_ERR_OK;
}

esp_gmf_err_t esp_gmf_pipeline_view_iterate_connection(
    const esp_gmf_pipeline_view_t *view, const void **iterator,
    const esp_gmf_pipeline_view_connection_t **connection)
{
    if (view == NULL || iterator == NULL || connection == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    const esp_gmf_pipeline_view_connection_t *current = *iterator;
    current = current == NULL
                  ? view->connections
                  : (const esp_gmf_pipeline_view_connection_t *)current->node.next;
    if (current == NULL) {
        *connection = NULL;
        return ESP_GMF_ERR_NOT_FOUND;
    }
    *iterator = current;
    *connection = current;
    return ESP_GMF_ERR_OK;
}
