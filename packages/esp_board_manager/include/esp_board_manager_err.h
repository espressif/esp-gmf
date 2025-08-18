/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/* Board Manager Error Base */
#define ESP_BOARD_ERR_BASE                      (0x80000)
#define ESP_BOARD_ERR_MANAGER_BASE              (ESP_BOARD_ERR_BASE + 0x000)
#define ESP_BOARD_ERR_PERIPH_BASE               (ESP_BOARD_ERR_BASE + 0x100)
#define ESP_BOARD_ERR_DEVICE_BASE               (ESP_BOARD_ERR_BASE + 0x200)

/* Board Manager Error Codes */
#define ESP_BOARD_ERR_MANAGER_NOT_INIT          (ESP_BOARD_ERR_MANAGER_BASE - 0x00)  /*!< Manager not initialized */
#define ESP_BOARD_ERR_MANAGER_ALREADY_INIT      (ESP_BOARD_ERR_MANAGER_BASE - 0x01)  /*!< Manager already initialized */
#define ESP_BOARD_ERR_MANAGER_PERIPH_NOT_FOUND  (ESP_BOARD_ERR_MANAGER_BASE - 0x02)  /*!< Peripheral not found */
#define ESP_BOARD_ERR_MANAGER_DEVICE_NOT_FOUND  (ESP_BOARD_ERR_MANAGER_BASE - 0x03)  /*!< Device not found */
#define ESP_BOARD_ERR_MANAGER_INVALID_ARG       (ESP_BOARD_ERR_MANAGER_BASE - 0x04)  /*!< Invalid argument */

/* Peripheral Error Codes */
#define ESP_BOARD_ERR_PERIPH_NOT_FOUND          (ESP_BOARD_ERR_PERIPH_BASE - 0x00)  /*!< Peripheral not found */
#define ESP_BOARD_ERR_PERIPH_INVALID_ARG        (ESP_BOARD_ERR_PERIPH_BASE - 0x01)  /*!< Invalid argument */
#define ESP_BOARD_ERR_PERIPH_NO_HANDLE          (ESP_BOARD_ERR_PERIPH_BASE - 0x02)  /*!< No handle found */
#define ESP_BOARD_ERR_PERIPH_NO_INIT            (ESP_BOARD_ERR_PERIPH_BASE - 0x03)  /*!< No init function */
#define ESP_BOARD_ERR_PERIPH_INIT_FAILED        (ESP_BOARD_ERR_PERIPH_BASE - 0x04)  /*!< Init failed */
#define ESP_BOARD_ERR_PERIPH_DEINIT_FAILED      (ESP_BOARD_ERR_PERIPH_BASE - 0x05)  /*!< Deinit failed */

/* Device Error Codes */
#define ESP_BOARD_ERR_DEVICE_NOT_FOUND          (ESP_BOARD_ERR_DEVICE_BASE - 0x00)  /*!< Device not found */
#define ESP_BOARD_ERR_DEVICE_INVALID_ARG        (ESP_BOARD_ERR_DEVICE_BASE - 0x01)  /*!< Invalid argument */
#define ESP_BOARD_ERR_DEVICE_NO_HANDLE          (ESP_BOARD_ERR_DEVICE_BASE - 0x02)  /*!< No handle found */
#define ESP_BOARD_ERR_DEVICE_NO_INIT            (ESP_BOARD_ERR_DEVICE_BASE - 0x03)  /*!< No init function */
#define ESP_BOARD_ERR_DEVICE_INIT_FAILED        (ESP_BOARD_ERR_DEVICE_BASE - 0x04)  /*!< Init failed */
#define ESP_BOARD_ERR_DEVICE_DEINIT_FAILED      (ESP_BOARD_ERR_DEVICE_BASE - 0x05)  /*!< Deinit failed */

/* Error handling macros with ## operator for flexible usage */
#define ESP_BOARD_RETURN_ON_FALSE(condition, error_code, tag, ...) do {  \
    if (!(condition)) {                                              \
        ESP_LOGE(tag, "[%s] " __VA_ARGS__, __func__);                \
        return error_code;                                           \
    }                                                                \
} while (0)

#define ESP_BOARD_RETURN_ON_ERROR(operation, tag, ...) do {  \
    esp_err_t err_rc_ = (operation);                     \
    if (err_rc_ != ESP_OK) {                             \
        ESP_LOGE(tag, "[%s] " __VA_ARGS__, __func__);    \
        return err_rc_;                                  \
    }                                                    \
} while (0)

#define ESP_BOARD_RETURN_ON_NULL(ptr, error_code, tag, ...) \
    ESP_BOARD_RETURN_ON_FALSE(ptr, error_code, tag, ##__VA_ARGS__)

#define ESP_BOARD_RETURN_ON_INVALID_ARG(ptr, tag, ...) \
    ESP_BOARD_RETURN_ON_FALSE(ptr, ESP_BOARD_ERR_PERIPH_INVALID_ARG, tag, ##__VA_ARGS__)

/* Convenience macros for common error patterns */
#define ESP_BOARD_RETURN_ON_DEVICE_NOT_FOUND(desc, name, tag, ...) \
    ESP_BOARD_RETURN_ON_FALSE(desc, ESP_BOARD_ERR_DEVICE_NOT_FOUND, tag, ##__VA_ARGS__)

#define ESP_BOARD_RETURN_ON_PERIPH_NOT_FOUND(desc, name, tag, ...) \
    ESP_BOARD_RETURN_ON_FALSE(desc, ESP_BOARD_ERR_PERIPH_NOT_FOUND, tag, ##__VA_ARGS__)

/* Error code conversion functions */
/**
 * @brief  Convert board error code to string
 *
 * @param[in]  err_code  Error code
 *
 * @return
 */
const char *esp_board_manager_err_to_name(esp_err_t err_code);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
