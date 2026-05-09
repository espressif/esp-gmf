/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ESP_BOARD_DEVICE_LCD_TOUCH_SUB_TYPE_I2C  "i2c"
#define ESP_BOARD_DEVICE_LCD_TOUCH_SUB_TYPE_SPI  "spi"

#define DEV_LCD_TOUCH_I2C_MAX_ADDR_COUNT  4

#if defined(CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT) || defined(ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT)
#ifndef ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT
#define ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT
#endif  /* ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT */
#endif  /* defined(CONFIG_ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT) || defined(ESP_BOARD_DEV_LCD_TOUCH_SUB_I2C_SUPPORT) */

/**
 * @brief  LCD touch I2C sub-type configuration
 *
 *         This structure contains the board-manager fields needed to bind a touch
 *         device to an I2C bus, plus the native ESP-IDF panel IO I2C config.
 *         I2C addresses use the board-manager 8-bit / left-shifted format.
 */
typedef struct {
    const char                    *i2c_name;                                    /*!< I2C bus name used by this touch device */
    size_t                         i2c_addr_count;                              /*!< Number of valid entries in i2c_addr[] */
    const uint16_t                 i2c_addr[DEV_LCD_TOUCH_I2C_MAX_ADDR_COUNT];  /*!< Candidate 8-bit I2C addresses; unused entries are 0 */
    esp_lcd_panel_io_i2c_config_t  io_i2c_config;                               /*!< I2C panel IO configuration; dev_addr is set after probe */
} dev_lcd_touch_i2c_sub_config_t;

/**
 * @brief  LCD touch SPI sub-type configuration
 *
 *         SPI touch support is reserved for a later implementation. The structure
 *         is kept here so the generic dev_lcd_touch shape can support both bus
 *         families without changing the public config type later.
 */
typedef struct {
    const char                    *spi_name;       /*!< SPI bus name used by this touch device */
    esp_lcd_panel_io_spi_config_t  io_spi_config;  /*!< SPI panel IO configuration, reserved for future SPI touch support */
} dev_lcd_touch_spi_sub_config_t;

/**
 * @brief  LCD touch device handles
 *
 *         This structure contains the runtime handles returned by ESP-IDF LCD
 *         touch and panel IO drivers.
 */
typedef struct {
    esp_lcd_touch_handle_t     touch_handle;  /*!< LCD touch driver handle */
    esp_lcd_panel_io_handle_t  io_handle;     /*!< LCD panel IO handle */
} dev_lcd_touch_handles_t;

/**
 * @brief  Generic LCD touch configuration structure
 *
 *         This structure describes an LCD touch device independent of bus type.
 *         Common touch fields reuse esp_lcd_touch_config_t directly, and bus
 *         specific fields are stored under sub_cfg according to sub_type.
 */
typedef struct {
    const char             *name;          /*!< Device name */
    const char             *chip;          /*!< Touch chip type */
    const char             *type;          /*!< Device type, must be "lcd_touch" */
    const char             *sub_type;      /*!< Touch bus sub-type, for example "i2c" */
    esp_lcd_touch_config_t  touch_config;  /*!< Common LCD touch configuration */
    union {
        dev_lcd_touch_i2c_sub_config_t  i2c;  /*!< I2C sub-type configuration */
        dev_lcd_touch_spi_sub_config_t  spi;  /*!< SPI sub-type configuration, reserved for future use */
    } sub_cfg;
} dev_lcd_touch_config_t;

/**
 * @brief  Initialize an LCD touch device
 *
 *         This function dispatches initialization to the implementation selected
 *         by dev_lcd_touch_config_t::sub_type.
 *
 * @param[in]   cfg            Pointer to dev_lcd_touch_config_t
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  device_handle  Pointer to the returned dev_lcd_touch_handles_t
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_lcd_touch_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize an LCD touch device
 *
 *         This function dispatches deinitialization to the implementation selected
 *         by the device configuration.
 *
 * @param[in]  device_handle  Pointer to dev_lcd_touch_handles_t
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_lcd_touch_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
