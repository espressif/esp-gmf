/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/**
 * @brief  Periph ADC test
 *
 *         This test case can be used to test the uart peripheral that has been initialized through the board manager,
 *         For the tx role, the test case will output the string "ABC" ten times through the io you have configured
 *         For the rx role, the test case will attempt to read data ten times through the io you have configured
 */

#include "esp_log.h"
#include "esp_board_periph.h"
#include "periph_uart.h"

static const char *TAG = "TEST_UART";

static void uart_test_tx(uart_port_t uart_num)
{
    ESP_LOGI(TAG, "Testing uart tx");
    uint8_t data[] = {'A', 'B', 'C', '\0'};
    int i = 10;
    while (i) {
        uart_write_bytes(uart_num, (const char *)data, 3);
        vTaskDelay(500 / portTICK_PERIOD_MS);
        i--;
    }
}

static void uart_test_rx(uart_port_t uart_num)
{
    ESP_LOGI(TAG, "Testing uart rx");
    uint8_t *data = (uint8_t *)malloc(1024);
    int i = 10;
    while (i) {
        int len = uart_read_bytes(uart_num, data, (1024 - 1), 20 / portTICK_PERIOD_MS);
        if (len) {
            data[len] = '\0';
            ESP_LOGI(TAG, "Recv str: %s", (char *)data);
            i--;
        }
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}

void test_periph_uart()
{
    void *uart_handle;
    esp_err_t ret = esp_board_periph_get_handle("uart_1", &uart_handle);
    if (ret != ESP_OK || uart_handle == NULL) {
        ESP_LOGE(TAG, "Failed to get uart handle");
        return;
    }
    ESP_LOGI(TAG, "Get uart_handle: %p", uart_handle);

    uart_test_tx(((periph_uart_handle_t *)uart_handle)->uart_num);
    vTaskDelay(500 / portTICK_PERIOD_MS);
    uart_test_rx(((periph_uart_handle_t *)uart_handle)->uart_num);

    return;
}
