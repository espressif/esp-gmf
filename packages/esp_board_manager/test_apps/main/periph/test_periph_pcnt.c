/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/**
 * @brief  Periph PCNT test
 *
 *         This test example can be used to test the PCNT peripheral that has been initialized through the board manager.
 *         Use the PCNT peripheral to decode the differential signals generated from a EC11 rotary encoder (or other encoders which can produce quadrature waveforms)
 *
 */

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "periph_pcnt.h"

#define EXAMPLE_FRAME_DURATION_MS 10
#define EXAMPLE_TEST_SECONDS      5

static const char *TAG = "TEST_PCNT";

static bool example_pcnt_on_reach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
    BaseType_t high_task_wakeup;
    QueueHandle_t queue = (QueueHandle_t)user_ctx;
    // send event data to queue, from this interrupt callback
    xQueueSendFromISR(queue, &(edata->watch_point_value), &high_task_wakeup);
    return (high_task_wakeup == pdTRUE);
}

void test_periph_pcnt()
{
    periph_pcnt_handle_t *handle = NULL;
    esp_err_t ret = esp_board_manager_get_periph_handle(ESP_BOARD_PERIPH_NAME_PCNT_UNIT, (void **)&handle);
    if (ret != ESP_OK || handle == NULL) {
        ESP_LOGE(TAG, "Failed to get PCNT handle");
        return;
    }

    pcnt_unit_handle_t pcnt_unit = handle->pcnt_unit;
    pcnt_event_callbacks_t cbs = {
        .on_reach = example_pcnt_on_reach,
    };
    QueueHandle_t queue = xQueueCreate(10, sizeof(int));
    ESP_ERROR_CHECK(pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, queue));

    ESP_LOGI(TAG, "Enable pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_enable(pcnt_unit));
    ESP_LOGI(TAG, "Clear pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_LOGI(TAG, "Start pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_start(pcnt_unit));

    int pulse_count = 0;
    int event_count = 0;
    int test_cnt = 1000 / EXAMPLE_FRAME_DURATION_MS * EXAMPLE_TEST_SECONDS;
    while (test_cnt--) {
        if (xQueueReceive(queue, &event_count, pdMS_TO_TICKS(EXAMPLE_FRAME_DURATION_MS))) {
            ESP_LOGI(TAG, "Watch point event, count: %d", event_count);
        } else {
            ret = pcnt_unit_get_count(pcnt_unit, &pulse_count);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to get pulse count");
                break;
            }
            ESP_LOGI(TAG, "Pulse count test: %d", pulse_count);
        }
    }

    ESP_ERROR_CHECK(pcnt_unit_stop(pcnt_unit));
    ESP_LOGI(TAG, "Stop pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_clear_count(pcnt_unit));
    ESP_LOGI(TAG, "Clear pcnt unit");
    ESP_ERROR_CHECK(pcnt_unit_disable(pcnt_unit));
    ESP_LOGI(TAG, "Disable pcnt unit");
    return;
}
