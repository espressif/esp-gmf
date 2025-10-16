/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "soc/gpio_num.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  UART peripheral handle structure
 *
 *         This structure contains the handle for a UART peripheral,
 *         including the UART port number and the associated queue handle.
 */
typedef struct {
    uart_port_t    uart_num;    /*!< UART port number */
    QueueHandle_t  uart_queue;  /*!< Queue handle for UART events if set use_queue */
} periph_uart_handle_t;

/**
 * @brief  UART configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         a UART peripheral, including buffer sizes, GPIO pins, and UART settings.
 */
typedef struct {
    uart_port_t    uart_num;          /*!< UART port number */
    int            rx_buffer_size;    /*!< Size of RX buffer in bytes */
    int            tx_buffer_size;    /*!< Size of TX buffer in bytes */
    gpio_num_t     tx_io_num;         /*!< GPIO number for TX pin */
    gpio_num_t     rx_io_num;         /*!< GPIO number for RX pin */
    gpio_num_t     rts_io_num;        /*!< GPIO number for RTS pin */
    gpio_num_t     cts_io_num;        /*!< GPIO number for CTS pin */
    uart_mode_t    uart_mode;         /*!< UART mode (e.g., UART_MODE_UART) */
    uint8_t        use_queue : 1;     /*!< Flag to enable queue usage */
    int            queue_size;        /*!< Size of the queue if used */
    int            intr_alloc_flags;  /*!< Interrupt allocation flags */
    uart_config_t  uart_config;       /*!< UART configuration (baud rate, data bits, etc.) */
} periph_uart_config_t;

/**
 * @brief  Initialize a UART peripheral
 *
 *         This function initializes a UART peripheral using the provided configuration structure.
 *         It sets up the UART hardware with the specified parameters, including buffers and GPIOs.
 *
 * @param[in]   cfg            Pointer to periph_uart_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  periph_handle  Pointer to store the periph_uart_handle_t handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_uart_init(void *cfg, int cfg_size, void **periph_handle);

/**
 * @brief  Deinitialize a UART peripheral
 *
 *         This function deinitializes the UART peripheral and frees the allocated resources.
 *         It should be called when the peripheral is no longer needed.
 *
 * @param[in]  periph_handle  UART peripheral handle
 *
 * @return
 *       - 0   On success
 *       - -1  On error
 */
int periph_uart_deinit(void *periph_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
