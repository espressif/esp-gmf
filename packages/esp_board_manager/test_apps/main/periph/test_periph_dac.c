/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/**
 * @brief  Periph DAC test
 *
 *         This test example can be used to test the dac peripheral that has been initialized through the board manager.
 *         Can be used to test oneshot, continuous and cosine modes
 *         oneshot: Sawtooth wave
 *         continuous: Triangle wave
 *         cosine: Cosine wave
 *
 */

#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_board_periph.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "periph_dac.h"

#define EXAMPLE_ARRAY_LEN     400
#define EXAMPLE_DAC_AMPLITUDE 255

static const char *TAG = "TEST_DAC";

static void test_dac_oneshot(periph_dac_handle_t *dac_handle)
{
    ESP_LOGI(TAG, "Testing DAC oneshot mode...");

    // Output different voltage levels
    int test_cnt = 20;
    while (test_cnt) {
        for (int i = 0; i <= EXAMPLE_DAC_AMPLITUDE; i += 10) {
            esp_err_t ret = dac_oneshot_output_voltage(dac_handle->oneshot, i);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Output voltage level: %d", i);
            } else {
                ESP_LOGE(TAG, "Failed to output voltage level %d: %s", i, esp_err_to_name(ret));
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        test_cnt--;
    }

    ESP_LOGI(TAG, "DAC oneshot test completed");
}

static void test_dac_continuous(periph_dac_handle_t *dac_handle)
{
    ESP_LOGI(TAG, "Testing DAC continuous mode...");

    // Enable continuous mode
    esp_err_t ret = dac_continuous_enable(dac_handle->continuous);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable continuous mode: %s", esp_err_to_name(ret));
        return;
    }

    // Create a simple triangle wave buffer
    uint32_t pnt_num = EXAMPLE_ARRAY_LEN;
    uint8_t tri_wav[pnt_num];
    for (int i = 0; i < pnt_num; i++) {
        tri_wav[i] = (i > (pnt_num / 2)) ? (2 * EXAMPLE_DAC_AMPLITUDE * (pnt_num - i) / pnt_num) : (2 * EXAMPLE_DAC_AMPLITUDE * i / pnt_num);
    }

    // Output triangle wave cyclically
    ret = dac_continuous_write_cyclically(dac_handle->continuous, tri_wav, sizeof(tri_wav), NULL);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Started cyclic triangle wave output");
        vTaskDelay(pdMS_TO_TICKS(5000));  // Run for 5 seconds
    } else {
        ESP_LOGE(TAG, "Failed to start cyclic output: %s", esp_err_to_name(ret));
    }

    // Disable continuous mode
    dac_continuous_disable(dac_handle->continuous);
    ESP_LOGI(TAG, "DAC continuous test completed");
}

static void test_dac_cosine(periph_dac_handle_t *dac_handle)
{
    ESP_LOGI(TAG, "Testing DAC cosine mode...");

    // Start cosine wave output
    esp_err_t ret = dac_cosine_start(dac_handle->cosine);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Started cosine wave output");
        vTaskDelay(pdMS_TO_TICKS(5000));  // Run for 5 seconds
    } else {
        ESP_LOGE(TAG, "Failed to start cosine wave: %s", esp_err_to_name(ret));
        return;
    }

    // Stop cosine wave output
    ret = dac_cosine_stop(dac_handle->cosine);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Stopped cosine wave output");
    } else {
        ESP_LOGE(TAG, "Failed to stop cosine wave: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "DAC cosine test completed");
}

void test_periph_dac(void)
{
    ESP_LOGI(TAG, "=== Starting DAC Peripheral Test ===");
    periph_dac_handle_t *dac_handle = NULL;

    // Get DAC handles
    esp_err_t err = esp_board_manager_get_periph_handle(ESP_BOARD_PERIPH_NAME_DAC, (void **)&dac_handle);
    if (err != ESP_OK || !dac_handle) {
        ESP_LOGE(TAG, "Failed to get DAC peripheral handle");
        return;
    }

    const char *periph_name = NULL;
    err = esp_board_periph_get_name_by_handle(dac_handle, &periph_name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get periph name: %s", esp_err_to_name(err));
        return;
    }

    periph_dac_config_t *cfg = NULL;
    err = esp_board_manager_get_periph_config(periph_name, (void **)&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get config: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "=== Testing DAC Modes ===");

    if (cfg->role == ESP_BOARD_PERIPH_ROLE_ONESHOT) {
        test_dac_oneshot(dac_handle);
    } else if (cfg->role == ESP_BOARD_PERIPH_ROLE_CONTINUOUS) {
        test_dac_continuous(dac_handle);
    } else if (cfg->role == ESP_BOARD_PERIPH_ROLE_COSINE) {
        test_dac_cosine(dac_handle);
    } else {
        ESP_LOGE(TAG, "Unknown DAC role: %d", cfg->role);
    }

    ESP_LOGI(TAG, "=== DAC Peripheral Test Completed ===");
}
