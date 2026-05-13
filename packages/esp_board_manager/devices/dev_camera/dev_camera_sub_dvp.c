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
#include "esp_board_device.h"
#include "esp_board_entry.h"
#include "esp_video_device.h"
#include "driver/ledc.h"

static const char *TAG = "DEV_CAMERA_SUB_DVP";

#define DVP_XCLK_LEDC_TIMER     LEDC_TIMER_1
#define DVP_XCLK_LEDC_CHANNEL   LEDC_CHANNEL_1

int dev_camera_sub_dvp_init(void *cfg, int cfg_size, void **device_handle)
{
    // No need to check parameters here, it will be checked in dev_camera_init
    esp_err_t ret = ESP_FAIL;
    const dev_camera_config_t *config = (const dev_camera_config_t *)cfg;

    ESP_LOGI(TAG, "Initializing DVP camera...");

    void *i2c_handle = NULL;
    ret = esp_board_periph_ref_handle(config->sub_cfg.dvp.i2c_name, &i2c_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get I2C handle for DVP camera");
        return -1;
    }

    // ESP32-S3 DVP controller does not output XCLK until CAM transfer starts.
    // esp_video's init_dvp_clk_func configures the DVP clock divider and also
    // re-routes the GPIO to the DVP peripheral signal, which overrides any
    // LEDC output on that pin. Since CAM transfer is not started during init,
    // the pin ends up with no clock, and OV2640 cannot respond to SCCB.
    // Workaround: tell esp_video that XCLK is not used (xclk_io = -1), and
    // drive XCLK ourselves via LEDC before esp_video_init.
    if (config->sub_cfg.dvp.dvp_io.xclk_io >= 0 && config->sub_cfg.dvp.xclk_freq > 0) {
        ledc_timer_config_t ledc_timer = {
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .duty_resolution = LEDC_TIMER_1_BIT,
            .timer_num = DVP_XCLK_LEDC_TIMER,
            .freq_hz = config->sub_cfg.dvp.xclk_freq,
            .clk_cfg = LEDC_AUTO_CLK,
        };
        ledc_channel_config_t ledc_channel = {
            .gpio_num = config->sub_cfg.dvp.dvp_io.xclk_io,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel = DVP_XCLK_LEDC_CHANNEL,
            .timer_sel = DVP_XCLK_LEDC_TIMER,
            .duty = 1,
            .hpoint = 0,
        };
        ESP_LOGI(TAG, "Starting LEDC XCLK on GPIO %d, freq %lu Hz",
                 config->sub_cfg.dvp.dvp_io.xclk_io, config->sub_cfg.dvp.xclk_freq);
        ledc_timer_config(&ledc_timer);
        ledc_channel_config(&ledc_channel);
    }

    // Copy DVP pin config but mask out xclk_io so esp_video_init does not
    // re-configure the GPIO to the DVP peripheral signal (which would stop
    // the LEDC clock).
    esp_cam_ctlr_dvp_pin_config_t dvp_pin = config->sub_cfg.dvp.dvp_io;
    dvp_pin.xclk_io = GPIO_NUM_NC;

    esp_video_init_dvp_config_t s_dvp_config = {
        .sccb_config.init_sccb = false,
        .sccb_config.i2c_handle = i2c_handle,
        .sccb_config.freq = config->sub_cfg.dvp.i2c_freq,
        .reset_pin = config->sub_cfg.dvp.reset_io,
        .pwdn_pin = config->sub_cfg.dvp.pwdn_io,
        .dvp_pin = dvp_pin,
        .xclk_freq = 0,  // xclk is driven by LEDC, not DVP controller
    };

    const esp_video_init_config_t cam_config = {
        .dvp = &s_dvp_config,
    };
    dev_camera_handle_t *handle = calloc(1, sizeof(dev_camera_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory");
        goto cleanup;
    }
    ret = esp_video_init(&cam_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize DVP camera driver: %s", esp_err_to_name(ret));
        free(handle);
        goto cleanup;
    }

    handle->dev_path = ESP_VIDEO_DVP_DEVICE_NAME;
    *device_handle = handle;
    ESP_LOGI(TAG, "DVP camera initialized successfully, dev_path: %s", handle->dev_path);
    return 0;
cleanup:
    esp_board_periph_unref_handle(config->sub_cfg.dvp.i2c_name);
    return -1;
}

int dev_camera_sub_dvp_deinit(void *device_handle)
{
    dev_camera_handle_t *handle = (dev_camera_handle_t *)device_handle;
    ESP_LOGI(TAG, "Deinitializing DVP camera...");

    // Deinitialize DVP camera
    // In the current version of esp_video(1.4.0), it will deinit all cameras that have been initialized
    esp_err_t ret = esp_video_deinit();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to deinitialize DVP camera: %s", esp_err_to_name(ret));
    }

    // Stop LEDC XCLK
    ledc_stop(LEDC_LOW_SPEED_MODE, DVP_XCLK_LEDC_CHANNEL, 0);

    dev_camera_config_t *cfg = NULL;
    esp_board_device_get_config_by_handle(handle, (void **)&cfg);
    if (cfg) {
        esp_board_periph_unref_handle(cfg->sub_cfg.dvp.i2c_name);
    }
    free(device_handle);
    return ret == ESP_OK ? 0 : -1;
}

ESP_BOARD_ENTRY_IMPLEMENT(dvp, dev_camera_sub_dvp_init, dev_camera_sub_dvp_deinit);
