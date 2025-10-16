/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/**
 * @brief  Periph ADC test
 *
 *         This test example can be used to test the adc peripheral that has been initialized through the board manager.
 *         Connect a power source to the gpio corresponding to the adc channel you configured,
 *         and you can see the printed input voltage value in the terminal
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_board_periph.h"
#include "esp_board_manager_defs.h"
#include "periph_adc.h"

#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
#define EXAMPLE_ADC_OUTPUT_TYPE         ADC_DIGI_OUTPUT_FORMAT_TYPE1
#define EXAMPLE_ADC_GET_CHANNEL(p_data) ((p_data)->type1.channel)
#define EXAMPLE_ADC_GET_DATA(p_data)    ((p_data)->type1.data)
#else
#define EXAMPLE_ADC_OUTPUT_TYPE         ADC_DIGI_OUTPUT_FORMAT_TYPE2
#define EXAMPLE_ADC_GET_CHANNEL(p_data) ((p_data)->type2.channel)
#define EXAMPLE_ADC_GET_DATA(p_data)    ((p_data)->type2.data)
#endif  /* CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2 */

static const char *TAG = "TEST_ADC";

static esp_err_t continuous_test(adc_continuous_handle_t adc_handle, periph_adc_config_t *adc_cfg);
static esp_err_t oneshot_test(adc_oneshot_unit_handle_t adc_handle, periph_adc_config_t *adc_cfg);

void test_periph_adc(void)
{
    periph_adc_handle_t *handle = NULL;
    esp_err_t ret = esp_board_periph_get_handle("adc_handle", (void **)&handle);
    if (ret != ESP_OK || handle == NULL) {
        ESP_LOGE(TAG, "Failed to get ADC handle");
        return;
    }

    periph_adc_config_t *adc_cfg = {0};
    ret = esp_board_periph_get_config("adc_handle", (void *)&adc_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get ADC config %s", "lcd_brightness", esp_err_to_name(ret));
        return;
    }

    if (strcmp(adc_cfg->role, ESP_BOARD_PERIPH_ROLE_ADC_CONTINUOUS) == 0) {
        ret = continuous_test(handle->continuous, adc_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to run continuous test");
        }
    } else if (strcmp(adc_cfg->role, ESP_BOARD_PERIPH_ROLE_ADC_ONESHOT) == 0) {
        ret = oneshot_test(handle->oneshot, adc_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to run oneshot test");
        }
    } else {
        ESP_LOGE(TAG, "Invalid ADC role: %s", adc_cfg->role);
    }

    return;
}

esp_err_t continuous_test(adc_continuous_handle_t adc_handle, periph_adc_config_t *adc_cfg)
{
    adc_cali_handle_t adc_cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = adc_cfg->cfg.continuous.unit_id,
        .atten = adc_cfg->cfg.continuous.atten,
        .bitwidth = adc_cfg->cfg.continuous.bit_width,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Call to adc_cali_create_scheme_curve_fitting failed");
        return ret;
    }

    uint32_t read_len = adc_cfg->cfg.continuous.handle_cfg.conv_frame_size;
    uint32_t ret_num = 0;
    uint8_t result[read_len];
    memset(result, 0, read_len);
    int test_cnt = 10;

    adc_continuous_start(adc_handle);
    while (test_cnt) {
        ret = adc_continuous_read(adc_handle, result, read_len, &ret_num, 100 / portTICK_PERIOD_MS);
        if (ret == ESP_OK) {
            ESP_LOGI("TASK", "ret is %x, ret_num is %" PRIu32 " bytes", ret, ret_num);
            for (int i = 0; i < ret_num; i += SOC_ADC_DIGI_RESULT_BYTES) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t *)&result[i];
                uint32_t chan_num = EXAMPLE_ADC_GET_CHANNEL(p);
                uint32_t data = EXAMPLE_ADC_GET_DATA(p);
                uint8_t unit = adc_cfg->cfg.continuous.unit_id;
                /* Check the channel number validation, the data is invalid if the channel num exceed the maximum channel */
                if (chan_num < SOC_ADC_CHANNEL_NUM(unit)) {
                    int voltage = 0;
                    ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, data, &voltage));
                    ESP_LOGI(TAG, "Unit: ADC_UNIT_%d, Channel: %" PRIu32 ", Value: %" PRIx32 ", Cali Voltage: %d mV", unit, chan_num, data, voltage);
                } else {
                    ESP_LOGW(TAG, "Invalid data [%s_%" PRIu32 "_%" PRIx32 "]", unit, chan_num, data);
                }
            }
        } else {
            ESP_LOGE(TAG, "Call to adc_continuous_read failed: %s", esp_err_to_name(ret));
            return ret;
        }
        test_cnt--;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    adc_continuous_stop(adc_handle);
    return ESP_OK;
}

esp_err_t oneshot_test(adc_oneshot_unit_handle_t adc_handle, periph_adc_config_t *adc_cfg)
{
    adc_cali_handle_t adc_cali_handle = NULL;
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = adc_cfg->cfg.oneshot.unit_cfg.unit_id,
        .atten = adc_cfg->cfg.oneshot.chan_cfg.atten,
        .bitwidth = adc_cfg->cfg.oneshot.chan_cfg.bitwidth,
    };
    esp_err_t ret = adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Call to adc_cali_create_scheme_curve_fitting failed");
        return ret;
    }

    int result = 0;
    int test_cnt = 10;

    while (test_cnt) {
        ret = adc_oneshot_read(adc_handle, adc_cfg->cfg.oneshot.channel_id, &result);
        if (ret == ESP_OK) {
            int voltage = 0;
            ESP_ERROR_CHECK(adc_cali_raw_to_voltage(adc_cali_handle, result, &voltage));
            ESP_LOGI(TAG, "Unit: ADC_UNIT_%d, Channel: %" PRIu32 ", Value: %" PRIx32 ", Cali Voltage: %d mV",
                     adc_cfg->cfg.oneshot.unit_cfg.unit_id,
                     adc_cfg->cfg.oneshot.channel_id,
                     result,
                     voltage);
        } else {
            ESP_LOGE(TAG, "Call to adc_oneshot_read failed: %s", esp_err_to_name(ret));
            return ret;
        }
        test_cnt--;
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }

    return ESP_OK;
}
