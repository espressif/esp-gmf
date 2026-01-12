/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "esp_board_manager.h"
#include "esp_board_manager_defs.h"
#include "dev_gpio_ctrl.h"
#include "test_dev_pwr_ctrl.h"

static const char *TAG = "TEST_PWR_CTRL";

esp_err_t test_dev_pwr_lcd_ctrl(bool enable)
{
    void *handle;
    esp_err_t ret = esp_board_manager_get_device_handle(ESP_BOARD_DEVICE_NAME_LCD_POWER, (void **)&handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NO LCD power GPIO, skip");
        return ESP_OK;
    }
    dev_gpio_ctrl_config_t *config = NULL;
    ret = esp_board_manager_get_device_config("lcd_power", (void **)&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get LCD power GPIO config: %s", esp_err_to_name(ret));
        return ret;
    }
    periph_gpio_handle_t *gpio_handle = (periph_gpio_handle_t *)handle;
    if (enable) {
        ret = gpio_set_level(gpio_handle->gpio_num, config->active_level);
    } else {
        ret = gpio_set_level(gpio_handle->gpio_num, config->default_level);
    }

    ESP_LOGI(TAG, "LCD-%d power %s", gpio_handle->gpio_num, enable ? "enabled" : "disabled");
    return ESP_OK;
}

esp_err_t test_dev_pwr_audio_ctrl(bool enable)
{
    void *handle;
    esp_err_t ret = esp_board_manager_get_device_handle("audio_power", (void **)&handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "NO audio power GPIO, skip");
        return ESP_OK;
    }
    dev_gpio_ctrl_config_t *config = NULL;
    ret = esp_board_manager_get_device_config("audio_power", (void **)&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get audio power GPIO config: %s", esp_err_to_name(ret));
        return ret;
    }
    periph_gpio_handle_t *gpio_handle = (periph_gpio_handle_t *)handle;
    if (enable) {
        ret = gpio_set_level(gpio_handle->gpio_num, config->active_level);
    } else {
        ret = gpio_set_level(gpio_handle->gpio_num, config->default_level);
    }

    ESP_LOGI(TAG, "Audio-%d power %s", gpio_handle->gpio_num, enable ? "enabled" : "disabled");
    return ESP_OK;
}
