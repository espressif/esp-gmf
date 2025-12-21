/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/**
 * @brief  Periph MCPWM test
 *
 *         This test example can be used to test the mcpwm peripheral that has been initialized through the board manager.
 *         It will generate three PWM waves with duty cycles of 25%, 50%, and 75% and output them through the IO configured by `board_peripherals.yaml`
 *
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_board_periph.h"
#include "esp_board_manager_defs.h"
#include "periph_mcpwm.h"

#define EXAMPLE_MCPWM_DELAY_MS 1000

static const char *TAG          = "TEST_MCPWM";
static uint32_t    period_ticks = 0;

static void test_mcpwm_setup_pwm_waveform(periph_mcpwm_handle_t *mcpwm_handle)
{
    ESP_LOGI(TAG, "Setting up PWM waveform...");

    // Set up standard PWM waveform: High at timer start, Low at compare event
    esp_err_t ret;

    // Set action: High when timer counts to zero (start of period)
    ret = mcpwm_generator_set_action_on_timer_event(mcpwm_handle->generator,
                                                    MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set timer event action: %s", esp_err_to_name(ret));
        return;
    }

    // Set action: Low when comparator 0 matches (controls duty cycle)
    if (mcpwm_handle->num_comparators >= 1) {
        ret = mcpwm_generator_set_action_on_compare_event(mcpwm_handle->generator,
                                                          MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, mcpwm_handle->comparators[0], MCPWM_GEN_ACTION_LOW));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set compare event action: %s", esp_err_to_name(ret));
            return;
        }
    }

    ESP_LOGI(TAG, "PWM waveform setup completed");
}

static void test_mcpwm_duty_cycle(periph_mcpwm_handle_t *mcpwm_handle)
{
    ESP_LOGI(TAG, "Testing MCPWM duty cycle control...");
    if (mcpwm_handle->num_comparators >= 1) {
        ESP_LOGI(TAG, "Timer period: %d ticks", period_ticks);
        // Calculate duty cycles based on timer period
        int test_duties[] = {
            period_ticks / 4,     // 25%
            period_ticks / 2,     // 50%
            period_ticks * 3 / 4  // 75%
        };
        for (int i = 0; i < 3; i++) {
            int duty = test_duties[i];
            // 计算对应的百分比
            float duty_percent = (float)duty / period_ticks * 100.0f;
            esp_err_t ret = mcpwm_comparator_set_compare_value(mcpwm_handle->comparators[0], duty);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "Set duty cycle: %d ticks (%.1f%%) - holding for 3 seconds", duty, duty_percent);
            } else {
                ESP_LOGE(TAG, "Failed to set duty cycle %d: %s", duty, esp_err_to_name(ret));
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
        }
    }
    ESP_LOGI(TAG, "MCPWM duty cycle test completed");
}

static void test_mcpwm_configuration(periph_mcpwm_handle_t *mcpwm_handle)
{
    ESP_LOGI(TAG, "Testing MCPWM configuration...");

    // Test force level control
    ESP_LOGI(TAG, "Setting force level to HIGH");
    esp_err_t ret = mcpwm_generator_set_force_level(mcpwm_handle->generator, 1, true);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Force level set to HIGH successfully");
    } else {
        ESP_LOGE(TAG, "Failed to set force level: %s", esp_err_to_name(ret));
    }

    vTaskDelay(pdMS_TO_TICKS(3000));

    // Remove force level
    ESP_LOGI(TAG, "Removing force level");
    ret = mcpwm_generator_set_force_level(mcpwm_handle->generator, -1, true);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Force level removed successfully");
    } else {
        ESP_LOGE(TAG, "Failed to remove force level: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "MCPWM configuration test completed");
}

void test_periph_mcpwm(void)
{
    ESP_LOGI(TAG, "=== Starting MCPWM Peripheral Test ===");
    periph_mcpwm_handle_t *mcpwm_handle = NULL;
    periph_mcpwm_config_t *mcpwm_cfg = NULL;
    esp_err_t ret = esp_board_periph_get_config(ESP_BOARD_PERIPH_NAME_MCPWM, (void **)&mcpwm_cfg);
    if (ret != ESP_OK || !mcpwm_cfg) {
        ESP_LOGE(TAG, "Failed to get MCPWM peripheral configuration");
        return;
    }

    // Get MCPWM handle
    ret = esp_board_periph_get_handle(ESP_BOARD_PERIPH_NAME_MCPWM, (void **)&mcpwm_handle);
    if (ret != ESP_OK || !mcpwm_handle) {
        ESP_LOGE(TAG, "Failed to get MCPWM peripheral handle");
        return;
    }

    ESP_LOGI(TAG, "MCPWM handle obtained: num_comparators=%d", mcpwm_handle->num_comparators);

    // Enable and start timer
    ret = mcpwm_timer_enable(mcpwm_handle->timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcpwm_timer_enable failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = mcpwm_timer_start_stop(mcpwm_handle->timer, MCPWM_TIMER_START_NO_STOP);
    if (ret != ESP_OK) {
        mcpwm_timer_disable(mcpwm_handle->timer);
        ESP_LOGE(TAG, "mcpwm_timer_start_stop failed: %s", esp_err_to_name(ret));
        return;
    }

    period_ticks = mcpwm_cfg->timer_config.period_ticks;
    test_mcpwm_setup_pwm_waveform(mcpwm_handle);
    ESP_LOGI(TAG, "=== Testing MCPWM Configuration ===");
    test_mcpwm_configuration(mcpwm_handle);

    ESP_LOGI(TAG, "=== Testing MCPWM Duty Cycle Control ===");
    test_mcpwm_duty_cycle(mcpwm_handle);

    ESP_LOGI(TAG, "=== MCPWM Peripheral Test Completed ===");

    // Stop and disable timer
    ret = mcpwm_timer_start_stop(mcpwm_handle->timer, MCPWM_TIMER_STOP_EMPTY);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Call to mcpwm_timer_start_stop failed: %s", esp_err_to_name(ret));
    }

    ret = mcpwm_timer_disable(mcpwm_handle->timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Call to mcpwm_timer_disable failed: %s", esp_err_to_name(ret));
    }
}
