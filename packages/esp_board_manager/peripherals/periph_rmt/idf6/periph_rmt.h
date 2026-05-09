/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once
#include <stdint.h>
#include "driver/rmt_tx.h"
#include "driver/rmt_rx.h"
#include "esp_idf_version.h"
#include "esp_board_manager_defs.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  RMT V2 peripheral handle structure
 *
 *         This structure holds the handle for an RMT channel.
 *         It is used to manage the RMT V2 peripheral instance after initialization.
 */
typedef struct {
    rmt_channel_handle_t  channel;  /*!< Generic RMT channel handle */
} periph_rmt_handle_t;

/**
 * @brief  RMT V2 extra flags for GPIO configuration
 */
typedef struct {
    uint32_t  io_od_mode : 1;  /*!< Enable open drain mode (TX only) */
} periph_rmt_extra_flags_t;

/**
 * @brief  RMT V2 peripheral configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         an RMT peripheral, including role selection, common channel settings,
 *         and mode-specific configurations for transmit or receive modes.
 */
typedef struct {
    esp_board_periph_role_t  role;  /*!< Channel role: ESP_BOARD_PERIPH_ROLE_TX or ESP_BOARD_PERIPH_ROLE_RX */
    union {
        rmt_tx_channel_config_t  tx;  /*!< TX channel configuration */
        rmt_rx_channel_config_t  rx;  /*!< RX channel configuration */
    } channel_config;
    periph_rmt_extra_flags_t  extra_flags;  /*!< Extra flags for GPIO configuration */
} periph_rmt_config_t;

/**
 * @brief  Initialize the RMT V2 peripheral
 *
 *         This function initializes an RMT peripheral using the provided configuration structure.
 *         It sets up the RMT channel with the specified role, GPIO, clock source, and mode-specific settings.
 *
 * @param[in]   cfg            Pointer to periph_rmt_config_t configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the returned periph_rmt_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_rmt_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize the RMT V2 peripheral
 *
 *         This function deinitializes the RMT peripheral and frees the allocated resources.
 *
 * @param[in]  periph_handle  Handle returned by periph_rmt_init
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_rmt_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
