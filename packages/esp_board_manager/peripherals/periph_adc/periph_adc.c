/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "esp_board_manager_defs.h"
#include "esp_board_periph.h"
#include "periph_adc.h"

static const char *TAG = "PERIPH_ADC";

static inline esp_err_t continuous_adc_init(periph_adc_config_t *adc_cfg, periph_adc_handle_t *handle)
{
    esp_err_t err = ESP_FAIL;
    adc_continuous_handle_t continuous_handle = NULL;

    err = adc_continuous_new_handle(&adc_cfg->cfg.continuous.handle_cfg, &continuous_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Create adc continuous handle failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = adc_cfg->cfg.continuous.sample_freq_hz,
        .conv_mode = adc_cfg->cfg.continuous.conv_mode,
        .format = adc_cfg->cfg.continuous.format,
    };
    int pattern_num = adc_cfg->cfg.continuous.pattern_num;
    adc_digi_pattern_config_t adc_pattern[SOC_ADC_PATT_LEN_MAX] = {0};
    dig_cfg.pattern_num = pattern_num;
    for (int i = 0; i < pattern_num && i < SOC_ADC_PATT_LEN_MAX; i++) {
        adc_pattern[i].atten = adc_cfg->cfg.continuous.atten;
        adc_pattern[i].channel = adc_cfg->cfg.continuous.channel_id[i];
        adc_pattern[i].unit = adc_cfg->cfg.continuous.unit_id;
        adc_pattern[i].bit_width = adc_cfg->cfg.continuous.bit_width;
    }
    dig_cfg.adc_pattern = adc_pattern;

    err = adc_continuous_config(continuous_handle, &dig_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configure continuous adc handle failed: %s", esp_err_to_name(err));
        adc_continuous_deinit(continuous_handle);
        return err;
    }
    handle->continuous = continuous_handle;
    ESP_LOGI(TAG, "Create adc continuous handle success");
    return ESP_OK;
}

static inline esp_err_t oneshot_adc_init(periph_adc_config_t *adc_cfg, periph_adc_handle_t *handle)
{
    esp_err_t err = ESP_FAIL;
    adc_oneshot_unit_handle_t oneshot_handle = NULL;

    err = adc_oneshot_new_unit(&adc_cfg->cfg.oneshot.unit_cfg, &oneshot_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Create adc oneshot unit failed: %s", esp_err_to_name(err));
        return err;
    }

    adc_channel_t channel = (adc_channel_t)adc_cfg->cfg.oneshot.channel_id;
    err = adc_oneshot_config_channel(oneshot_handle, channel, &adc_cfg->cfg.oneshot.chan_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Configure adc oneshot channel failed: %s", esp_err_to_name(err));
        adc_oneshot_del_unit(oneshot_handle);
        return err;
    }
    handle->oneshot = oneshot_handle;
    ESP_LOGI(TAG, "Create adc oneshot unit success");
    return ESP_OK;
}

int periph_adc_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size != sizeof(periph_adc_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    periph_adc_config_t *adc_cfg = (periph_adc_config_t *)cfg;

    periph_adc_handle_t *handle = (periph_adc_handle_t *)calloc(1, sizeof(periph_adc_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for periph_adc_handle_t");
        return -1;
    }

    esp_err_t err = ESP_FAIL;
    if (adc_cfg->role == ESP_BOARD_PERIPH_ROLE_CONTINUOUS) {
        err = continuous_adc_init(adc_cfg, handle);
        if (err != ESP_OK) {
            goto cleanup;
        }
    } else if (adc_cfg->role == ESP_BOARD_PERIPH_ROLE_ONESHOT) {
        err = oneshot_adc_init(adc_cfg, handle);
        if (err != ESP_OK) {
            goto cleanup;
        }
    } else {
        ESP_LOGE(TAG, "Unknown role %d", adc_cfg->role);
        goto cleanup;
    }

    *periph_handle = (void *)handle;
    return 0;

cleanup:
    free(handle);
    return -1;
}

int periph_adc_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }
    periph_adc_handle_t *handle = (periph_adc_handle_t *)periph_handle;
    const char *periph_name = NULL;
    esp_err_t err = esp_board_periph_get_name_by_handle(periph_handle, &periph_name);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get periph name: %s", esp_err_to_name(err));
        goto cleanup;
    }

    periph_adc_config_t *cfg = NULL;
    err = esp_board_periph_get_config(periph_name, (void **)&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get config: %s", esp_err_to_name(err));
        goto cleanup;
    }

    if (cfg->role == ESP_BOARD_PERIPH_ROLE_CONTINUOUS) {
        adc_continuous_handle_t continuous_handle = handle->continuous;
        err = adc_continuous_deinit(continuous_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to deinit continuous_handle failed: %s", esp_err_to_name(err));
            goto cleanup;
        }
    } else if (cfg->role == ESP_BOARD_PERIPH_ROLE_ONESHOT) {
        adc_oneshot_unit_handle_t oneshot_handle = handle->oneshot;
        err = adc_oneshot_del_unit(oneshot_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to del oneshot_handle: %s", esp_err_to_name(err));
            goto cleanup;
        }
    } else {
        ESP_LOGW(TAG, "Unknown role on deinit: %d", cfg->role);
    }

    ESP_LOGI(TAG, "Deinitialize periph_adc role=%d", cfg->role);

    free(handle);
    return 0;

cleanup:
    free(handle);
    return -1;
}
