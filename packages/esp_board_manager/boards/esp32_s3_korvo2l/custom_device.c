/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_board_device.h"
#include "esp_log.h"
#include "dev_custom.h"

static const char *TAG = "SETUP_DEVICE";

int my_custom_sensor_init(void *config, int cfg_size, void **device_handle)
{
    uint32_t *user_handle = (uint32_t*)malloc(sizeof(uint32_t));
    if (!user_handle) {
        ESP_LOGE(TAG, "Failed to allocate user handle");
        return -1;
    }
    *user_handle = 0x99887766;
    ESP_LOGI(TAG, "User handle allocated: %p", user_handle);
    *device_handle = user_handle;
    return 0;
}

int my_custom_sensor_deinit(void *device_handle)
{
    ESP_LOGI(TAG, "Deinitializing my_custom_sensor device handle: %p", device_handle);
    free(device_handle);
    return 0;
}

CUSTOM_DEVICE_IMPLEMENT(my_custom_sensor, my_custom_sensor_init, my_custom_sensor_deinit);
