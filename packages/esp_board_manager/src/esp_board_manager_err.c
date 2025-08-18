/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdbool.h>
#include "esp_log.h"
#include "esp_board_manager_err.h"

/* Error code to string mapping */
static const struct {
    esp_err_t   code;
    const char *name;
} board_error_map[] = {
    /* Board Manager Errors */
    {ESP_BOARD_ERR_MANAGER_NOT_INIT,         "ESP_BOARD_ERR_MANAGER_NOT_INIT"},
    {ESP_BOARD_ERR_MANAGER_ALREADY_INIT,     "ESP_BOARD_ERR_MANAGER_ALREADY_INIT"},
    {ESP_BOARD_ERR_MANAGER_PERIPH_NOT_FOUND, "ESP_BOARD_ERR_MANAGER_PERIPH_NOT_FOUND"},
    {ESP_BOARD_ERR_MANAGER_DEVICE_NOT_FOUND, "ESP_BOARD_ERR_MANAGER_DEVICE_NOT_FOUND"},

    /* Peripheral Errors */
    {ESP_BOARD_ERR_PERIPH_NOT_FOUND,         "ESP_BOARD_ERR_PERIPH_NOT_FOUND"},
    {ESP_BOARD_ERR_PERIPH_INVALID_ARG,       "ESP_BOARD_ERR_PERIPH_INVALID_ARG"},
    {ESP_BOARD_ERR_PERIPH_NO_HANDLE,         "ESP_BOARD_ERR_PERIPH_NO_HANDLE"},
    {ESP_BOARD_ERR_PERIPH_NO_INIT,           "ESP_BOARD_ERR_PERIPH_NO_INIT"},
    {ESP_BOARD_ERR_PERIPH_INIT_FAILED,       "ESP_BOARD_ERR_PERIPH_INIT_FAILED"},
    {ESP_BOARD_ERR_PERIPH_DEINIT_FAILED,     "ESP_BOARD_ERR_PERIPH_DEINIT_FAILED"},

    /* Device Errors */
    {ESP_BOARD_ERR_DEVICE_NOT_FOUND,         "ESP_BOARD_ERR_DEVICE_NOT_FOUND"},
    {ESP_BOARD_ERR_DEVICE_INVALID_ARG,       "ESP_BOARD_ERR_DEVICE_INVALID_ARG"},
    {ESP_BOARD_ERR_DEVICE_NO_HANDLE,         "ESP_BOARD_ERR_DEVICE_NO_HANDLE"},
    {ESP_BOARD_ERR_DEVICE_NO_INIT,           "ESP_BOARD_ERR_DEVICE_NO_INIT"},
    {ESP_BOARD_ERR_DEVICE_INIT_FAILED,       "ESP_BOARD_ERR_DEVICE_INIT_FAILED"},
    {ESP_BOARD_ERR_DEVICE_DEINIT_FAILED,     "ESP_BOARD_ERR_DEVICE_DEINIT_FAILED"},
};

const char *esp_board_manager_err_to_name(esp_err_t err_code)
{
    if (err_code < ESP_BOARD_ERR_BASE) {
        return "UNKNOWN_BOARD_ERROR";
    }
    for (size_t i = 0; i < sizeof(board_error_map) / sizeof(board_error_map[0]); i++) {
        if (board_error_map[i].code == err_code) {
            return board_error_map[i].name;
        }
    }
    return "UNKNOWN_BOARD_ERROR";
}
