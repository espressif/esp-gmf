/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * Test API C++ compilation only, not as a example reference
 */


 #include <stdio.h>
 #include "esp_log.h"
 #include "esp_err.h"
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "esp_board_manager.h"

static const char *TAG = "CXX_BUILD_TEST";

extern "C" void cxx_build_test(void)
{
    ESP_LOGI(TAG, "Starting ESP Board Manager Test Application");
    esp_board_manager_print_board_info();

    // Initialize board manager
    ESP_LOGI(TAG, "Initializing board manager...");
    int ret = esp_board_manager_init();
    if (ret != 0) {
        ESP_LOGE(TAG, "Board manager initialization failed: %d", ret);
        return;
    }
    ESP_LOGI(TAG, "Board manager initialized successfully");
    esp_board_manager_print();

    ESP_LOGI(TAG, "Starting cleanup...");
    ret = esp_board_manager_deinit();
    if (ret != 0) {
        ESP_LOGE(TAG, "Board manager deinitialization failed: %d", ret);
    } else {
        ESP_LOGI(TAG, "Board manager deinitialized successfully");
    }
    ESP_LOGI(TAG, "Test completed");
}
