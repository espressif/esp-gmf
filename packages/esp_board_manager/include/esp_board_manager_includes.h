/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_board_manager.h"
#include "esp_board_device.h"
#include "esp_board_periph.h"
#include "esp_board_manager_defs.h"

/* ============================================================================
 * Peripheral Headers
 * ============================================================================ */

/* GPIO Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_GPIO_SUPPORT
#include "periph_gpio.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_GPIO_SUPPORT */

/* I2C Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_I2C_SUPPORT
#include "periph_i2c.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_I2C_SUPPORT */

/* SPI Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_SPI_SUPPORT
#include "periph_spi.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_SPI_SUPPORT */

/* I2S Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT
#include "periph_i2s.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_I2S_SUPPORT */

/* LEDC Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_LEDC_SUPPORT
#include "periph_ledc.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_LEDC_SUPPORT */

/* ============================================================================
 * Device Headers
 * ============================================================================ */

/* Audio Codec Device */
#ifdef CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT
#include "dev_audio_codec.h"
#endif  /* CONFIG_ESP_BOARD_DEV_AUDIO_CODEC_SUPPORT */

/* Camera Device */
#ifdef CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT
#include "dev_camera.h"
#endif  /* CONFIG_ESP_BOARD_DEV_CAMERA_SUPPORT */

/* Custom Device */
#ifdef CONFIG_ESP_BOARD_DEV_CUSTOM_SUPPORT
#include "dev_custom.h"
#endif  /* CONFIG_ESP_BOARD_DEV_CUSTOM_SUPPORT */

/* Display LCD SPI Device */
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT
#include "dev_display_lcd_spi.h"
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SPI_SUPPORT */

/* FATFS SD Card Device */
#ifdef CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT
#include "dev_fatfs_sdcard.h"
#endif  /* CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SUPPORT */

/* FATFS SD Card SPI Device */
#ifdef CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SPI_SUPPORT
#include "dev_fatfs_sdcard_spi.h"
#endif  /* CONFIG_ESP_BOARD_DEV_FATFS_SDCARD_SPI_SUPPORT */

/* SPIFFS Filesystem Device */
#ifdef CONFIG_ESP_BOARD_DEV_FS_SPIFFS_SUPPORT
#include "dev_fs_spiffs.h"
#endif  /* CONFIG_ESP_BOARD_DEV_FS_SPIFFS_SUPPORT */

/* GPIO Control Device */
#ifdef CONFIG_ESP_BOARD_DEV_GPIO_CTRL_SUPPORT
#include "dev_gpio_ctrl.h"
#endif  /* CONFIG_ESP_BOARD_DEV_GPIO_CTRL_SUPPORT */

/* GPIO Expander Device */
#ifdef CONFIG_ESP_BOARD_DEV_GPIO_EXPANDER_SUPPORT
#include "dev_gpio_expander.h"
#endif  /* CONFIG_ESP_BOARD_DEV_GPIO_EXPANDER_SUPPORT */

/* LCD Touch I2C Device */
#ifdef CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT
#include "dev_lcd_touch_i2c.h"
#endif  /* CONFIG_ESP_BOARD_DEV_LCD_TOUCH_I2C_SUPPORT */

/* LEDC Control Device */
#ifdef CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT
#include "dev_ledc_ctrl.h"
#endif  /* CONFIG_ESP_BOARD_DEV_LEDC_CTRL_SUPPORT */
