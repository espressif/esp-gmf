/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#pragma once

#include "esp_gmf_io.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief  File IO configurations, if any entry is zero then the configuration will be set to default values
 */
typedef struct {
    int          dir;         /*!< IO direction, reader or writer */
    const char  *name;        /*!< Name for this instance */
    int          cache_size;  /*!< Cache size for file IO operations in bytes. If size <= 512, it will be set to 0.
                                   Note: Larger cache size will improve read and write performance but consume more memory */
    int          cache_caps;  /*!< Cache memory capabilities, if zero then it will be set to MALLOC_CAP_DMA.
                                   Note:
                                        1. If chips have SOC_SDMMC_PSRAM_DMA_CAPABLE capability(such as ESP32P4),
                                            then you can set (MALLOC_CAP_SPIRAM | MALLOC_CAP_DMA) to save SRAM
                                        2. For ESP32, should use (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) or MALLOC_CAP_DMA caps to malloc cache
                                        3. For ESP32Sxx and ESP32Cxx, can also use MALLOC_CAP_INTERNAL caps to malloc cache */
} file_io_cfg_t;

#define FILE_IO_CFG_DEFAULT() {     \
    .dir  = ESP_GMF_IO_DIR_READER,  \
    .name = NULL,                   \
}

/**
 * @brief  Initializes the file stream I/O with the provided configuration
 *
 * @param[in]   config  Pointer to the file IO configuration
 * @param[out]  io      Pointer to the file IO handle to be initialized
 *
 * @return
 *       - ESP_GMF_ERR_OK           Success
 *       - ESP_GMF_ERR_INVALID_ARG  Invalid configuration provided
 *       - ESP_GMF_ERR_MEMORY_LACK  Failed to allocate memory
 */
esp_gmf_err_t esp_gmf_io_file_init(file_io_cfg_t *config, esp_gmf_io_handle_t *io);

#ifdef __cplusplus
}
#endif /* __cplusplus */
