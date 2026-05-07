/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef enum {
    ESP_FAIL       = -1,
    ESP_OK         = 0,
    ESP_ERR_NO_MEM = 0x101,  /*!< Out of memory */
    ESP_ERR_INVALID_ARG,
    ESP_ERR_INVALID_STATE,     /*!< Invalid state */
    ESP_ERR_INVALID_SIZE,      /*!< Invalid size */
    ESP_ERR_NOT_FOUND,         /*!< Requested resource not found */
    ESP_ERR_NOT_SUPPORTED,     /*!< Operation or feature not supported */
    ESP_ERR_TIMEOUT,           /*!< Operation timed out */
    ESP_ERR_INVALID_RESPONSE,  /*!< Received response was invalid */
    ESP_ERR_INVALID_CRC,       /*!< CRC or checksum was invalid */
    ESP_ERR_INVALID_VERSION,   /*!< Version was invalid */
} esp_err_t;

#define ESP_ERROR_CHECK(stat)  do {                      \
    if ((stat) != 0) {                                   \
        printf("Error at %s:%d\n", __FILE__, __LINE__);  \
    }                                                    \
} while (0);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
