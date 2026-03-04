/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_board_manager_defs.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  ADC handle structure
 *
 *         This structure stores the underlying ADC driver handle.
 * @note   For continuous mode the `continuous` member is used, for one-shot mode the `oneshot` member is used.
 */
typedef union {
    adc_continuous_handle_t    continuous;  /*!< Continuous-mode handle */
    adc_oneshot_unit_handle_t  oneshot;     /*!< One-shot unit handle */
} periph_adc_handle_t;

typedef enum {
    PERIPH_ADC_CONTINUOUS_CFG_MODE_SINGLE_UNIT = 0,  /*!< Single unit + channel list */
    PERIPH_ADC_CONTINUOUS_CFG_MODE_PATTERN     = 1,  /*!< Full adc_digi_pattern_config_t list */
} periph_adc_continuous_cfg_mode_t;

/**
 * @brief  Continuous-mode configuration
 *
 *         This structure contains all the configuration needed to
 *         initialize continuous-mode ADC operation, including handle config,
 *         sampling frequency, conversion mode, output format and pattern list.
 *         It supports two internal modes:
 *         - single_unit: unit/atten/bit_width + channel_id[] (legacy-compatible)
 *         - pattern: full adc_digi_pattern_config_t[] (supports mixed unit patterns)
 */
typedef struct {
    adc_continuous_handle_cfg_t       handle_cfg;      /*!< Continuous-mode handle config */
    uint32_t                          sample_freq_hz;  /*!< Sampling frequency in Hz. */
    adc_digi_convert_mode_t           conv_mode;       /*!< ADC DMA conversion mode. */
    adc_digi_output_format_t          format;          /*!< ADC DMA output format. */
    uint32_t                          pattern_num;     /*!< Number of valid pattern entries */
    periph_adc_continuous_cfg_mode_t  cfg_mode;        /*!< Continuous config mode */
    union {
        struct {
            uint8_t  unit_id;                           /*!< ADC unit id (1 or 2) */
            uint8_t  atten;                             /*!< ADC attenuation */
            uint8_t  bit_width;                         /*!< ADC bit width */
            uint8_t  channel_id[SOC_ADC_PATT_LEN_MAX];  /*!< Channel id list */
        } single_unit;
        adc_digi_pattern_config_t  patterns[SOC_ADC_PATT_LEN_MAX];  /*!< Full pattern list */
    } cfg;
} periph_adc_continuous_cfg_t;

/**
 * @brief  One-shot mode specific configuration
 *
 *         This structure contains all the specific configuration needed to
 *         initialize one-shot ADC operation, including channel ID, unit config, and channel config.
 */
typedef struct {
    adc_channel_t                channel_id;  /*!< ADC channel id used for one-shot reads. */
    adc_oneshot_unit_init_cfg_t  unit_cfg;    /*!< ADC unit initialization config */
    adc_oneshot_chan_cfg_t       chan_cfg;    /*!< ADC channel initialization config */
} periph_adc_oneshot_cfg_t;

/**
 * @brief  ADC peripheral configuration structure
 *         This structure contains all the configuration needed to initialize
 *         ADC peripheral, including continuous-mode and oneshot-mode configurations.
 */
typedef struct {
    esp_board_periph_role_t  role;  /*!< Periph role: esp_board_periph_role_t */
    union {
        periph_adc_continuous_cfg_t  continuous;  /*!< Continuous-mode specific config */
        periph_adc_oneshot_cfg_t     oneshot;     /*!< One-shot mode specific config */
    } cfg;
} periph_adc_config_t;

/**
 * @brief  Initialize ADC peripheral
 *
 *         This function initializes a ADC driver instance using the provided configuration structure.
 *         It sets up the ADC hardware with the specified parameters.
 *
 * @param[in]   cfg            Pointer to periph_adc_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the periph_adc_handle_t
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_adc_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize ADC peripheral
 *
 *         This function deinitializes the ADC peripheral and frees the allocated resources.
 *
 *
 * @param[in]  periph_handle  ADC peripheral handle
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_adc_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
