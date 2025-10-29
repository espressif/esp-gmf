/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * Test API C++ compilation only, not as a example reference
 */
#include "esp_gmf_app_cli.h"
#include "esp_gmf_app_setup_peripheral.h"
#include "esp_gmf_app_sys.h"

extern "C" void test_cxx_build(void)
{
    void *sdcard_handle = NULL;
    esp_gmf_app_setup_sdcard(&sdcard_handle);
    esp_gmf_app_cli_init("test> ", NULL);

    esp_gmf_app_sys_monitor_start();
    esp_gmf_app_sys_monitor_stop();
}
