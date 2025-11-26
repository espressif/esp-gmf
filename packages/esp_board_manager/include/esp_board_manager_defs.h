/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Reserved device names
 *
 *         The device name identifies a device in user applications
 *
 *         The board manager defines the following device names for the corresponding devices
 *         You can use these names directly in user applications and in board_devices.yaml
 *
 *         You may also define your own device names in applications and in board_devices.yaml,
 *         but they must be unique and must not conflict with the reserved names
 */
#define ESP_BOARD_DEVICE_NAME_AUDIO_DAC      "audio_dac"          /*!< Audio DAC device base name */
#define ESP_BOARD_DEVICE_NAME_AUDIO_ADC      "audio_adc"          /*!< Audio ADC device base name */
#define ESP_BOARD_DEVICE_NAME_FS_SDCARD      "fs_sdcard"          /*!< SD card device base name */
#define ESP_BOARD_DEVICE_NAME_FS_FAT         "fs_fat"             /*!< FAT filesystem device base name */
#define ESP_BOARD_DEVICE_NAME_LCD_TOUCH      "lcd_touch"          /*!< LCD touch device base name */
#define ESP_BOARD_DEVICE_NAME_DISPLAY_LCD    "display_lcd"        /*!< LCD display device base name */
#define ESP_BOARD_DEVICE_NAME_LCD_POWER      "lcd_power"          /*!< LCD power control device base name */
#define ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS "lcd_brightness"     /*!< LCD brightness control device base name */
#define ESP_BOARD_DEVICE_NAME_FS_SPIFFS      "fs_spiffs"          /*!< SPIFFS filesystem device base name */
#define ESP_BOARD_DEVICE_NAME_GPIO_EXPANDER  "gpio_expander"      /*!< GPIO expander device base name */
#define ESP_BOARD_DEVICE_NAME_CAMERA         "camera_sensor"      /*!< Camera device base name */
#define ESP_BOARD_DEVICE_NAME_SD_POWER       "sdcard_power_ctrl"  /*!< SD card power control device base name */

/**
 * @brief  Device type keys
 *
 *         The type identifies the category of a device. Multiple devices can share the same type
 *         Format: lowercase letters, numbers, and underscores
 *         Must not be numbers only; must be unique within the configuration
 */
#define ESP_BOARD_DEVICE_TYPE_AUDIO_CODEC      "audio_codec"       /*!< Audio codec device type */
#define ESP_BOARD_DEVICE_TYPE_FATFS_SDCARD     "fatfs_sdcard"      /*!< FATFS SD card device type */
#define ESP_BOARD_DEVICE_TYPE_FATFS_SDCARD_SPI "fatfs_sdcard_spi"  /*!< FATFS SD card device type */
#define ESP_BOARD_DEVICE_TYPE_FS_FAT           "fs_fat"            /*!< FAT filesystem device type */
#define ESP_BOARD_DEVICE_TYPE_FS_SPIFFS        "fs_spiffs"         /*!< SPIFFS filesystem device type */
#define ESP_BOARD_DEVICE_TYPE_LCD_TOUCH_I2C    "lcd_touch_i2c"     /*!< LCD touch I2C device type */
#define ESP_BOARD_DEVICE_TYPE_DISPLAY_LCD_SPI  "display_lcd_spi"   /*!< LCD display SPI device type */
#define ESP_BOARD_DEVICE_TYPE_DISPLAY_LCD      "display_lcd"       /*!< LCD display SPI device type */
#define ESP_BOARD_DEVICE_TYPE_GPIO_CTRL        "gpio_ctrl"         /*!< GPIO control device type */
#define ESP_BOARD_DEVICE_TYPE_LEDC_CTRL        "ledc_ctrl"         /*!< LEDC control device type */
#define ESP_BOARD_DEVICE_TYPE_GPIO_EXPANDER    "gpio_expander"     /*!< GPIO expander device type */
#define ESP_BOARD_DEVICE_TYPE_CAMERA           "camera"            /*!< Camera sensor device type */

/**
 * @brief  Peripheral type keys
 *
 *         The type identifies the category of a peripheral. Multiple peripherals can share the same type
 *         Format: lowercase letters, numbers, and underscores
 *         Must not be numbers only; must be unique within the configuration
 */

#define ESP_BOARD_PERIPH_TYPE_I2C     "i2c"      /*!< I2C peripheral type */
#define ESP_BOARD_PERIPH_TYPE_I2S     "i2s"      /*!< I2S peripheral type */
#define ESP_BOARD_PERIPH_TYPE_SPI     "spi"      /*!< SPI peripheral type */
#define ESP_BOARD_PERIPH_TYPE_LEDC    "ledc"     /*!< LEDC peripheral type */
#define ESP_BOARD_PERIPH_TYPE_GPIO    "gpio"     /*!< GPIO peripheral type */
#define ESP_BOARD_PERIPH_TYPE_UART    "uart"     /*!< UART peripheral type */
#define ESP_BOARD_PERIPH_TYPE_ADC     "adc"      /*!< ADC peripheral type */
#define ESP_BOARD_PERIPH_TYPE_RMT     "rmt"      /*!< RMT peripheral type */
#define ESP_BOARD_PERIPH_TYPE_PCNT    "pcnt"     /*!< PCNT peripheral type */
#define ESP_BOARD_PERIPH_TYPE_ANACMPR "anacmpr"  /*!< Analog Comparator peripheral type */
#define ESP_BOARD_PERIPH_TYPE_DAC     "dac"      /*!< DAC peripheral type */
#define ESP_BOARD_PERIPH_TYPE_MCPWM   "mcpwm"    /*!< MCPWM peripheral type */
#define ESP_BOARD_PERIPH_TYPE_SDM     "sdm"      /*!< Sigma Delta Modulation peripheral type */

/**
 * @brief  Peripheral role keys
 *
 *         These define valid values for the peripheral role field
 *         The role describes the peripheral's function (for example: master/slave, host/device)
 */
#define ESP_BOARD_PERIPH_ROLE_MASTER         "master"      /*!< Master role */
#define ESP_BOARD_PERIPH_ROLE_SLAVE          "slave"       /*!< Slave role */
#define ESP_BOARD_PERIPH_ROLE_TX             "tx"          /*!< Transmitter role */
#define ESP_BOARD_PERIPH_ROLE_RX             "rx"          /*!< Receiver role */
#define ESP_BOARD_PERIPH_ROLE_ADC_CONTINUOUS "continuous"  /*!< ADC continuous role */
#define ESP_BOARD_PERIPH_ROLE_ADC_ONESHOT    "oneshot"     /*!< ADC oneshot role */
#define ESP_BOARD_PERIPH_ROLE_DAC_CONTINUOUS "continuous"  /*!< DAC continuous role */
#define ESP_BOARD_PERIPH_ROLE_DAC_ONESHOT    "oneshot"     /*!< DAC oneshot role */
#define ESP_BOARD_PERIPH_ROLE_DAC_COSINE     "cosine"      /*!< DAC cosine role */
#define ESP_BOARD_PERIPH_ROLE_NONE           "none"        /*!< No specific role */

/**
 * @brief  Peripheral format keys (I2S)
 *
 *         These define valid values for the I2S format field
 *         The format uses hyphen-separated values
 *         Examples: tdm-out, tdm-in, std-out, std-in, pdm-out, pdm-in
 */
#define ESP_BOARD_PERIPH_FORMAT_STD_OUT "std-out"  /*!< I2S standard output format */
#define ESP_BOARD_PERIPH_FORMAT_STD_IN  "std-in"   /*!< I2S standard input format */

#ifdef __cplusplus
}
#endif  /* __cplusplus */
