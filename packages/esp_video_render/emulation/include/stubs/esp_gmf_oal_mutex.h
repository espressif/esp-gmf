/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef void *esp_gmf_oal_mutex_t;

// Minimal error type compatible with existing callsites.
typedef int esp_gmf_err_t;
#define ESP_GMF_ERR_OK  0

esp_gmf_oal_mutex_t esp_gmf_oal_mutex_create(void);
void esp_gmf_oal_mutex_delete(esp_gmf_oal_mutex_t m);
bool esp_gmf_oal_mutex_lock(esp_gmf_oal_mutex_t m, uint32_t timeout_ms);
bool esp_gmf_oal_mutex_unlock(esp_gmf_oal_mutex_t m);
