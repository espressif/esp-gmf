/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_lcd_types.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/* Display resolution */
#define BSP_LCD_H_RES  (240)
#define BSP_LCD_V_RES  (240)

/* Display */
#define BSP_LCD_DATA0  (38)
#define BSP_LCD_PCLK   (45)
#define BSP_LCD_DC     (47)
#define BSP_LCD_RST    (21)
#define BSP_LCD_CS_0   (41)
#define BSP_LCD_CS_1   (48)
#define BSP_LCD_BL     (39)
#define BSP_LCD_POWER  (-1)  // Control power(VCC) for the LCD, if not used, set to GPIO_NUM_NC

#define TRANS_SIZE  (BSP_LCD_H_RES * BSP_LCD_V_RES / 10)

/* LCD display color format */
#define BSP_LCD_COLOR_FORMAT    (ESP_LCD_COLOR_FORMAT_RGB565)
/* LCD display color bytes endianness */
#define BSP_LCD_BIGENDIAN       (1)
// Bit number used to represent command and parameter
#define LCD_CMD_BITS            (8)
#define LCD_PARAM_BITS          (8)
#define LCD_LEDC_CH             (LEDC_CHANNEL_0)
#define LCD_LEDC_SPEED_MODE     (LEDC_LOW_SPEED_MODE)
// SPI bus parameter
#define BSP_LCD_PIXEL_CLOCK_HZ  (80 * 1000 * 1000)  // Need to check if your display supports this frequency
#define BSP_LCD_SPI_NUM         (SPI2_HOST)
#define SPI_ISR_CPU_ID          (ESP_INTR_CPU_AFFINITY_TO_CORE_ID(ESP_INTR_CPU_AFFINITY_1))

#define BSP_SDCARD_CLK  (2)
#define BSP_SDCARD_CMD  (42)
#define BSP_SDCARD_D0   (1)
#define BSP_SDCARD_D1   (-1)
#define BSP_SDCARD_D2   (-1)
#define BSP_SDCARD_D3   (-1)

typedef struct {
    esp_lcd_panel_handle_t     lcd_handle;
    esp_lcd_panel_io_handle_t  io_handle;
} dual_display_t;

/**
 * @brief  Initialize the dual LCD display driver
 *
 * @param[out]  disp  Display and IO handle to store
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid argument is wrong
 */
esp_err_t dual_display_init(dual_display_t *out_disp);

/**
 * @brief  Set display's brightness
 *
 * @note  Brightness must be already initialized by `dual_display_init()`
 *
 * @param[in]  percent  Brightness in [%]
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Parameter error
 */
esp_err_t dual_display_brightness_set(int percent);

/**
 * @brief  Deinitialize the dual LCD display driver
 *
 * @note  It will deinitialize and free all resources used by the driver (including brightness control)
 */
void dual_display_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
