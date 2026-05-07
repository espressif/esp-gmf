/**
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include <stdint.h>

typedef int esp_gmf_err_io_t;

// Match ESP-GMF conventions used in esp_video_render sources.
#define ESP_GMF_IO_OK    0
#define ESP_GMF_IO_FAIL  (-1)

typedef struct {
    uint8_t  *buf;
    uint32_t  buf_length;
    uint32_t  valid_size;
} esp_gmf_payload_t;

typedef esp_gmf_err_io_t (*port_acquire)(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks);
typedef esp_gmf_err_io_t (*port_release)(void *handle, esp_gmf_payload_t *load, int wait_ticks);
