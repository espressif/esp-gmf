/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include "esp_log.h"
#include "esp_video_init.h"
#include "dev_camera.h"
#include "esp_board_periph.h"
#include "esp_board_entry.h"
#include "esp_board_device.h"
#include "esp_video_device.h"

static const char *TAG = "DEV_CAMERA_SUB_CSI";

typedef struct {
    dev_camera_handle_t base;
    esp_cam_sensor_xclk_handle_t xclk_handle;
} dev_camera_csi_priv_handle_t;


int dev_camera_sub_csi_init(void *cfg, int cfg_size, void **device_handle)
{
    // No need to check parameters here, it will be checked in dev_camera_init
    esp_err_t ret = ESP_FAIL;

    const dev_camera_config_t *config = (const dev_camera_config_t *)cfg;

    ESP_LOGI(TAG, "Initializing CSI camera...");

    void *i2c_handle = NULL;
    ret = esp_board_periph_ref_handle(config->sub_cfg.csi.i2c_name, &i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get I2C handle for CSI camera");
        return -1;
    }

    // Get LDO peripheral handle
    void *ldo_handle = NULL;
    if (config->sub_cfg.csi.ldo_name && strlen(config->sub_cfg.csi.ldo_name) > 0) {
        ret = esp_board_periph_ref_handle(config->sub_cfg.csi.ldo_name, &ldo_handle);
        if (ret != 0) {
            ESP_LOGE(TAG, "Failed to get LDO peripheral handle: %d", ret);
            esp_board_periph_unref_handle(config->sub_cfg.csi.i2c_name);
            return -1;
        }
    }

    esp_cam_sensor_xclk_handle_t xclk_handle = NULL;
    // Check if XCLK pin is configured (>= 0) and frequency is valid
    // Note: We access esp_clock_router_cfg directly as we assume this mode
    if (config->sub_cfg.csi.xclk_config.esp_clock_router_cfg.xclk_pin != -1 &&
        config->sub_cfg.csi.xclk_config.esp_clock_router_cfg.xclk_freq_hz > 0) {

        ESP_LOGI(TAG, "Initializing XCLK on pin %d with freq %" PRIu32 "Hz...",
                 config->sub_cfg.csi.xclk_config.esp_clock_router_cfg.xclk_pin,
                 config->sub_cfg.csi.xclk_config.esp_clock_router_cfg.xclk_freq_hz);

        ret = esp_cam_sensor_xclk_allocate(ESP_CAM_SENSOR_XCLK_ESP_CLOCK_ROUTER, &xclk_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to allocate XCLK handle");
            return -1;
        }

        // We can pass the address of xclk_config directly as it matches the type
        ret = esp_cam_sensor_xclk_start(xclk_handle, &config->sub_cfg.csi.xclk_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start XCLK");
            esp_cam_sensor_xclk_free(xclk_handle);
            return -1;
        }
    }

    esp_video_init_csi_config_t s_csi_config = {
        .sccb_config.init_sccb = false,
        .sccb_config.i2c_handle = i2c_handle,
        .sccb_config.freq = config->sub_cfg.csi.i2c_freq,
        .reset_pin = config->sub_cfg.csi.reset_io,
        .pwdn_pin = config->sub_cfg.csi.pwdn_io,
        .dont_init_ldo = config->sub_cfg.csi.dont_init_ldo,
    };

    const esp_video_init_config_t cam_config = {
        .csi = &s_csi_config,
    };
    dev_camera_csi_priv_handle_t *priv_handle = calloc(1, sizeof(dev_camera_csi_priv_handle_t));
    if (priv_handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        goto cleanup;
    }
    dev_camera_handle_t *handle = &priv_handle->base;

    ret = esp_video_init(&cam_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize CSI camera driver: %s", esp_err_to_name(ret));
        free(priv_handle);
        goto cleanup;
    }

    handle->dev_path = ESP_VIDEO_MIPI_CSI_DEVICE_NAME;
    handle->meta_path = ESP_VIDEO_ISP1_DEVICE_NAME;
    priv_handle->xclk_handle = xclk_handle;
    *device_handle = handle;
    ESP_LOGI(TAG, "CSI camera initialized successfully, dev_path: %s", handle->dev_path);
    return 0;
cleanup:
    esp_cam_sensor_xclk_stop(xclk_handle);
    esp_cam_sensor_xclk_free(xclk_handle);
    esp_board_periph_unref_handle(config->sub_cfg.csi.ldo_name);
    esp_board_periph_unref_handle(config->sub_cfg.csi.i2c_name);
    return -1;
}

int dev_camera_sub_csi_deinit(void *device_handle)
{
    dev_camera_handle_t *handle = (dev_camera_handle_t *)device_handle;
    dev_camera_csi_priv_handle_t *priv_handle = __containerof(handle, dev_camera_csi_priv_handle_t, base);

    ESP_LOGI(TAG, "Deinitializing CSI camera...");

    if (priv_handle->xclk_handle) {
        esp_cam_sensor_xclk_stop(priv_handle->xclk_handle);
        esp_cam_sensor_xclk_free(priv_handle->xclk_handle);
    }

    // Deinitialize CSI camera
    // In the current version of esp_video(1.4.0), it will deinit all cameras that have been initialized
    esp_err_t ret = esp_video_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize CSI camera: %s", esp_err_to_name(ret));
    }

    dev_camera_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(handle, (void **)&cfg);
    if (cfg) {
        esp_board_periph_unref_handle(cfg->sub_cfg.csi.i2c_name);
        if (cfg->sub_cfg.csi.ldo_name && strlen(cfg->sub_cfg.csi.ldo_name) > 0) {
            esp_board_periph_unref_handle(cfg->sub_cfg.csi.ldo_name);
        }
    }
    free(priv_handle);
    return ret == ESP_OK ? 0 : -1;
}

ESP_BOARD_ENTRY_IMPLEMENT(csi, dev_camera_sub_csi_init, dev_camera_sub_csi_deinit);
