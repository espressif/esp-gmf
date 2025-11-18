/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/**
 * @brief  Periph ANACMPR test
 *
 *         This test example can be used to test the anacmpr peripheral that has been initialized through the board manager.
 *         Use 50% of the internal voltage as the reference source,
 *         and configure a GPIO to monitor the positive and negative crosses on the analog comparator unit.
 *
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_board_periph.h"
#include "esp_board_manager_defs.h"
#include "periph_anacmpr.h"
#include "periph_gpio.h"

static const char *TAG          = "TEST_ANACMPR";
static gpio_num_t  gpio_monitor = -1;  // GPIO used to monitor cross events

static bool test_anacmpr_on_cross_callback(ana_cmpr_handle_t cmpr, const ana_cmpr_cross_event_data_t *edata, void *user_ctx)
{
    static int lvl = 0;
    // Toggle the GPIO to monitor the cross event
    if (gpio_monitor != -1) {
        gpio_set_level(gpio_monitor, lvl);
        lvl = !lvl;
    }
    return false;
}

static void test_anacmpr_with_internal_reference(periph_anacmpr_handle_t *anacmpr_handle)
{
    ESP_LOGI(TAG, "Testing Analog Comparator with internal 50%% VDD reference...");

    // Configure internal reference to 50% VDD
    ana_cmpr_internal_ref_config_t ref_cfg = {
        .ref_volt = ANA_CMPR_REF_VOLT_50_PCT_VDD,
    };
    esp_err_t ret = ana_cmpr_set_internal_reference(anacmpr_handle->cmpr_handle, &ref_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set internal reference: %s", esp_err_to_name(ret));
        return;
    }

    // Set debounce configuration to prevent noise
    ana_cmpr_debounce_config_t dbc_cfg = {
        .wait_us = 100,  // 1ms debounce time
    };
    ret = ana_cmpr_set_debounce(anacmpr_handle->cmpr_handle, &dbc_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set debounce: %s", esp_err_to_name(ret));
        return;
    }

    // Register cross event callback
    ana_cmpr_event_callbacks_t cbs = {
        .on_cross = test_anacmpr_on_cross_callback,
    };
    ret = ana_cmpr_register_event_callbacks(anacmpr_handle->cmpr_handle, &cbs, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register callbacks: %s", esp_err_to_name(ret));
        return;
    }

    // Enable the analog comparator
    ret = ana_cmpr_enable(anacmpr_handle->cmpr_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable analog comparator: %s", esp_err_to_name(ret));
        return;
    }

    int src_gpio = -1;
    ret = ana_cmpr_get_gpio(anacmpr_handle->unit, ANA_CMPR_SOURCE_CHAN, &src_gpio);
    ESP_LOGI(TAG, "Analog Comparator initialized successfully");
    ESP_LOGI(TAG, "  - Source GPIO: %d", src_gpio);
    ESP_LOGI(TAG, "  - Reference: Internal 50%% VDD");
    if (gpio_monitor != -1) {
        ESP_LOGI(TAG, "  - Monitor GPIO: %d (toggles on cross events)", gpio_monitor);
    }

    // Run test for 5 seconds
    ESP_LOGI(TAG, "Running test for 5 seconds...");
    for (int i = 0; i < 5; i++) {
        ESP_LOGI(TAG, "Test running... %d/5 seconds", i + 1);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Disable the analog comparator
    ret = ana_cmpr_disable(anacmpr_handle->cmpr_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable analog comparator: %s", esp_err_to_name(ret));
    }
}

void test_periph_anacmpr(void)
{
    ESP_LOGI(TAG, "=== Starting Analog Comparator Peripheral Test ===");

    periph_anacmpr_handle_t *anacmpr_handle = NULL;
    // Get Analog Comparator handle
    esp_err_t ret = esp_board_periph_get_handle(ESP_BOARD_PERIPH_NAME_ANACMPR, (void **)&anacmpr_handle);
    if (ret != ESP_OK || !anacmpr_handle) {
        ESP_LOGE(TAG, "Failed to get Analog Comparator peripheral handle");
        return;
    }

    periph_gpio_handle_t *gpio_handle = NULL;
    ret = esp_board_periph_get_handle(ESP_BOARD_PERIPH_NAME_GPIO_MONITOR, (void **)&gpio_handle);
    if (ret == ESP_OK && gpio_handle) {
        gpio_monitor = gpio_handle->gpio_num;
    } else {
        ESP_LOGW(TAG, "Failed to get GPIO monitor peripheral handle");
    }

    ESP_LOGI(TAG, "=== Testing Analog Comparator with Internal Reference ===");

    // Test with internal reference
    test_anacmpr_with_internal_reference(anacmpr_handle);

    ESP_LOGI(TAG, "=== Analog Comparator Peripheral Test Completed ===");
}
