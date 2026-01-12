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

/* ADC Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_ADC_SUPPORT
#include "periph_adc.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_ADC_SUPPORT */

/* Analog Comparator Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_ANACMPR_SUPPORT
#include "periph_anacmpr.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_ANACMPR_SUPPORT */

/* DAC Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_DAC_SUPPORT
#include "periph_dac.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_DAC_SUPPORT */

/* MCPWM Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_MCPWM_SUPPORT
#include "periph_mcpwm.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_MCPWM_SUPPORT */

/* PCNT Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_PCNT_SUPPORT
#include "periph_pcnt.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_PCNT_SUPPORT */

/* RMT Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_RMT_SUPPORT
#include "periph_rmt.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_RMT_SUPPORT */

/* SDM Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_SDM_SUPPORT
#include "periph_sdm.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_SDM_SUPPORT */

/* UART Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_UART_SUPPORT
#include "periph_uart.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_UART_SUPPORT */

/* DSI Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_DSI_SUPPORT
#include "periph_dsi.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_DSI_SUPPORT */

/* LDO Peripheral */
#ifdef CONFIG_ESP_BOARD_PERIPH_LDO_SUPPORT
#include "periph_ldo.h"
#endif  /* CONFIG_ESP_BOARD_PERIPH_LDO_SUPPORT */

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

#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT
#include "dev_display_lcd.h"
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUPPORT */

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

#ifdef CONFIG_ESP_BOARD_DEV_BUTTON_SUPPORT
#include "dev_button.h"
#endif  /* CONFIG_ESP_BOARD_DEV_BUTTON_SUPPORT */
