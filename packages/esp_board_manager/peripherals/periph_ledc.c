/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "periph_ledc.h"
#include "driver/ledc.h"
#include <string.h>
#include "esp_log.h"

static const char *TAG = "PERIPH_LEDC";

int periph_ledc_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || cfg_size < sizeof(periph_ledc_config_t) || (periph_handle == NULL)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    periph_ledc_config_t config = {0};
    memcpy(&config, cfg, cfg_size);

    // Configure LEDC timer first
    ledc_timer_config_t ledc_timer = {
        .speed_mode = config.handle.speed_mode,
        .duty_resolution = config.duty_resolution,
        .timer_num = config.timer_sel,
        .freq_hz = config.freq_hz,
        .clk_cfg = LEDC_AUTO_CLK};

    esp_err_t err = ledc_timer_config(&ledc_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_timer_config failed: %d", err);
        return -1;
    }

    // Configure LEDC channel
    ledc_channel_config_t ledc_channel = {
        .speed_mode = config.handle.speed_mode,
        .channel = config.handle.channel,
        .timer_sel = config.timer_sel,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = config.gpio_num,
        .duty = config.duty,
        .hpoint = 0,
        .flags.output_invert = config.output_invert,
        .sleep_mode = config.sleep_mode,
    };

    err = ledc_channel_config(&ledc_channel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_channel_config failed: %d", err);
        return -1;
    }

    // Create handle with channel info
    periph_ledc_handle_t *handle = malloc(sizeof(periph_ledc_handle_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return -1;
    }
    memcpy(handle, &config.handle, sizeof(periph_ledc_handle_t));
    *periph_handle = handle;

    ESP_LOGI(TAG, "LEDC initialized: speed_mode=%d, channel=%d, gpio=%d, freq=%" PRIu32 "Hz",
             handle->speed_mode, handle->channel, config.gpio_num, config.freq_hz);
    return 0;
}

int periph_ledc_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }
    periph_ledc_handle_t *handle = (periph_ledc_handle_t *)periph_handle;
    // Stop the LEDC channel
    esp_err_t err = ledc_stop(handle->speed_mode, handle->channel, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc_stop failed: %d", err);
        return -1;
    }
    ESP_LOGI(TAG, "LEDC deinitialized: speed_mode=%d, channel=%d", handle->speed_mode, handle->channel);
    free(periph_handle);
    return 0;
}
