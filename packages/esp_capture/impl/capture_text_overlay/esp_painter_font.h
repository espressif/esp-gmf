/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef struct {
    const uint8_t *bitmap;
    uint16_t       width;
    uint16_t       height;
} esp_painter_basic_font_t;

#define ESP_PAINTER_FONT_DECLARE(font_name) extern const esp_painter_basic_font_t font_name;

#if CONFIG_ESP_PAINTER_BASIC_FONT_12
ESP_PAINTER_FONT_DECLARE(esp_painter_basic_font_12);
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_12 */

#if CONFIG_ESP_PAINTER_BASIC_FONT_16
ESP_PAINTER_FONT_DECLARE(esp_painter_basic_font_16);
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_16 */

#if CONFIG_ESP_PAINTER_BASIC_FONT_20
ESP_PAINTER_FONT_DECLARE(esp_painter_basic_font_20);
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_20 */

#if CONFIG_ESP_PAINTER_BASIC_FONT_24
ESP_PAINTER_FONT_DECLARE(esp_painter_basic_font_24);
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_24 */

#if CONFIG_ESP_PAINTER_BASIC_FONT_28
ESP_PAINTER_FONT_DECLARE(esp_painter_basic_font_28);
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_28 */

#if CONFIG_ESP_PAINTER_BASIC_FONT_32
ESP_PAINTER_FONT_DECLARE(esp_painter_basic_font_32);
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_32 */

#if CONFIG_ESP_PAINTER_BASIC_FONT_36
ESP_PAINTER_FONT_DECLARE(esp_painter_basic_font_36);
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_36 */

#if CONFIG_ESP_PAINTER_BASIC_FONT_40
ESP_PAINTER_FONT_DECLARE(esp_painter_basic_font_40);
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_40 */

#if CONFIG_ESP_PAINTER_BASIC_FONT_44
ESP_PAINTER_FONT_DECLARE(esp_painter_basic_font_44);
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_44 */

#if CONFIG_ESP_PAINTER_BASIC_FONT_48
ESP_PAINTER_FONT_DECLARE(esp_painter_basic_font_48);
#endif  /* CONFIG_ESP_PAINTER_BASIC_FONT_48 */

#ifdef __cplusplus
}
#endif  /* __cplusplus */
