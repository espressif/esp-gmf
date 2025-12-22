/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once
#include "driver/pulse_cnt.h"
#include "soc/soc_caps.h"
#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
    #warning "periph_pcnt is not supported in ESP-IDF v6.0.0 and above yet."
#endif

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  PCNT peripheral handle structure
 *
 *         This structure holds the handle for a PCNT unit and its associated channels.
 *         It is used to manage the PCNT peripheral instance after initialization.
 */
typedef struct {
    pcnt_unit_handle_t     pcnt_unit;                             /*!< Handle to the PCNT unit */
    pcnt_channel_handle_t  channels[SOC_PCNT_CHANNELS_PER_UNIT];  /*!< Array of handles for PCNT channels */
} periph_pcnt_handle_t;

/**
 * @brief  PCNT channel configuration structure
 *
 *         This structure contains the configuration parameters for a single PCNT channel,
 *         including channel config and edge/level action settings for counting behavior.
 */
typedef struct {
    pcnt_chan_config_t           channel_config;  /*!< Basic PCNT channel configuration */
    pcnt_channel_edge_action_t   pos_act;         /*!< Action on positive edge */
    pcnt_channel_edge_action_t   neg_act;         /*!< Action on negative edge */
    pcnt_channel_level_action_t  high_act;        /*!< Action when signal level is high */
    pcnt_channel_level_action_t  low_act;         /*!< Action when signal level is low */
} periph_pcnt_channel_config_t;

/**
 * @brief  PCNT unit configuration structure
 *
 *         This structure contains the complete configuration for a PCNT unit,
 *         including unit settings, glitch filter, channel configurations, and watch points.
 */
typedef struct {
    pcnt_unit_config_t            unit_config;                                          /*!< Basic PCNT unit configuration */
    pcnt_glitch_filter_config_t   filter_config;                                        /*!< Glitch filter configuration for noise suppression */
    uint8_t                       channel_count;                                        /*!< Number of channels to be created under this PCNT unit */
    periph_pcnt_channel_config_t  channel_list[SOC_PCNT_CHANNELS_PER_UNIT];             /*!< Array of channel configurations */
    uint8_t                       watch_point_count;                                    /*!< Number of watch points to be added */
    int                           watch_point_list[SOC_PCNT_THRES_POINT_PER_UNIT + 3];  /*!< Array of watch point values for event triggers */
} periph_pcnt_config_t;

/**
 * @brief  Initialize the PCNT peripheral
 *
 *         This function initializes a PCNT peripheral using the provided configuration structure.
 *         It sets up the PCNT unit, channels, and watch points as specified in the configuration.
 *
 * @param[in]   cfg            Pointer to periph_pcnt_config_t configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the returned periph_pcnt_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_pcnt_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize the PCNT peripheral
 *
 *         This function deinitializes the PCNT peripheral and frees the allocated resources.
 *
 * @param[in]  periph_handle  Handle returned by periph_pcnt_init
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_pcnt_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
