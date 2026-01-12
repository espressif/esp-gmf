/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_log.h"
#include "periph_mcpwm.h"

static const char *TAG = "PERIPH_MCPWM";

int periph_mcpwm_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size < sizeof(periph_mcpwm_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }

    periph_mcpwm_config_t *mcpwm_cfg = (periph_mcpwm_config_t *)cfg;
    // Check if the number of comparators is within valid range
    if (mcpwm_cfg->num_comparators < 0 || mcpwm_cfg->num_comparators > SOC_MCPWM_COMPARATORS_PER_OPERATOR) {
        ESP_LOGE(TAG, "Invalid number of comparators: %d", mcpwm_cfg->num_comparators);
        return -1;
    }

    periph_mcpwm_handle_t *handle = (periph_mcpwm_handle_t *)calloc(1, sizeof(periph_mcpwm_handle_t));
    if (handle == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for periph_mcpwm_handle_t");
        return -1;
    }

    // Create MCPWM timer
    esp_err_t err = mcpwm_new_timer(&mcpwm_cfg->timer_config, &handle->timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Call to mcpwm_new_timer failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // Create MCPWM operator
    err = mcpwm_new_operator(&mcpwm_cfg->operator_config, &handle->operator);
    if (err != ESP_OK) {
        mcpwm_del_timer(handle->timer);
        ESP_LOGE(TAG, "Call to mcpwm_new_operator failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // Connect timer and operator
    err = mcpwm_operator_connect_timer(handle->operator, handle->timer);
    if (err != ESP_OK) {
        mcpwm_del_operator(handle->operator);
        mcpwm_del_timer(handle->timer);
        ESP_LOGE(TAG, "Call to mcpwm_operator_connect_timer failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // Create comparators
    handle->num_comparators = 0;
    for (int i = 0; i < mcpwm_cfg->num_comparators; i++) {
        err = mcpwm_new_comparator(handle->operator, & mcpwm_cfg->comparator_cfgs[i], &handle->comparators[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Create new comparator[%d] failed: %s", i, esp_err_to_name(err));
            goto cleanup;
        }

        handle->num_comparators++;
    }

    // Create generator
    err = mcpwm_new_generator(handle->operator, & mcpwm_cfg->generator_config, &handle->generator);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Call to mcpwm_new_generator failed: %s", esp_err_to_name(err));
        goto cleanup;
    }

    // Note: Generator event actions and comparator duty cycles are not set here
    // Users should configure these in application layer based on their specific requirements
    // This provides more flexibility for different PWM waveform patterns

    ESP_LOGI(TAG, "MCPWM peripheral initialized successfully");
    ESP_LOGD(TAG, "  GPIO: %d", mcpwm_cfg->generator_config.gen_gpio_num);
    ESP_LOGD(TAG, "  Resolution: %" PRIu32 " Hz", mcpwm_cfg->timer_config.resolution_hz);
    ESP_LOGD(TAG, "  Period: %" PRIu32 " ticks", mcpwm_cfg->timer_config.period_ticks);
    ESP_LOGD(TAG, "  Count mode: %d", mcpwm_cfg->timer_config.count_mode);
    ESP_LOGD(TAG, "  Comparators: %d", handle->num_comparators);

    *periph_handle = handle;
    return 0;

cleanup:
    // Cleanup on error
    for (int i = 0; i < handle->num_comparators; i++) {
        mcpwm_del_comparator(handle->comparators[i]);
    }
    if (handle->generator) {
        mcpwm_del_generator(handle->generator);
    }
    if (handle->operator) {
        mcpwm_del_operator(handle->operator);
    }
    if (handle->timer) {
        mcpwm_del_timer(handle->timer);
    }
    free(handle);
    return -1;
}

int periph_mcpwm_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }

    periph_mcpwm_handle_t *handle = (periph_mcpwm_handle_t *)periph_handle;
    esp_err_t err;
    if (handle->generator) {
        // Delete components in reverse order
        err = mcpwm_del_generator(handle->generator);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Call to mcpwm_del_generator failed: %s", esp_err_to_name(err));
        }
    }

    // Delete all comparators
    for (int i = 0; i < handle->num_comparators; i++) {
        err = mcpwm_del_comparator(handle->comparators[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Del comparator[%d] failed: %s", i, esp_err_to_name(err));
        }
    }

    if (handle->operator) {
        err = mcpwm_del_operator(handle->operator);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Call to mcpwm_del_operator failed: %s", esp_err_to_name(err));
        }
    }

    if (handle->timer) {
        err = mcpwm_del_timer(handle->timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Call to mcpwm_del_timer failed: %s", esp_err_to_name(err));
        }
    }

    free(handle);
    ESP_LOGI(TAG, "MCPWM peripheral deinitialized successfully");
    return 0;
}
