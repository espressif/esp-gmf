/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once
#include "esp_board_periph.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  SPI peripheral handle
 *
 *         This structure represents a SPI port number
 */
typedef struct {
    spi_host_device_t  spi_port;  /*!< SPI port number */
} periph_spi_handle_t;

/**
 * @brief  SPI peripheral configuration structure
 *
 *         This structure contains the configuration parameters needed to initialize
 *         an SPI peripheral, including the SPI port and bus configuration.
 */
typedef struct {
    spi_host_device_t  spi_port;        /*!< SPI port number */
    spi_bus_config_t   spi_bus_config;  /*!< SPI bus configuration */
} periph_spi_config_t;

/**
 * @brief  Initialize the SPI peripheral
 *
 *         This function initializes an SPI peripheral using the provided configuration structure.
 *         It sets up the SPI bus with the specified configuration.
 *
 * @param[in]   cfg            Pointer to periph_spi_config_t configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the returned periph_spi_handle_t  handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_spi_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize the SPI peripheral
 *
 *         This function deinitializes the SPI peripheral and frees the allocated resources.
 *         It should be called when the peripheral is no longer needed.
 *
 * @param[in]  periph_handle  Handle returned by periph_spi_init
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_spi_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
