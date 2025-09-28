/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_capture_types.h"
#include "capture_os.h"
#include "capture_thread.h"
#include "capture_mem.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define CAPTURE_CHECK_MEM(ptr, reason) if (ptr == NULL) {  \
    ESP_LOGE(TAG, "No enough memory for" reason);          \
}

#define CAPTURE_CHECK_MEM_RET(ptr, reason, ret) if (ptr == NULL) {  \
    ESP_LOGE(TAG, "No enough memory for" reason);                   \
    return ret;                                                     \
}

#define CAPTURE_VERIFY_PTR(ptr, reason) if ((ptr) == NULL) {  \
    ESP_LOGE(TAG, "Invalid argument for" reason);             \
}

#define CAPTURE_VERIFY_PTR_RET(ptr, reason) if ((ptr) == NULL) {  \
    ESP_LOGE(TAG, "Invalid argument of" reason);                  \
    return ESP_CAPTURE_ERR_INVALID_ARG;                           \
}

#define CAPTURE_LOG_ON_ERR(ret, reason, ...) if (ret != ESP_CAPTURE_ERR_OK) {  \
    ESP_LOGE(TAG, reason, ##__VA_ARGS__);                                      \
}

#define CAPTURE_SAFE_FREE(ptr) if (ptr) {  \
    capture_free(ptr);                     \
}

#define CAPTURE_BREAK_SET_RETURN(ret, ret_val) {  \
    ret = ret_val;                                \
    break;                                        \
}

#define CAPTURE_BREAK_ON_ERR(statement) if ((statement) != 0) {  \
    break;                                                       \
}

#define CAPTURE_RETURN_ON_FAIL(ret) if (ret != 0) {  \
    return ret;                                      \
}

#define CAPTURE_SAFE_STR(str) ((str) ? (str) : "")

#define CAPTURE_ELEMS(array) (sizeof(array) / sizeof(array[0]))

#ifdef __cplusplus
}
#endif  /* __cplusplus */
