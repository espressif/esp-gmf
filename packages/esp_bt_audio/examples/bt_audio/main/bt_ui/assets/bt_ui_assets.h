/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief  BT UI assets: Chinese font (Noto Sans SC 28px) and stream icons
 *
 * Font bitmaps and stream icons live in the dedicated "assets" flash partition
 * and are mapped with esp_partition_mmap(). Pixel data stays in flash
 * (zero-copy); only small descriptor tables are allocated in RAM.
 */
#pragma once

#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Stream-icon ids stored in the assets partition
 */
typedef enum {
    BT_UI_IMAGE_CIS_STREAM = 1,
    BT_UI_IMAGE_BIS_STREAM = 2,
} bt_ui_image_id_t;

/**
 * @brief  Map the assets partition and load the Chinese font and stream icons
 *
 * @return
 *       - ESP_OK   On success
 *       - Others   On failure
 */
esp_err_t bt_ui_assets_init(void);

/**
 * @brief  Release mapped font and icon resources
 */
void bt_ui_assets_deinit(void);

/**
 * @brief  28px Noto Sans SC font, or NULL before init / on failure
 */
const lv_font_t *bt_ui_font_cn_28(void);

/**
 * @brief  Stream icon from the assets partition, or NULL if it is not loaded
 */
const lv_image_dsc_t *bt_ui_image(bt_ui_image_id_t id);

#define BT_UI_FONT_CN_28 (bt_ui_font_cn_28())

#ifdef __cplusplus
}
#endif
