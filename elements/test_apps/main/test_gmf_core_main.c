/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "unity.h"
#include "esp_gmf_app_unit_test.h"
#if CONFIG_IDF_TARGET_ESP32P4
#include "esp_hosted.h"
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */

void app_main(void)
{
#if CONFIG_IDF_TARGET_ESP32P4
    esp_hosted_init();
    esp_hosted_connect_to_slave();
#endif  /* CONFIG_IDF_TARGET_ESP32P4 */
    esp_gmf_app_test_main();
}
