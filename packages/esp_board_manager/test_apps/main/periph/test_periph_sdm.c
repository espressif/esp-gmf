/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/**
 * @brief  Periph SDM test
 *
 *         This test example can be used to test the sdm peripheral that has been initialized through the board manager.
 *         Can generate PWM signals used to control RGB lights to produce a breathing light effect.
 *
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_board_periph.h"
#include "periph_sdm.h"

#define EXAMPLE_SDM_DUTY_STEP 2
#define EXAMPLE_SDM_DUTY_MAX  120
#define EXAMPLE_SDM_DUTY_MIN  -120
#define EXAMPLE_SDM_DELAY_MS  10

static const char *TAG = "TEST_SDM";

static void test_sdm_pulse_density(periph_sdm_handle_t *sdm_handle)
{
    ESP_LOGI(TAG, "Testing SDM pulse density modulation...");

    int8_t duty = EXAMPLE_SDM_DUTY_MIN;
    int step = EXAMPLE_SDM_DUTY_STEP;
    int test_cycles = 10;  // Test for 10 cycles

    while (test_cycles) {
        esp_err_t ret = sdm_channel_set_pulse_density(sdm_handle->sdm_chan, duty);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Set pulse density: %d", duty);
        } else {
            ESP_LOGE(TAG, "Failed to set pulse density %d: %s", duty, esp_err_to_name(ret));
        }

        vTaskDelay(pdMS_TO_TICKS(EXAMPLE_SDM_DELAY_MS));

        duty += step;
        if (duty == EXAMPLE_SDM_DUTY_MAX || duty == EXAMPLE_SDM_DUTY_MIN) {
            step *= -1;
            test_cycles--;
        }
    }

    ESP_LOGI(TAG, "SDM pulse density test completed");
}

void test_periph_sdm(void)
{
    ESP_LOGI(TAG, "=== Starting SDM Peripheral Test ===");
    periph_sdm_handle_t *sdm_handle = NULL;

    // Get SDM handle
    esp_err_t ret = esp_board_periph_get_handle("sdm_handle", (void **)&sdm_handle);
    if (ret != ESP_OK || !sdm_handle) {
        ESP_LOGE(TAG, "Failed to get SDM peripheral handle");
        return;
    }

    ret = sdm_channel_enable(sdm_handle->sdm_chan);
    if (ret != ESP_OK) {
        sdm_del_channel(sdm_handle->sdm_chan);
        free(sdm_handle);
        ESP_LOGE(TAG, "sdm_channel_enable failed: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "=== Testing SDM Pulse Density Modulation ===");
    test_sdm_pulse_density(sdm_handle);

    ESP_LOGI(TAG, "=== SDM Peripheral Test Completed ===");

    ret = sdm_channel_disable(sdm_handle->sdm_chan);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Call to sdm_channel_disable failed: %s", esp_err_to_name(ret));
        return;
    }
}
