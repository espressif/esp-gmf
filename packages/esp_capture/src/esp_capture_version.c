
/**
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include "esp_capture_version.h"

#define STRINGIFY(x)                        #x
#define TOSTRING(x)                         STRINGIFY(x)
#define VERSION_STRING(major, minor, patch) TOSTRING(major) "." TOSTRING(minor) "." TOSTRING(patch)

const char *esp_capture_get_version(void)
{
    return VERSION_STRING(ESP_CAPTURE_VER_MAJOR, ESP_CAPTURE_VER_MINOR, ESP_CAPTURE_VER_PATCH);
}
