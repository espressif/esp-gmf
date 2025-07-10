/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * Test API C++ compilation only, not as a example reference
 */
#include "esp_gmf_pool.h"
#include "gmf_loader_setup_defaults.h"

extern "C" void test_cxx_build(void)
{
    esp_gmf_pool_handle_t pool = NULL;

    esp_gmf_pool_init(&pool);

    gmf_loader_setup_all_defaults(pool);

    gmf_loader_teardown_all_defaults(pool);

    esp_gmf_pool_deinit(pool);
}
