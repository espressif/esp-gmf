/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "periph_uart.h"

static const char *TAG = "PERIPH_UART";

int periph_uart_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size != sizeof(periph_uart_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    // Create handle with SPI port info
    periph_uart_handle_t *handle = (periph_uart_handle_t *)calloc(1, sizeof(periph_uart_handle_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return -1;
    }
    periph_uart_config_t *uart_cfg = (periph_uart_config_t *)cfg;

    esp_err_t ret = ESP_FAIL;
    if (uart_cfg->use_queue) {
        ret = uart_driver_install(uart_cfg->uart_num, uart_cfg->rx_buffer_size, uart_cfg->tx_buffer_size, uart_cfg->queue_size, &handle->uart_queue, uart_cfg->intr_alloc_flags);
    } else {
        ret = uart_driver_install(uart_cfg->uart_num, uart_cfg->rx_buffer_size, uart_cfg->tx_buffer_size, 0, NULL, uart_cfg->intr_alloc_flags);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install uart driver: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ret = uart_param_config(uart_cfg->uart_num, &uart_cfg->uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure uart parameters: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ret = uart_set_pin(uart_cfg->uart_num, uart_cfg->tx_io_num, uart_cfg->rx_io_num, uart_cfg->rts_io_num, uart_cfg->cts_io_num);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set uart pins: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    if (uart_cfg->uart_mode != UART_MODE_UART) {
        ret = uart_set_mode(uart_cfg->uart_num, uart_cfg->uart_mode);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set uart mode: %s", esp_err_to_name(ret));
            goto cleanup;
        }
    }

    handle->uart_num = uart_cfg->uart_num;
    *periph_handle = handle;

    ESP_LOGI(TAG, "UART %d initialized successfully", uart_cfg->uart_num);
    return 0;

cleanup:
    free(handle);
    return -1;
}

int periph_uart_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }

    periph_uart_handle_t *handle = (periph_uart_handle_t *)periph_handle;
    ESP_LOGI(TAG, "Deinitializing UART %d", handle->uart_num);
    esp_err_t ret = uart_driver_delete(handle->uart_num);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete uart driver: %s", esp_err_to_name(ret));
        return -1;
    }
    free(periph_handle);
    return 0;
}
