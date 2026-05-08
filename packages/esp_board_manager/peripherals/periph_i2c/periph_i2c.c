/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "periph_i2c.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "periph_i2c_internal.h"

static const char *TAG = "PERIPH_I2C";

typedef struct i2c_effective_addr_node {
    char                           *i2c_name;
    char                           *device_name;
    uint16_t                        addr;
    struct i2c_effective_addr_node *next;
} i2c_effective_addr_node_t;

static i2c_effective_addr_node_t *s_effective_addr_list;

static bool is_valid_8bit_i2c_addr(uint16_t addr)
{
    return addr > 0 && addr <= 0xfe && (addr & 0x1) == 0;
}

static i2c_effective_addr_node_t *find_effective_addr_node(const char *device_name)
{
    for (i2c_effective_addr_node_t *node = s_effective_addr_list; node; node = node->next) {
        if (strcmp(node->device_name, device_name) == 0) {
            return node;
        }
    }
    return NULL;
}

esp_err_t periph_i2c_set_effective_addr_internal(const char *i2c_name,
                                                 const char *device_name,
                                                 uint16_t addr)
{
    if (!i2c_name || i2c_name[0] == '\0' || !device_name || device_name[0] == '\0' ||
        !is_valid_8bit_i2c_addr(addr)) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_effective_addr_node_t *node = find_effective_addr_node(device_name);
    if (node) {
        char *new_i2c_name = strdup(i2c_name);
        if (!new_i2c_name) {
            return ESP_ERR_NO_MEM;
        }
        free(node->i2c_name);
        node->i2c_name = new_i2c_name;
        node->addr = addr;
        return ESP_OK;
    }

    node = (i2c_effective_addr_node_t *)calloc(1, sizeof(i2c_effective_addr_node_t));
    if (!node) {
        return ESP_ERR_NO_MEM;
    }
    node->i2c_name = strdup(i2c_name);
    node->device_name = strdup(device_name);
    if (!node->i2c_name || !node->device_name) {
        free(node->i2c_name);
        free(node->device_name);
        free(node);
        return ESP_ERR_NO_MEM;
    }
    node->addr = addr;
    node->next = s_effective_addr_list;
    s_effective_addr_list = node;
    return ESP_OK;
}

esp_err_t periph_i2c_get_effective_addr_internal(const char *device_name,
                                                 uint16_t *addr)
{
    if (!device_name || device_name[0] == '\0' || !addr) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_effective_addr_node_t *node = find_effective_addr_node(device_name);
    if (!node) {
        return ESP_ERR_NOT_FOUND;
    }
    *addr = node->addr;
    return ESP_OK;
}

esp_err_t periph_i2c_clear_effective_addr_internal(const char *device_name)
{
    if (!device_name || device_name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_effective_addr_node_t **link = &s_effective_addr_list;
    while (*link) {
        i2c_effective_addr_node_t *node = *link;
        if (strcmp(node->device_name, device_name) == 0) {
            *link = node->next;
            free(node->i2c_name);
            free(node->device_name);
            free(node);
            return ESP_OK;
        }
        link = &node->next;
    }
    return ESP_ERR_NOT_FOUND;
}

int periph_i2c_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size < sizeof(i2c_master_bus_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    i2c_master_bus_handle_t handle = NULL;
    esp_err_t err = i2c_new_master_bus((i2c_master_bus_config_t *)cfg, &handle);
    if (err != ESP_OK) {
        free(handle);
        ESP_LOGE(TAG, "i2c_new_master_bus failed: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "I2C master bus initialized successfully");
    *periph_handle = handle;
    return 0;
}

int periph_i2c_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }
    i2c_master_bus_handle_t handle = (i2c_master_bus_handle_t)periph_handle;
    esp_err_t err = i2c_del_master_bus(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_del_master_bus failed: %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI(TAG, "I2C master bus deinitialized successfully");
    return 0;
}
