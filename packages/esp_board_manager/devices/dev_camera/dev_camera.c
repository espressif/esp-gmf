/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <unistd.h>
#include "esp_log.h"
#include "esp_video_init.h"
#include "dev_camera.h"
#include "esp_board_device.h"
#include "esp_board_periph.h"

static const char *TAG = "DEV_CAMERA";

extern esp_err_t camera_factory_entry_t(const dev_camera_config_t *camera_cfg, dev_camera_handle_t *ret_camera);

int dev_camera_init(void *cfg, int cfg_size, void **device_handle)
{
    if (!cfg || !device_handle) {
        ESP_LOGE(TAG, "Invalid parameters, cfg: %p, device_handle: %p", cfg, device_handle);
        return -1;
    }

    const dev_camera_config_t *camera_cfg = (const dev_camera_config_t *)cfg;
    dev_camera_handle_t *camera_handles = (dev_camera_handle_t *)calloc(1, sizeof(dev_camera_handle_t));
    if (camera_handles == NULL) {
        ESP_LOGE(TAG, "Failed to allocate camera handles");
        return -1;
    }
    esp_err_t ret = camera_factory_entry_t(camera_cfg, camera_handles);
    if (ret != ESP_OK || !camera_handles) {
        ESP_LOGE(TAG, "Failed to create camera handle\n");
        free(camera_handles);
        return -1;
    }

    ESP_LOGI(TAG, "Successfully initialized camera device: %s, p: %p", camera_cfg->name, camera_handles);
    *device_handle = camera_handles;
    return 0;
}

int dev_camera_deinit(void *device_handle)
{
    if (device_handle == NULL) {
        ESP_LOGE(TAG, "Invalid parameters, device_handle is NULL");
        return -1;
    }
    dev_camera_handle_t *camera_handles = (dev_camera_handle_t *)device_handle;
    esp_video_deinit();

    const char *name = NULL;
    const esp_board_device_handle_t *device_handle_struct = esp_board_device_find_by_handle(device_handle);
    if (device_handle_struct) {
        name = device_handle_struct->name;
    }
    dev_camera_config_t *cfg = NULL;
    esp_board_device_get_config(name, (void **)&cfg);
    if (cfg) {
        esp_board_periph_unref_handle(cfg->config.dvp.i2c_name);
    }

    free(camera_handles);
    return 0;
}
