/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "unity.h"
#include "esp_gmf_pipeline_view.h"

static esp_gmf_pipeline_view_t *s_registered_view;
static esp_gmf_pipeline_view_t *s_registered_view_alt;

static esp_gmf_err_t test_get_view(const esp_gmf_pipeline_view_t **view)
{
    if (view == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (s_registered_view == NULL) {
        *view = NULL;
        return ESP_GMF_ERR_NOT_FOUND;
    }
    *view = s_registered_view;
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t test_get_view_alt(const esp_gmf_pipeline_view_t **view)
{
    if (view == NULL) {
        return ESP_GMF_ERR_INVALID_ARG;
    }
    if (s_registered_view_alt == NULL) {
        *view = NULL;
        return ESP_GMF_ERR_NOT_FOUND;
    }
    *view = s_registered_view_alt;
    return ESP_GMF_ERR_OK;
}

ESP_GMF_PIPELINE_VIEW_CONNECTOR_REGISTER(s_test_connector, test_get_view);
ESP_GMF_PIPELINE_VIEW_CONNECTOR_REGISTER(s_test_connector_alt, test_get_view_alt);

TEST_CASE("GMF Pipeline View operations", "[ESP_GMF_PIPELINE_VIEW]")
{
    esp_gmf_pipeline_view_t *view = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_init(&view));
    TEST_ASSERT_NOT_NULL(view);

    esp_gmf_pipeline_handle_t stream = (esp_gmf_pipeline_handle_t)0x1;
    esp_gmf_pipeline_handle_t mixed = (esp_gmf_pipeline_handle_t)0x2;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_add_pipeline(view, "stream-0", stream));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_add_pipeline(view, "mixed", mixed));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_ALREADY_EXISTS,
                      esp_gmf_pipeline_view_add_pipeline(view, "mixed", NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_add_connection(view, "stream-0", "mixed"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_ALREADY_EXISTS,
                      esp_gmf_pipeline_view_add_connection(view, "stream-0", "mixed"));

    const void *iterator = NULL;
    const esp_gmf_pipeline_view_item_t *item = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_iterate_pipeline(view, &iterator, &item));
    TEST_ASSERT_EQUAL_STRING("stream-0", item->name);
    TEST_ASSERT_EQUAL_PTR(stream, item->pipeline);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_iterate_pipeline(view, &iterator, &item));
    TEST_ASSERT_EQUAL_STRING("mixed", item->name);
    TEST_ASSERT_EQUAL_PTR(mixed, item->pipeline);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND,
                      esp_gmf_pipeline_view_iterate_pipeline(view, &iterator, &item));

    iterator = NULL;
    const esp_gmf_pipeline_view_connection_t *connection = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_iterate_connection(view, &iterator, &connection));
    TEST_ASSERT_EQUAL_STRING("stream-0", connection->from->name);
    TEST_ASSERT_EQUAL_STRING("mixed", connection->to->name);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND,
                      esp_gmf_pipeline_view_iterate_connection(view, &iterator, &connection));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_set_pipeline(view, "stream-0", NULL));
    stream = mixed;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_get_pipeline(view, "stream-0", &stream));
    TEST_ASSERT_NULL(stream);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_remove_pipeline(view, "mixed"));
    iterator = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND,
                      esp_gmf_pipeline_view_iterate_connection(view, &iterator, &connection));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_deinit(view));
}

TEST_CASE("GMF Pipeline View static connector", "[ESP_GMF_PIPELINE_VIEW]")
{
    const esp_gmf_pipeline_view_t *view = (const esp_gmf_pipeline_view_t *)0x1;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND, test_get_view(&view));
    TEST_ASSERT_NULL(view);
    view = (const esp_gmf_pipeline_view_t *)0x1;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND, test_get_view_alt(&view));
    TEST_ASSERT_NULL(view);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_init(&s_registered_view));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_add_pipeline(s_registered_view, "test", NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_init(&s_registered_view_alt));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_add_pipeline(s_registered_view_alt, "alternate", NULL));

    const void *iterator = NULL;
    const esp_gmf_pipeline_view_connector_t *connector = NULL;
    bool found = false;
    bool found_alt = false;
    while (esp_gmf_pipeline_view_iterate_connector(&iterator, &connector) == ESP_GMF_ERR_OK) {
        if (connector->get_view == test_get_view) {
            view = NULL;
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, connector->get_view(&view));
            TEST_ASSERT_EQUAL_PTR(s_registered_view, view);
            found = true;
        } else if (connector->get_view == test_get_view_alt) {
            view = NULL;
            TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, connector->get_view(&view));
            TEST_ASSERT_EQUAL_PTR(s_registered_view_alt, view);
            found_alt = true;
        }
    }
    TEST_ASSERT_TRUE(found);
    TEST_ASSERT_TRUE(found_alt);
    TEST_ASSERT_NULL(connector);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_deinit(s_registered_view));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_deinit(s_registered_view_alt));
    s_registered_view = NULL;
    s_registered_view_alt = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND, test_get_view(&view));
    TEST_ASSERT_NULL(view);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND, test_get_view_alt(&view));
    TEST_ASSERT_NULL(view);
}

TEST_CASE("GMF Pipeline View errors and cascading removal", "[ESP_GMF_PIPELINE_VIEW]")
{
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_pipeline_view_init(NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_pipeline_view_deinit(NULL));

    esp_gmf_pipeline_view_t *view = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_init(&view));
    const void *iterator = NULL;
    const esp_gmf_pipeline_view_item_t *item = (const esp_gmf_pipeline_view_item_t *)0x1;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND,
                      esp_gmf_pipeline_view_iterate_pipeline(view, &iterator, &item));
    TEST_ASSERT_NULL(item);
    const esp_gmf_pipeline_view_connection_t *connection =
        (const esp_gmf_pipeline_view_connection_t *)0x1;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND,
                      esp_gmf_pipeline_view_iterate_connection(view, &iterator, &connection));
    TEST_ASSERT_NULL(connection);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_add_pipeline(view, "", NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_add_pipeline(NULL, "a", NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_add_pipeline(view, "a", (esp_gmf_pipeline_handle_t)0x1));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_add_pipeline(view, "b", (esp_gmf_pipeline_handle_t)0x2));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_add_pipeline(view, "c", (esp_gmf_pipeline_handle_t)0x3));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND,
                      esp_gmf_pipeline_view_set_pipeline(view, "missing", NULL));
    esp_gmf_pipeline_handle_t pipeline = (esp_gmf_pipeline_handle_t)0x4;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND,
                      esp_gmf_pipeline_view_get_pipeline(view, "missing", &pipeline));
    TEST_ASSERT_NULL(pipeline);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND,
                      esp_gmf_pipeline_view_add_connection(view, "a", "missing"));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_add_connection(view, "a", "b"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_add_connection(view, "c", "b"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_add_connection(view, "a", "c"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_remove_connection(view, "a", "c"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND,
                      esp_gmf_pipeline_view_remove_connection(view, "a", "c"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_add_connection(view, "a", "c"));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_remove_pipeline(view, "b"));
    iterator = NULL;
    connection = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_iterate_connection(view, &iterator, &connection));
    TEST_ASSERT_EQUAL_STRING("a", connection->from->name);
    TEST_ASSERT_EQUAL_STRING("c", connection->to->name);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND,
                      esp_gmf_pipeline_view_iterate_connection(view, &iterator, &connection));
    TEST_ASSERT_NULL(connection);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND, esp_gmf_pipeline_view_remove_pipeline(view, "b"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_deinit(view));
}

TEST_CASE("GMF Pipeline View invalid args and handle update", "[ESP_GMF_PIPELINE_VIEW]")
{
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_add_pipeline(NULL, "a", NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_iterate_connector(NULL, NULL));

    esp_gmf_pipeline_view_t *view = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_init(&view));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_add_pipeline(view, NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_set_pipeline(NULL, "a", NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_set_pipeline(view, "", NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_remove_pipeline(view, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_get_pipeline(view, "a", NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_add_connection(view, "", "a"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_remove_connection(view, "a", NULL));

    const void *iterator = NULL;
    const esp_gmf_pipeline_view_item_t *item = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_iterate_pipeline(NULL, &iterator, &item));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_iterate_pipeline(view, NULL, &item));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_iterate_pipeline(view, &iterator, NULL));
    const esp_gmf_pipeline_view_connection_t *connection = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_iterate_connection(NULL, &iterator, &connection));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_iterate_connection(view, NULL, &connection));
    const esp_gmf_pipeline_view_connector_t *connector = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_iterate_connector(&iterator, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG,
                      esp_gmf_pipeline_view_iterate_connector(NULL, &connector));

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_add_pipeline(view, "loop", (esp_gmf_pipeline_handle_t)0x10));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_add_connection(view, "loop", "loop"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_ALREADY_EXISTS,
                      esp_gmf_pipeline_view_add_connection(view, "loop", "loop"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_set_pipeline(view, "loop", (esp_gmf_pipeline_handle_t)0x20));
    esp_gmf_pipeline_handle_t pipeline = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_get_pipeline(view, "loop", &pipeline));
    TEST_ASSERT_EQUAL_PTR((esp_gmf_pipeline_handle_t)0x20, pipeline);

    iterator = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_iterate_connection(view, &iterator, &connection));
    TEST_ASSERT_EQUAL_STRING("loop", connection->from->name);
    TEST_ASSERT_EQUAL_STRING("loop", connection->to->name);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                      esp_gmf_pipeline_view_remove_connection(view, "loop", "loop"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_NOT_FOUND,
                      esp_gmf_pipeline_view_remove_connection(view, "loop", "loop"));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_view_deinit(view));
}
