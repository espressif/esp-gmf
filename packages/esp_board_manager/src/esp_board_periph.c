/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_board_periph.h"
#include "esp_board_manager_err.h"
#include "esp_board_find_utils.h"

static const char *TAG = "BOARD_PERIPH";

extern const esp_board_periph_desc_t g_esp_board_peripherals[];
extern esp_board_periph_entry_t g_esp_board_periph_handles[];

/* Peripheral handle list */
static esp_board_periph_list_t *periph_list = NULL;

/* Defined in board_periph_handles.c */
extern esp_board_periph_entry_t g_esp_board_periph_handles[];

/* Find peripheral list entry by name */
static esp_board_periph_list_t *find_periph_list(const char *name)
{
    esp_board_periph_list_t *list = periph_list;
    while (list) {
        if (strcmp(list->name, name) == 0) {
            return list;
        }
        list = list->next;
    }
    return NULL;
}

/* Add new peripheral list entry */
static esp_board_periph_list_t *add_periph_list(const char *name, const char *type, const char *role)
{
    esp_board_periph_list_t *list = calloc(1, sizeof(esp_board_periph_list_t));
    if (!list) {
        return NULL;
    }

    list->name = strdup(name);
    list->type = strdup(type);
    list->role = strdup(role);
    list->ref_count = 0;
    list->periph_handle = NULL;
    list->next = periph_list;
    periph_list = list;

    return list;
}

esp_err_t esp_board_periph_init(const char *name)
{
    ESP_BOARD_RETURN_ON_FALSE(name, ESP_BOARD_ERR_PERIPH_INVALID_ARG, TAG, "name is null");

    /* Find peripheral descriptor */
    const esp_board_periph_desc_t *desc = esp_board_find_periph_desc(name);
    ESP_BOARD_RETURN_ON_PERIPH_NOT_FOUND(desc, name, TAG, "Peripheral %s not found", name);

    /* Find handle */
    esp_board_periph_entry_t *handle = esp_board_find_periph_handle(desc->type, desc->role);
    ESP_BOARD_RETURN_ON_FALSE(handle, ESP_BOARD_ERR_PERIPH_NO_HANDLE, TAG,
                          "No handle found for %s (type=%s, role=%s)", name, desc->type, desc->role);

    /* Find or create list entry */
    esp_board_periph_list_t *list = find_periph_list(name);
    if (!list) {
        list = add_periph_list(name, desc->type, desc->role);
        ESP_BOARD_RETURN_ON_FALSE(list, ESP_ERR_NO_MEM, TAG, "Failed to create list entry for %s", name);
    }

    /* If already initialized, increase reference count */
    if (list->ref_count > 0) {
        list->ref_count++;
        ESP_LOGI(TAG, "Reuse periph: %s, ref_count=%d", name, list->ref_count);
        return ESP_OK;
    }

    /* First time initialization */
    ESP_BOARD_RETURN_ON_FALSE(handle->init, ESP_BOARD_ERR_PERIPH_NO_INIT, TAG, "No init function for periph: %s", name);
    esp_err_t ret = handle->init((void *)desc->cfg, desc->cfg_size, &list->periph_handle);
    ESP_BOARD_RETURN_ON_ERROR(ret, TAG, "Failed to init periph: %s", name);
    list->ref_count = 1;
    ESP_LOGD(TAG, "Initialized periph: %s, handle: %p", name, list->periph_handle);
    return ESP_OK;
}

esp_err_t esp_board_periph_get_handle(const char *name, void **periph_handle)
{
    ESP_BOARD_RETURN_ON_FALSE(name && periph_handle, ESP_BOARD_ERR_PERIPH_INVALID_ARG, TAG, "Invalid args");

    /* Find list entry */
    esp_board_periph_list_t *list = find_periph_list(name);
    ESP_BOARD_RETURN_ON_FALSE(list && list->periph_handle, ESP_BOARD_ERR_PERIPH_NO_HANDLE, TAG,
                          "No handle found for %s on get handle", name);
    *periph_handle = list->periph_handle;
    return ESP_OK;
}

esp_err_t esp_board_periph_get_name_by_handle(void *periph_handle, const char **name)
{
    ESP_BOARD_RETURN_ON_FALSE(periph_handle && name, ESP_BOARD_ERR_PERIPH_INVALID_ARG, TAG, "Invalid args");
    /* Search through all peripheral list entries */
    esp_board_periph_list_t *list = periph_list;
    while (list) {
        if (list->periph_handle == periph_handle) {
            *name = list->name;
            ESP_LOGD(TAG, "Found peripheral name: %s for handle: %p", list->name, periph_handle);
            return ESP_OK;
        }
        list = list->next;
    }
    ESP_LOGE(TAG, "Not found name for peripheral with handle: %p", periph_handle);
    return ESP_BOARD_ERR_PERIPH_NOT_FOUND;
}

esp_err_t esp_board_periph_ref_handle(const char *name, void **periph_handle)
{
    ESP_BOARD_RETURN_ON_FALSE(name && periph_handle, ESP_BOARD_ERR_PERIPH_INVALID_ARG, TAG, "Invalid args");

    esp_err_t ret = esp_board_periph_init(name);
    ESP_BOARD_RETURN_ON_ERROR(ret, TAG, "Failed to init periph: %s on ref handle", name);

    esp_board_periph_list_t *list = find_periph_list(name);
    ESP_BOARD_RETURN_ON_FALSE(list, ESP_BOARD_ERR_PERIPH_NO_HANDLE, TAG, "No list entry found for %s", name);

    if (list->periph_handle == NULL) {
        ESP_LOGE(TAG, "Peripheral %s handle is NULL", name);
        return ESP_BOARD_ERR_PERIPH_NO_HANDLE;
    }
    *periph_handle = list->periph_handle;
    return ESP_OK;
}

esp_err_t esp_board_periph_unref_handle(const char *name)
{
    ESP_BOARD_RETURN_ON_FALSE(name, ESP_BOARD_ERR_PERIPH_INVALID_ARG, TAG, "The name is NULL");
    return esp_board_periph_deinit(name);
}

esp_err_t esp_board_periph_get_config(const char *name, void **config)
{
    if (name == NULL || config == NULL) {
        ESP_LOGE(TAG, "Invalid arguments: name or config is NULL");
        return ESP_BOARD_ERR_PERIPH_INVALID_ARG;
    }
    const esp_board_periph_desc_t *desc = esp_board_find_periph_desc(name);
    if (desc == NULL) {
        ESP_LOGE(TAG, "Peripheral descriptor not found for %s", name);
        return ESP_BOARD_ERR_PERIPH_NOT_FOUND;
    }
    if (desc->cfg == NULL) {
        ESP_LOGW(TAG, "Peripheral %s has no configuration", name);
        *config = NULL;
        return ESP_BOARD_ERR_PERIPH_NOT_SUPPORTED;
    }
    *config = (void *)desc->cfg;
    ESP_LOGI(TAG, "Peripheral %s config found: %p (size: %d)", name, desc->cfg, desc->cfg_size);
    return ESP_OK;
}

esp_err_t esp_board_periph_init_custom(const char *name, esp_board_periph_init_func init, esp_board_periph_deinit_func deinit)
{
    ESP_BOARD_RETURN_ON_FALSE(name && init && deinit, ESP_BOARD_ERR_PERIPH_INVALID_ARG, TAG, "Invalid args");

    /* Find peripheral descriptor */
    const esp_board_periph_desc_t *desc = esp_board_find_periph_desc(name);
    ESP_BOARD_RETURN_ON_PERIPH_NOT_FOUND(desc, name, TAG, "Peripheral %s not found", name);

    /* Find handle */
    esp_board_periph_entry_t *handle = esp_board_find_periph_handle(desc->type, desc->role);
    ESP_BOARD_RETURN_ON_FALSE(handle, ESP_BOARD_ERR_PERIPH_NO_HANDLE, TAG,
                          "No handle found for %s (type=%s, role=%s)", name, desc->type, desc->role);

    /* Find or create list entry */
    esp_board_periph_list_t *list = find_periph_list(name);
    if (!list) {
        list = add_periph_list(name, desc->type, desc->role);
        ESP_BOARD_RETURN_ON_FALSE(list, ESP_ERR_NO_MEM, TAG, "Failed to create list entry for %s", name);
    }

    /* Set functions */
    handle->init = init;
    handle->deinit = deinit;

    /* Initialize the peripheral if not already initialized */
    if (list->ref_count == 0) {
        esp_err_t ret = handle->init((void *)desc->cfg, desc->cfg_size, &list->periph_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to init periph: %s", name);
            /* Reset functions on failure */
            handle->init = NULL;
            handle->deinit = NULL;
            return ESP_BOARD_ERR_PERIPH_INIT_FAILED;
        }
        list->ref_count = 1;
        ESP_LOGI(TAG, "Custom initialized periph: %s, handle: %p", name, list->periph_handle);
    }

    return ESP_OK;
}

esp_err_t esp_board_periph_deinit(const char *name)
{
    ESP_BOARD_RETURN_ON_FALSE(name, ESP_BOARD_ERR_PERIPH_INVALID_ARG, TAG, "name is null");

    /* Find peripheral descriptor */
    const esp_board_periph_desc_t *desc = esp_board_find_periph_desc(name);
    ESP_BOARD_RETURN_ON_PERIPH_NOT_FOUND(desc, name, TAG, "Peripheral %s not found", name);

    /* Find handle */
    esp_board_periph_entry_t *handle = esp_board_find_periph_handle(desc->type, desc->role);
    ESP_BOARD_RETURN_ON_FALSE(handle, ESP_BOARD_ERR_PERIPH_NO_HANDLE, TAG,
                          "No handle found for %s (type=%s, role=%s)", name, desc->type, desc->role);

    /* Find list entry */
    esp_board_periph_list_t *list = find_periph_list(name);
    ESP_BOARD_RETURN_ON_FALSE(list, ESP_BOARD_ERR_PERIPH_NO_HANDLE, TAG, "No list entry found for %s", name);

    /* Check if peripheral is initialized */
    if (!list->periph_handle) {
        ESP_LOGW(TAG, "Peripheral %s not initialized", name);
        return ESP_OK;
    }

    /* Decrement reference count */
    list->ref_count--;
    ESP_LOGI(TAG, "Deinit peripheral %s ref_count: %d", name, list->ref_count);

    /* Only deinitialize if ref_count reaches 0 */
    if (list->ref_count == 0) {
        /* Check if deinit function exists */
        ESP_BOARD_RETURN_ON_FALSE(handle->deinit, ESP_BOARD_ERR_PERIPH_NO_INIT, TAG,
                              "No deinit function for periph: %s", name);

        /* Deinitialize peripheral */
        esp_err_t ret = handle->deinit(list->periph_handle);
        ESP_BOARD_RETURN_ON_ERROR(ret, TAG, "Failed to deinit periph: %s", name);

        list->periph_handle = NULL;
    } else {
        ESP_LOGW(TAG, "Peripheral %s still has %d references, not deinitializing", name, list->ref_count);
    }

    return ESP_OK;
}

esp_err_t esp_board_periph_show(const char *name)
{
    if (name) {
        /* Show information for specific peripheral */
        const esp_board_periph_desc_t *desc = esp_board_find_periph_desc(name);
        ESP_BOARD_RETURN_ON_PERIPH_NOT_FOUND(desc, name, TAG, "Peripheral %s not found", name);

        esp_board_periph_list_t *list = find_periph_list(name);
        ESP_LOGI(TAG, "Peripheral %s:", name);
        ESP_LOGI(TAG, "  Type: %s", desc->type);
        ESP_LOGI(TAG, "  Role: %s", desc->role);
        ESP_LOGI(TAG, "  Format: %s", desc->format ? desc->format : "N/A");
        ESP_LOGI(TAG, "  Config size: %d", desc->cfg_size);
        ESP_LOGI(TAG, "  ID: %d", desc->id);
        if (list) {
            ESP_LOGI(TAG, "  Handle: %p", list->periph_handle);
            ESP_LOGI(TAG, "  Status: %s", list->periph_handle ? "Initialized" : "Not initialized");
            ESP_LOGI(TAG, "  Ref count: %d", list->ref_count);
        } else {
            ESP_LOGI(TAG, "  No list entry found");
        }
    } else {
        /* Show information for all peripherals */
        const esp_board_periph_desc_t *desc = g_esp_board_peripherals;
        while (desc) {
            esp_board_periph_list_t *list = find_periph_list(desc->name);
            ESP_LOGI(TAG, "Peripheral %s:", desc->name);
            ESP_LOGI(TAG, "  Type: %s", desc->type);
            ESP_LOGI(TAG, "  Role: %s", desc->role);
            ESP_LOGI(TAG, "  Format: %s", desc->format ? desc->format : "N/A");
            ESP_LOGI(TAG, "  Config size: %d", desc->cfg_size);
            ESP_LOGI(TAG, "  ID: %d", desc->id);
            if (list) {
                ESP_LOGI(TAG, "  Handle: %p", list->periph_handle);
                ESP_LOGI(TAG, "  Status: %s", list->periph_handle ? "Initialized" : "Not initialized");
                ESP_LOGI(TAG, "  Ref count: %d", list->ref_count);
            } else {
                ESP_LOGI(TAG, "  No list entry found");
            }
            desc = desc->next;
        }
    }
    return ESP_OK;
}

esp_err_t esp_board_periph_init_all(void)
{
    const esp_board_periph_desc_t *desc = g_esp_board_peripherals;
    esp_err_t ret;

    /* Initialize all peripherals in the list */
    while (desc && desc->name) {
        ret = esp_board_periph_init(desc->name);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to initialize peripheral: %s", desc->name);
        }
        desc = desc->next;
    }
    return ESP_OK;
}

esp_err_t esp_board_periph_deinit_all(void)
{
    const esp_board_periph_desc_t *desc = g_esp_board_peripherals;
    esp_err_t ret;

    /* Deinitialize all peripherals in the list */
    while (desc && desc->name) {
        ret = esp_board_periph_deinit(desc->name);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deinitialize peripheral: %s", desc->name);
        }
        desc = desc->next;
    }
    return ESP_OK;
}
