/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "dev_led_strip.h"
#include "led_strip.h"
#include "test_dev_led_strip.h"

static const char *TAG = "TEST_LED_STRIP";

typedef struct {
    const char *name;
    uint8_t     red;
    uint8_t     green;
    uint8_t     blue;
} test_led_strip_color_t;

void test_dev_led_strip(void)
{
    ESP_LOGI(TAG, "=== LED Strip Device Test ===");

    dev_led_strip_handles_t *handles = NULL;
    dev_led_strip_config_t *config = NULL;
    esp_err_t ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LED_STRIP, (void **)&handles);
    if (ret != ESP_OK || handles == NULL) {
        ESP_LOGE(TAG, "Failed to get LED strip device handle");
        return;
    }

    ret = esp_board_manager_get_device_config(ESP_BOARD_DEVICE_NAME_LED_STRIP, (void **)&config);
    if (ret != ESP_OK || config == NULL) {
        ESP_LOGE(TAG, "Failed to get LED strip device config");
        return;
    }

    led_strip_handle_t strip = handles->strip_handle;
    if (strip == NULL) {
        ESP_LOGE(TAG, "LED strip handle is NULL");
        return;
    }

    uint32_t led_count = config->strip_config.max_leds;
    if (led_count == 0) {
        ESP_LOGE(TAG, "LED strip max_leds is 0");
        return;
    }

    ESP_LOGI(TAG, "Testing LED strip device %s, led_count=%" PRIu32, ESP_BOARD_DEVICE_NAME_LED_STRIP, led_count);

    const test_led_strip_color_t colors[] = {
        {"red", 255, 0, 0},
        {"green", 0, 255, 0},
        {"blue", 0, 0, 255},
        {"white", 64, 64, 64},
        {"off", 0, 0, 0},
    };

    for (int cycle = 0; cycle < 2; cycle++) {
        for (size_t color_idx = 0; color_idx < sizeof(colors) / sizeof(colors[0]); color_idx++) {
            ESP_LOGI(TAG, "Set LED strip to %s", colors[color_idx].name);
            for (uint32_t led = 0; led < led_count; led++) {
                ret = led_strip_set_pixel(strip, led, colors[color_idx].red, colors[color_idx].green,
                                          colors[color_idx].blue);
                if (ret != ESP_OK) {
                    ESP_LOGE(TAG, "Failed to set LED %" PRIu32 ": %s", led, esp_err_to_name(ret));
                    return;
                }
            }

            ret = led_strip_refresh(strip);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Failed to refresh LED strip: %s", esp_err_to_name(ret));
                return;
            }
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }

    led_strip_clear(strip);
    ESP_LOGI(TAG, "LED strip test completed");
}
