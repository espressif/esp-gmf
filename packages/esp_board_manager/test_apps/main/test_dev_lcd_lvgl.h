/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

esp_err_t test_dev_lcd_lvgl_init(void);

esp_err_t test_dev_lcd_touch_init(void);

esp_err_t test_dev_lcd_lvgl_deinit(void);

esp_err_t test_dev_lcd_touch_deinit(void);

/**
 * @brief  Show test menu interface
 *
 * @return
 */
esp_err_t test_dev_lcd_lvgl_show_menu(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
