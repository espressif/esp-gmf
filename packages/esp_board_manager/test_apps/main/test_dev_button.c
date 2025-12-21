/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_board_manager.h"
#include "dev_button.h"
#include "test_dev_button.h"
#include "esp_board_device.h"

static const char *TAG = "TEST_BUTTON";

static void simple_button_event_handler(void *arg, void *data)
{
    button_handle_t btn_handle = (button_handle_t)arg;
    button_event_t event = iot_button_get_event(btn_handle);
    const char *event_str = iot_button_get_event_str(event);

    switch (event) {
        case BUTTON_PRESS_DOWN:
            ESP_LOGI(TAG, "Press down event (%s)", (char *)data);
            break;
        case BUTTON_PRESS_UP:
            ESP_LOGI(TAG, "Press up event (%s)", (char *)data);
            break;
        case BUTTON_SINGLE_CLICK:
            ESP_LOGI(TAG, "Single click event (%s)", (char *)data);
            break;
        case BUTTON_DOUBLE_CLICK:
            ESP_LOGI(TAG, "Double click event (%s)", (char *)data);
            break;
        case BUTTON_MULTIPLE_CLICK:
            ESP_LOGI(TAG, "Multiple click event: %d times (%s)", iot_button_get_repeat(btn_handle), (char *)data);
            break;
        case BUTTON_LONG_PRESS_START:
            ESP_LOGI(TAG, "Long press start (duration: %dms) (%s)", iot_button_get_pressed_time(btn_handle), (char *)data);
            break;
        case BUTTON_LONG_PRESS_HOLD:
            ESP_LOGI(TAG, "Long press hold (hold count: %d) (%s)", iot_button_get_long_press_hold_cnt(btn_handle), (char *)data);
            break;
        case BUTTON_LONG_PRESS_UP:
            ESP_LOGI(TAG, "Long press release (total duration: %dms) (%s)", iot_button_get_pressed_time(btn_handle), (char *)data);
            break;
        case BUTTON_PRESS_REPEAT:
            ESP_LOGI(TAG, "Press repeat (count: %d) (%s)", iot_button_get_repeat(btn_handle), (char *)data);
            break;
        case BUTTON_PRESS_REPEAT_DONE:
            ESP_LOGI(TAG, "Press repeat done (total count: %d) (%s)", iot_button_get_repeat(btn_handle), (char *)data);
            break;
        case BUTTON_PRESS_END:
            ESP_LOGI(TAG, "Press end (%s)", (char *)data);
            break;
        default:
            ESP_LOGI(TAG, "Event: %s", event_str);
            break;
    }
}

#ifdef CONFIG_ESP_BOARD_DEV_BUTTON_SUB_GPIO_SUPPORT
static int test_gpio_button(void)
{
    ESP_LOGI(TAG, "=== Testing GPIO Button ===");
    // Get button handle
    dev_button_handles_t *btn_handles = NULL;
    esp_err_t ret = esp_board_manager_get_device_handle("gpio_button_0", (void **)&btn_handles);
    if (ret != ESP_OK || btn_handles == NULL) {
        ESP_LOGE(TAG, "Failed to get gpio_button_0 handle");
        return -1;
    }
    ESP_LOGI(TAG, "Successfully obtained button handle");

    ret = esp_board_device_callback_register("gpio_button_0", simple_button_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register callback for button: %s", "gpio_button_0");
        return -1;
    }
    return 0;
}
#endif  /* CONFIG_ESP_BOARD_DEV_BUTTON_SUB_GPIO_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_BUTTON_SUB_ADC_SUPPORT
static int test_adc_button(void)
{
    ESP_LOGI(TAG, "=== Testing ADC Button ===");
    // Get button handle
    dev_button_handles_t *btn_handles = NULL;
    const char *button_name = NULL;

    // Try to get button handle, support both single button and button group
    esp_err_t ret = esp_board_manager_get_device_handle("adc_button_group", (void **)&btn_handles);
    if (ret != ESP_OK || btn_handles == NULL) {
        ret = esp_board_manager_get_device_handle("adc_button_0", (void **)&btn_handles);
        if (ret != ESP_OK || btn_handles == NULL) {
            ESP_LOGE(TAG, "Failed to get ADC button handle");
            return -1;
        } else {
            button_name = "adc_button_0";
            ESP_LOGI(TAG, "Successfully obtained ADC button handle for single button: %s", button_name);
        }
    } else {
        button_name = "adc_button_group";
        ESP_LOGI(TAG, "Successfully obtained ADC button handle for multi-button: %s", button_name);
    }

    ret = esp_board_device_callback_register(button_name, simple_button_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register callback for button: %s", button_name);
        return -1;
    }
    return 0;
}
#endif  /* CONFIG_ESP_BOARD_DEV_BUTTON_SUB_ADC_SUPPORT */

void test_dev_button(void)
{
    int ret = -1;
#if CONFIG_ESP_BOARD_DEV_BUTTON_SUB_GPIO_SUPPORT
    ret = test_gpio_button();
#elif CONFIG_ESP_BOARD_DEV_BUTTON_SUB_ADC_SUPPORT
    ret = test_adc_button();
#endif  /* CONFIG_ESP_BOARD_DEV_BUTTON_SUB_GPIO_SUPPORT */
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to test dev button");
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(10000));  // Test for 10 seconds
    ESP_LOGI(TAG, "Dev button test completed");
}
