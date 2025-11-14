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
#define ESP_BOARD_DEVICE_NAME_AUDIO_DAC        "audio_dac"          /*!< Audio DAC device base name */
#define ESP_BOARD_DEVICE_NAME_AUDIO_ADC        "audio_adc"          /*!< Audio ADC device base name */
#define ESP_BOARD_DEVICE_NAME_FS_SDCARD        "fs_sdcard"          /*!< SD card device base name */
#define ESP_BOARD_DEVICE_NAME_FS_FAT           "fs_fat"             /*!< FAT filesystem device base name */
#define ESP_BOARD_DEVICE_NAME_LCD_TOUCH        "lcd_touch"          /*!< LCD touch device base name */
#define ESP_BOARD_DEVICE_NAME_DISPLAY_LCD      "display_lcd"        /*!< LCD display device base name */
#define ESP_BOARD_DEVICE_NAME_LCD_POWER        "lcd_power"          /*!< LCD power control device base name */
#define ESP_BOARD_DEVICE_NAME_LCD_BRIGHTNESS   "lcd_brightness"     /*!< LCD brightness control device base name */
#define ESP_BOARD_DEVICE_NAME_FS_SPIFFS        "fs_spiffs"          /*!< SPIFFS filesystem device base name */
#define ESP_BOARD_DEVICE_NAME_GPIO_EXPANDER    "gpio_expander"      /*!< GPIO expander device base name */
#define ESP_BOARD_DEVICE_NAME_CAMERA           "camera_sensor"      /*!< Camera device base name */
#define ESP_BOARD_DEVICE_NAME_SD_POWER         "sdcard_power_ctrl"  /*!< SD card power control device base name */
#define ESP_BOARD_DEVICE_NAME_ADC_BUTTON_0     "adc_button_0"       /*!< ADC button device base name */
#define ESP_BOARD_DEVICE_NAME_ADC_BUTTON_1     "adc_button_1"       /*!< ADC button device base name */
#define ESP_BOARD_DEVICE_NAME_ADC_BUTTON_2     "adc_button_2"       /*!< ADC button device base name */
#define ESP_BOARD_DEVICE_NAME_ADC_BUTTON_3     "adc_button_3"       /*!< ADC button device base name */
#define ESP_BOARD_DEVICE_NAME_ADC_BUTTON_4     "adc_button_4"       /*!< ADC button device base name */
#define ESP_BOARD_DEVICE_NAME_ADC_BUTTON_5     "adc_button_5"       /*!< ADC button device base name */
#define ESP_BOARD_DEVICE_NAME_ADC_BUTTON_GROUP "adc_button_group"   /*!< ADC button device base name */

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
#define ESP_BOARD_DEVICE_TYPE_POWER_CTRL       "power_ctrl"        /*!< Power control device type */
#define ESP_BOARD_DEVICE_TYPE_BUTTON           "button"            /*!< Button device type */

/**
 * @brief  Peripheral role enum
 *
 *         These define valid values for the peripheral role field
 *         The role describes the peripheral's function (for example: master/slave, host/device)
 */
typedef enum {
    ESP_BOARD_PERIPH_ROLE_NONE       = 0,  /*!< No specific role */
    ESP_BOARD_PERIPH_ROLE_MASTER     = 1,  /*!< Master role */
    ESP_BOARD_PERIPH_ROLE_SLAVE      = 2,  /*!< Slave role */
    ESP_BOARD_PERIPH_ROLE_IO         = 3,  /*!< IO role */
    ESP_BOARD_PERIPH_ROLE_TX         = 4,  /*!< Transmitter role */
    ESP_BOARD_PERIPH_ROLE_RX         = 5,  /*!< Receiver role */
    ESP_BOARD_PERIPH_ROLE_CONTINUOUS = 6,  /*!< Continuous role */
    ESP_BOARD_PERIPH_ROLE_ONESHOT    = 7,  /*!< Oneshot role */
    ESP_BOARD_PERIPH_ROLE_COSINE     = 8,  /*!< Cosine role */
} esp_board_periph_role_t;

/**
 * @brief  Peripheral role names map
 *
 *         This array maps peripheral role enum values to their corresponding names
 */
static const char *role_names_map[] __attribute__((unused)) = {
    [ESP_BOARD_PERIPH_ROLE_NONE]       = "NONE",
    [ESP_BOARD_PERIPH_ROLE_MASTER]     = "MASTER",
    [ESP_BOARD_PERIPH_ROLE_SLAVE]      = "SLAVE",
    [ESP_BOARD_PERIPH_ROLE_IO]         = "IO",
    [ESP_BOARD_PERIPH_ROLE_TX]         = "TX",
    [ESP_BOARD_PERIPH_ROLE_RX]         = "RX",
    [ESP_BOARD_PERIPH_ROLE_CONTINUOUS] = "CONTINUOUS",
    [ESP_BOARD_PERIPH_ROLE_ONESHOT]    = "ONESHOT",
    [ESP_BOARD_PERIPH_ROLE_COSINE]     = "COSINE",
};

/**
 * @brief  Peripheral format keys (I2S)
 *
 *         These define valid values for the I2S format field
 *         The format uses hyphen-separated values
 *         Examples: tdm-out, tdm-in, std-out, std-in, pdm-out, pdm-in
 */
#define ESP_BOARD_PERIPH_FORMAT_STD_OUT "std-out"  /*!< I2S standard output format */
#define ESP_BOARD_PERIPH_FORMAT_STD_IN  "std-in"   /*!< I2S standard input format */

/**
 * @brief  Peripheral name keys
 *
 *         These define commonly used peripheral names across different boards
 *         Using these macros ensures consistency and avoids typos
 */
#define ESP_BOARD_PERIPH_NAME_I2C_MASTER             "i2c_master"              /*!< I2C master peripheral name */
#define ESP_BOARD_PERIPH_NAME_I2S_AUDIO_OUT          "i2s_audio_out"           /*!< I2S audio output peripheral name */
#define ESP_BOARD_PERIPH_NAME_I2S_AUDIO_IN           "i2s_audio_in"            /*!< I2S audio input peripheral name */
#define ESP_BOARD_PERIPH_NAME_SPI_DISPLAY            "spi_display"             /*!< SPI display peripheral name */
#define ESP_BOARD_PERIPH_NAME_SPI_MASTER             "spi_master"              /*!< SPI master peripheral name */
#define ESP_BOARD_PERIPH_NAME_LEDC_BACKLIGHT         "ledc_backlight"          /*!< LEDC backlight peripheral name */
#define ESP_BOARD_PERIPH_NAME_GPIO_PA_CONTROL        "gpio_pa_control"         /*!< GPIO power amplifier control peripheral name */
#define ESP_BOARD_PERIPH_NAME_GPIO_BACKLIGHT_CONTROL "gpio_backlight_control"  /*!< GPIO backlight control peripheral name */
#define ESP_BOARD_PERIPH_NAME_GPIO_SD_POWER          "gpio_sd_power"           /*!< GPIO SD card power control peripheral name */
#define ESP_BOARD_PERIPH_NAME_GPIO_LCD_RESET         "gpio_lcd_reset"          /*!< GPIO LCD reset peripheral name */
#define ESP_BOARD_PERIPH_NAME_GPIO_POWER_LCD         "gpio_power_lcd"          /*!< GPIO LCD power peripheral name */
#define ESP_BOARD_PERIPH_NAME_GPIO_MONITOR           "gpio_monitor"            /*!< GPIO monitor peripheral name */
#define ESP_BOARD_PERIPH_NAME_ADC                    "adc_unit_1"              /*!< ADC unit 1 peripheral name */
#define ESP_BOARD_PERIPH_NAME_DAC                    "dac_channel_0"           /*!< DAC channel 0 peripheral name */
#define ESP_BOARD_PERIPH_NAME_RMT_TX                 "rmt_tx"                  /*!< RMT transmitter peripheral name */
#define ESP_BOARD_PERIPH_NAME_RMT_RX                 "rmt_rx"                  /*!< RMT receiver peripheral name */
#define ESP_BOARD_PERIPH_NAME_PCNT_UNIT              "pcnt_unit"               /*!< PCNT unit peripheral name */
#define ESP_BOARD_PERIPH_NAME_MCPWM                  "mcpwm_group_0"           /*!< MCPWM group 0 peripheral name */
#define ESP_BOARD_PERIPH_NAME_UART_1                 "uart_1"                  /*!< UART 1 peripheral name */
#define ESP_BOARD_PERIPH_NAME_SDM                    "sdm"                     /*!< SDM peripheral name */
#define ESP_BOARD_PERIPH_NAME_ANACMPR                "anacmpr_unit_0"          /*!< Analog Comparator peripheral name */
#define ESP_BOARD_PERIPH_NAME_LDO_MIPI               "ldo_mipi"                /*!< LDO for MIPI peripheral name */
#define ESP_BOARD_PERIPH_NAME_DSI_DISPLAY            "dsi_display"             /*!< DSI display peripheral name */

#ifdef __cplusplus
}
#endif  /* __cplusplus */
