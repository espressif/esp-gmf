/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "periph_gpio.h"
#include "driver/gpio.h"
#include <string.h>
#include <stdint.h>
#include "esp_log.h"

static const char *TAG = "PERIPH_GPIO";

int periph_gpio_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || cfg_size < sizeof(periph_gpio_config_t) || (periph_handle == NULL)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    periph_gpio_handle_t *handle = calloc(1, sizeof(periph_gpio_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for periph_gpio_handle_t");
        return -1;
    }
    periph_gpio_config_t *periph_cfg = (periph_gpio_config_t *)cfg;
    gpio_config_t config = periph_cfg->gpio_config;
    handle->gpio_num = config.pin_bit_mask;
    // Configure GPIO
    config.pin_bit_mask = BIT64(handle->gpio_num);
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_config failed on pin %llu: %d", config.pin_bit_mask, err);
        return -1;
    }
    // Set default level if it's an output pin
    if (config.mode == GPIO_MODE_OUTPUT || config.mode == GPIO_MODE_INPUT_OUTPUT ||
        config.mode == GPIO_MODE_OUTPUT_OD || config.mode == GPIO_MODE_INPUT_OUTPUT_OD) {
        err = gpio_set_level(config.pin_bit_mask, periph_cfg->default_level);
        ESP_LOGI(TAG, "Initialize success, pin: %d, set the default level: %d", handle->gpio_num, periph_cfg->default_level);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "gpio_set_level failed: %d", err);
            return -1;
        }
    } else {
        ESP_LOGI(TAG, "Initialize success, pin: %d, default_level: %d", handle->gpio_num, periph_cfg->default_level);
    }

    *periph_handle = handle;
    return 0;
}

int periph_gpio_deinit(void *periph_handle)
{
    // GPIO deinit is optional; typically you may reset pin direction.
    // For this API, just return 0 for success.
    free(periph_handle);
    return 0;
}
