/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "driver/spi_master.h"
#include "driver/spi_common.h"

#include "periph_spi.h"

static const char *TAG = "PERIPH_SPI";

int periph_spi_init(void *cfg, int cfg_size, void **periph_handle)
{
    if (!cfg || !periph_handle || cfg_size < sizeof(periph_spi_config_t)) {
        ESP_LOGE(TAG, "Invalid parameters");
        return -1;
    }
    // Create handle with SPI port info
    periph_spi_handle_t *handle = calloc(1, sizeof(periph_spi_handle_t));
    if (!handle) {
        ESP_LOGE(TAG, "Failed to allocate handle");
        return -1;
    }
    periph_spi_config_t *config = (periph_spi_config_t *)cfg;
    // Initialize SPI bus using spi_bus_config from config
    ESP_LOGD(TAG, "SPI Port:%d, miso=%d, mosi=%d, sclk=%d, quadwp=%d, quadhd=%d, max_transfer_sz=%d, flags=0x%" PRIx32 ", intr_flags=0x%x",
             config->spi_port,
             config->spi_bus_config.miso_io_num,
             config->spi_bus_config.mosi_io_num,
             config->spi_bus_config.sclk_io_num,
             config->spi_bus_config.quadwp_io_num,
             config->spi_bus_config.quadhd_io_num,
             config->spi_bus_config.max_transfer_sz,
             config->spi_bus_config.flags,
             config->spi_bus_config.intr_flags);

    esp_err_t ret = spi_bus_initialize(config->spi_port, &config->spi_bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to initialize SPI bus: %s", esp_err_to_name(ret));
        return -1;
    }

    handle->spi_port = config->spi_port;
    *periph_handle = handle;

    ESP_LOGI(TAG, "SPI-%d peripheral initialized successfully", config->spi_port);
    return 0;
}

int periph_spi_deinit(void *periph_handle)
{
    if (!periph_handle) {
        ESP_LOGE(TAG, "Invalid handle");
        return -1;
    }

    periph_spi_handle_t *handle = (periph_spi_handle_t *)periph_handle;
    ESP_LOGI(TAG, "Deinitializing SPI peripheral port %d", handle->spi_port);
    spi_bus_free(handle->spi_port);
    free(periph_handle);
    return 0;
}
