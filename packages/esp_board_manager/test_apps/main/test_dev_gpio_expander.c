/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_board_device.h"
#include "dev_gpio_expander.h"
#include "freertos/FreeRTOS.h"
#include "esp_io_expander.h"

#define GPIO_EXPANDER_TEST IO_EXPANDER_PIN_NUM_1

static const char *TAG = "TEST_GPIO_EXPANDER";

void test_dev_gpio_expander(void)
{
    void *dev_cfg = NULL;
    esp_err_t ret = esp_board_device_get_config("gpio_expander", &dev_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get gpio_expander device config: %s", esp_err_to_name(ret));
        return;
    }

    void *dev_handle = NULL;
    ret = esp_board_device_get_handle("gpio_expander", &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get gpio_expander device handle: %s", esp_err_to_name(ret));
        return;
    }

    dev_io_expander_config_t *gpio_config = (dev_io_expander_config_t *)dev_cfg;
    esp_io_expander_handle_t *gpio_expander = (esp_io_expander_handle_t *)dev_handle;

    ESP_LOGI(TAG, "GPIO Expander Device Config:");
    ESP_LOGI(TAG, "  Name: %s", gpio_config->name);
    esp_io_expander_print_state(*gpio_expander);
    ESP_LOGI(TAG, " GPIO Expander Device test completed successfully!");

    return;
}
