/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_board_manager_includes.h"

static const char *TAG = "VIDEO_PLAYER_MAIN";

int video_player_run_demo(void);

static esp_err_t board_init_optional_gpio_expander(void)
{
#if CONFIG_ESP_BOARD_DEV_GPIO_EXPANDER_SUPPORT
    static bool gpio_expander_inited = false;
    if (gpio_expander_inited) {
        return ESP_OK;
    }
    esp_err_t ret = esp_board_manager_init_device_by_name(ESP_BOARD_DEVICE_NAME_GPIO_EXPANDER);
    if (ret == ESP_OK) {
        gpio_expander_inited = true;
    } else {
        ESP_LOGW(TAG, "Failed to initialize gpio expander: %s", esp_err_to_name(ret));
    }
#endif  /* CONFIG_ESP_BOARD_DEV_GPIO_EXPANDER_SUPPORT */
    return ESP_OK;
}

void app_main(void)
{
    board_init_optional_gpio_expander();
    int ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_DISPLAY_LCD);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to initialize LCD");
        return;
    }
    ret = esp_board_device_init(ESP_BOARD_DEVICE_NAME_FS_SDCARD);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SD card");
        return;
    }
    video_player_run_demo();
}
