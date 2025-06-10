/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_gmf_app_cli.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_sys.h"

static const char *TAG = "GMF_APP_UTILS_TEST";

/* CLI Module Tests */
TEST_CASE("esp_gmf_app_cli_init_basic", "[gmf_app_utils][cli][leaks=8000]")
{
    ESP_LOGI(TAG, "Testing CLI initialization");

    // Test basic CLI initialization without custom commands
    esp_err_t ret = esp_gmf_app_cli_init("test> ", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    // Test double initialization - should return ESP_OK with warning
    ret = esp_gmf_app_cli_init("test2> ", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);
}

/* System Monitor Tests */
TEST_CASE("esp_gmf_app_sys_monitor_lifecycle", "[gmf_app_utils][sys][leaks=14000]")
{
    ESP_LOGI(TAG, "Testing system monitor lifecycle");

    // Test monitor start
    esp_gmf_app_sys_monitor_start();

    // Let monitor run for a short time
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Test monitor stop
    esp_gmf_app_sys_monitor_stop();

    // Wait for task cleanup
    vTaskDelay(100 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Monitor lifecycle test completed");
}

TEST_CASE("esp_gmf_app_sdcard_lifecycle", "[gmf_app_utils][peripheral]")
{
    ESP_LOGI(TAG, "Testing SD card lifecycle");

    void *sdcard_handle = NULL;

    // Test SD card setup
    esp_gmf_app_setup_sdcard(&sdcard_handle);
    ESP_LOGI(TAG, "SD card handle after setup: %p", sdcard_handle);

    // Test SD card teardown
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    ESP_LOGI(TAG, "SD card teardown completed");
}

/* Integration Tests */
TEST_CASE("full_integration_test", "[gmf_app_utils][integration][leaks=13000]")
{
    ESP_LOGI(TAG, "Running full integration test");

    esp_gmf_app_codec_info_t codec_info = ESP_GMF_APP_CODEC_INFO_DEFAULT();

    // Initialize CLI
    esp_err_t ret = esp_gmf_app_cli_init("integration> ", NULL);
    TEST_ASSERT_EQUAL(ESP_OK, ret);

    // Start system monitor
    esp_gmf_app_sys_monitor_start();

    // Setup SD card
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);

    // Test codec setup (this might fail if no codec hardware is present)
    esp_gmf_app_setup_codec_dev(&codec_info);

    // Let everything run for a bit
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    // Clean up in reverse order
    esp_gmf_app_teardown_codec_dev();
    esp_gmf_app_teardown_sdcard(sdcard_handle);
    esp_gmf_app_sys_monitor_stop();

    vTaskDelay(500 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Integration test completed successfully");
}

TEST_CASE("esp_gmf_app_wifi_lifecycle", "[gmf_app_utils][wifi][leaks=13000]")
{
    esp_gmf_app_wifi_connect();
    ESP_LOGI(TAG, "WiFi connected successfully");

    // Wait a bit to ensure connection is stable
    vTaskDelay(2000 / portTICK_PERIOD_MS);

    esp_gmf_app_wifi_disconnect();
    ESP_LOGI(TAG, "WiFi disconnected successfully");
}
