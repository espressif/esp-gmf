/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_lcd_types.h"
#if CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT
#include "esp_lcd_mipi_dsi.h"
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT */
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_dev.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define ESP_BOARD_DEVICE_LCD_SUB_TYPE_DSI "dsi"  /*!< LCD display over DSI */
#define ESP_BOARD_DEVICE_LCD_SUB_TYPE_SPI "spi"  /*!< LCD display over SPI */

#if CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT
/**
 * @brief  DSI LCD display sub configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         an LCD display device over DSI, including chip, device type, panel configuration,
 *         DSI bus name, and panel IO configuration.
 */
typedef struct {
    const char                 *dsi_name;           /*!< DSI bus name */
    int                         reset_gpio_num;     /*!< Reset GPIO number */
    uint8_t                     reset_active_high;  /*!< Setting this if the panel reset is high level active */
    esp_lcd_dbi_io_config_t     dbi_config;         /*!< DBI configuration */
    esp_lcd_dpi_panel_config_t  dpi_config;         /*!< DPI configuration */
} dev_display_lcd_dsi_sub_config_t;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT */

#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_SPI_SUPPORT
/**
 * @brief  SPI LCD display sub configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         an LCD display device over SPI, including chip type, panel configuration,
 *         SPI name, and panel IO configuration.
 */
typedef struct {
    const char                    *spi_name;       /*!< SPI bus name */
    esp_lcd_panel_dev_config_t     panel_config;   /*!< LCD panel device configuration */
    esp_lcd_panel_io_spi_config_t  io_spi_config;  /*!< SPI panel IO configuration */
} dev_display_lcd_spi_sub_config_t;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_SPI_SUPPORT */

/**
 * @brief  LCD display device handles structure
 *
 *         This structure contains the handles for the LCD panel IO and panel device.
 */
typedef struct {
    esp_lcd_panel_io_handle_t  io_handle;     /*!< LCD panel IO handle */
    esp_lcd_panel_handle_t     panel_handle;  /*!< LCD panel device handle */
} dev_display_lcd_handles_t;

/**
 * @brief  LCD display configuration structure
 *
 *         This structure contains all the configuration parameters needed to initialize
 *         an LCD display device, including chip, device type, and bus-specific configuration.
 */
typedef struct {
    const char              *name;              /*!< Device name */
    const char              *chip;              /*!< LCD chip type */
    const char              *sub_type;          /*!< Sub type (dsi or spi) */
    uint16_t                 lcd_width;         /*!< LCD width */
    uint16_t                 lcd_height;        /*!< LCD height */
    uint8_t                  swap_xy      : 1;  /*!< Swap X and Y coordinates */
    uint8_t                  mirror_x     : 1;  /*!< Mirror X coordinates */
    uint8_t                  mirror_y     : 1;  /*!< Mirror Y coordinates */
    uint8_t                  need_reset   : 1;  /*!< Whether the panel needs reset during initialization */
    uint8_t                  invert_color : 1;  /*!< Invert color flag */
    lcd_rgb_element_order_t  rgb_ele_order;     /*!< RGB element order */
    lcd_rgb_data_endian_t    data_endian;       /*!< Set the data endian for color data larger than 1 byte */
    uint32_t                 bits_per_pixel;    /*!< Color depth */
    union {
#if CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT
        dev_display_lcd_dsi_sub_config_t  dsi;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_DSI_SUPPORT */
#ifdef CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_SPI_SUPPORT
        dev_display_lcd_spi_sub_config_t  spi;
#endif  /* CONFIG_ESP_BOARD_DEV_DISPLAY_LCD_SUB_SPI_SUPPORT */
    } sub_cfg;
} dev_display_lcd_config_t;

/**
 * @brief  Initialize the LCD display device with the given configuration
 *
 *         This function initializes an LCD display device using the provided configuration structure.
 *         It sets up the necessary hardware interfaces (DSI, SPI, GPIO, etc.) and allocates resources
 *         for the display. The resulting device handle can be used for further display operations.
 *
 * @param[in]   cfg            Pointer to the LCD display configuration structure
 * @param[in]   cfg_size       Size of the configuration structure
 * @param[out]  device_handle  Pointer to a variable to receive the dev_display_lcd_handles_t handle
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_display_lcd_init(void *cfg, int cfg_size, void **device_handle);

/**
 * @brief  Deinitialize the LCD display device and free related resources
 *
 *         It will call the sub-device deinitialize function and try to release bus resources too.
 *
 * @param[in]  device_handle  Pointer to the device handle to be deinitialized
 *
 * @return
 *       - 0               On success
 *       - Negative_value  On failure
 */
int dev_display_lcd_deinit(void *device_handle);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
