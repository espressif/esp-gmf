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

esp_gmf_oal_mutex_t esp_gmf_oal_mutex_create(void);
int esp_gmf_oal_mutex_destroy(esp_gmf_oal_mutex_t m);
int esp_gmf_oal_mutex_lock(esp_gmf_oal_mutex_t m);
int esp_gmf_oal_mutex_unlock(esp_gmf_oal_mutex_t m);
