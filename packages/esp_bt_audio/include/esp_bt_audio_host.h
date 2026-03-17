/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */
#pragma once

#include "esp_err.h"
#ifdef CONFIG_BT_BLUEDROID_ENABLED
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#endif  /* CONFIG_BT_BLUEDROID_ENABLED */

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief  Maximum length of Bluetooth device name
 */
#define ESP_BT_AUDIO_HOST_MAX_DEV_NAME_LEN  (32)

#ifdef CONFIG_BT_NIMBLE_ENABLED

/**
 * @brief  Structure for NimBLE host configuration
 */
typedef struct {
    char  dev_name[ESP_BT_AUDIO_HOST_MAX_DEV_NAME_LEN];  /*!< Device name */
} esp_bt_audio_host_nimble_cfg_t;

/**
 * @brief  Default configuration for NimBLE host
 */
#define ESP_BT_AUDIO_HOST_NIMBLE_CFG_DEFAULT()  {  \
    .dev_name = "esp_ble",                         \
}
#endif  /* CONFIG_BT_NIMBLE_ENABLED */

#ifdef CONFIG_BT_BLUEDROID_ENABLED
/**
 * @brief  Structure for Bluedroid host configuration
 */
typedef struct {
    char                    dev_name[ESP_BT_AUDIO_HOST_MAX_DEV_NAME_LEN];  /*!< Device name */
    esp_bluedroid_config_t  bluedroid_cfg;                                 /*!< Bluedroid configuration */
    esp_bt_pin_type_t       pin_type;                                      /*!< PIN type */
    esp_bt_pin_code_t       pin_code;                                      /*!< PIN code */
    esp_bt_sp_param_t       sp_param;                                      /*!< Simple Pairing parameter */
    esp_bt_io_cap_t         iocap;                                         /*!< IO capability */
} esp_bt_audio_host_bluedroid_cfg_t;

/**
 * @brief  Default configuration for Bluedroid host
 */
#define ESP_BT_AUDIO_HOST_BLUEDROID_CFG_DEFAULT()  {      \
    .dev_name      = "esp_classic",                       \
    .bluedroid_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT(),  \
    .pin_type      = ESP_BT_PIN_TYPE_FIXED,               \
    .pin_code      = {'1', '2', '3', '4'},                \
    .sp_param      = ESP_BT_SP_IOCAP_MODE,                \
    .iocap         = ESP_BT_IO_CAP_NONE,                  \
}
#endif  /* CONFIG_BT_BLUEDROID_ENABLED */

/**
 * @brief  Initialize the Bluetooth host
 *
 * @param[in]  cfg  Pointer to the host configuration (esp_bt_audio_host_nimble_cfg_t or esp_bt_audio_host_bluedroid_cfg_t)
 *
 * @return
 *       - ESP_OK               On success
 *       - ESP_ERR_INVALID_ARG  Invalid argument
 *       - Other                Error code from Bluetooth stack initialization
 */
esp_err_t esp_bt_audio_host_init(void *cfg);

/**
 * @brief  Deinitialize the Bluetooth host
 */
void esp_bt_audio_host_deinit(void);

#ifdef __cplusplus
}
#endif  /* __cplusplus */
