/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once
#include "esp_board_periph.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "driver/i2s_tdm.h"
#include "driver/i2s_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  I2S peripheral configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         an I2S peripheral, including port settings, communication mode, direction,
 *         and specific configuration for different I2S modes (standard, PDM, TDM).
 */
typedef struct {
    i2s_port_t       port;       /*!< I2S controller port number */
    i2s_role_t       role;       /*!< I2S role (master/slave) */
    i2s_comm_mode_t  mode;       /*!< I2S communication mode (I2S_COMM_MODE_STD, I2S_COMM_MODE_PDM, I2S_COMM_MODE_TDM) */
    i2s_dir_t        direction;  /*!< I2S direction (I2S_DIR_RX: input BIT(0), I2S_DIR_TX: output BIT(1)) */
    union {
        i2s_std_config_t     std;     /*!< Standard I2S configuration */
#if CONFIG_SOC_I2S_SUPPORTS_TDM
        i2s_tdm_config_t     tdm;     /*!< TDM I2S configuration */
#endif // CONFIG_SOC_I2S_SUPPORTS_TDM
#if CONFIG_SOC_I2S_SUPPORTS_PDM_TX
        i2s_pdm_tx_config_t  pdm_tx;  /*!< PDM I2S configuration for output direction */
#endif // CONFIG_SOC_I2S_SUPPORTS_PDM_TX
#if CONFIG_SOC_I2S_SUPPORTS_PDM_RX
        i2s_pdm_rx_config_t  pdm_rx;  /*!< PDM I2S configuration for input direction */
#endif // CONFIG_SOC_I2S_SUPPORTS_PDM_RX
    } i2s_cfg;                        /*!< I2S configuration union */
} periph_i2s_config_t;

/**
 * @brief  Initialize the I2S peripheral
 *
 *         This function initializes an I2S peripheral using the provided configuration structure.
 *         It sets up the I2S controller with the specified mode, role, and direction.
 *
 * @param[in]   cfg            Pointer to periph_i2s_config_t configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the returned i2s_chan_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_i2s_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize the I2S peripheral
 *
 *         This function deinitializes the I2S peripheral and frees the allocated resources.
 *         It should be called when the peripheral is no longer needed.
 *
 * @param[in]  periph_handle  Handle returned by periph_i2s_init
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_i2s_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
