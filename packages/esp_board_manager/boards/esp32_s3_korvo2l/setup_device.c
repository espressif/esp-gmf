/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_board_device.h"
#include "esp_log.h"
#include "dev_custom.h"

static const char *TAG = "SETUP_DEVICE";

int my_custom_sensor_init(void *config, int cfg_size, void **device_handle)
{
    ESP_LOGI(TAG, "Initializing my_custom_sensor device");
    // Allocate user device handle if needed
    *device_handle = NULL;  // For this example, we don't need a user handle
    return 0;
}

int my_custom_sensor_deinit(void *device_handle)
{
    ESP_LOGI(TAG, "Deinitializing my_custom_sensor device");
    // Cleanup user device handle if needed
    return 0;
}

CUSTOM_DEVICE_IMPLEMENT(my_custom_sensor, my_custom_sensor_init, my_custom_sensor_deinit);
