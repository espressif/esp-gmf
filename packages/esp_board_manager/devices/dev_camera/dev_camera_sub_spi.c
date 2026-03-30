/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include "esp_log.h"
#include "esp_video_device.h"
#include "dev_camera.h"
#include "esp_board_periph.h"
#include "esp_board_device.h"
#include "esp_board_entry.h"

static const char *TAG = "DEV_CAMERA_SUB_SPI";

int dev_camera_sub_spi_init(void *cfg, int cfg_size, void **device_handle)
{
#if !CONFIG_ESP_VIDEO_ENABLE_SPI_VIDEO_DEVICE
    ESP_LOGE(TAG, "Enable CONFIG_ESP_VIDEO_ENABLE_SPI_VIDEO_DEVICE (esp_video) for SPI camera");
    return -1;
#else
    esp_err_t ret;
    const dev_camera_config_t *config = (const dev_camera_config_t *)cfg;

    ESP_LOGI(TAG, "Initializing SPI camera (esp_video)...");

    void *i2c_handle = NULL;
    ret = esp_board_periph_ref_handle(config->sub_cfg.spi.i2c_name, &i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get I2C handle for SPI camera");
        return -1;
    }

    esp_video_init_spi_config_t spi_cfg = config->sub_cfg.spi.esp_video_spi;
    spi_cfg.sccb_config.init_sccb  = false;
    spi_cfg.sccb_config.freq       = config->sub_cfg.spi.i2c_freq;
    spi_cfg.sccb_config.i2c_handle = i2c_handle;

    const esp_video_init_config_t cam_config = {
        .spi = &spi_cfg,
    };

    dev_camera_handle_t *handle = calloc(1, sizeof(dev_camera_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        esp_board_periph_unref_handle(config->sub_cfg.spi.i2c_name);
        return -1;
    }

    ret = esp_video_init(&cam_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize SPI camera: %s", esp_err_to_name(ret));
        free(handle);
        esp_board_periph_unref_handle(config->sub_cfg.spi.i2c_name);
        return -1;
    }

    handle->dev_path  = ESP_VIDEO_SPI_DEVICE_NAME;
    handle->meta_path = NULL;
    *device_handle    = handle;
    ESP_LOGI(TAG, "SPI camera initialized, dev_path: %s", handle->dev_path);
    return 0;
#endif  /* !CONFIG_ESP_VIDEO_ENABLE_SPI_VIDEO_DEVICE */
}

int dev_camera_sub_spi_deinit(void *device_handle)
{
#if !CONFIG_ESP_VIDEO_ENABLE_SPI_VIDEO_DEVICE
    (void)device_handle;
    return 0;
#else
    dev_camera_handle_t *handle = (dev_camera_handle_t *)device_handle;
    if (handle == NULL) {
        return -1;
    }

    esp_err_t ret = esp_video_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_video_deinit failed: %s", esp_err_to_name(ret));
    }

    dev_camera_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(handle, (void **)&cfg);
    if (cfg) {
        esp_board_periph_unref_handle(cfg->sub_cfg.spi.i2c_name);
    }
    free(handle);
    return ret == ESP_OK ? 0 : -1;
#endif  /* !CONFIG_ESP_VIDEO_ENABLE_SPI_VIDEO_DEVICE */
}

/* Entry name must differ from display_lcd "spi" (see dev_camera.c mapping). */
ESP_BOARD_ENTRY_IMPLEMENT(camera_spi, dev_camera_sub_spi_init, dev_camera_sub_spi_deinit);
