/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "esp_log.h"
#include "esp_cpu.h"
#include "esp_asrc.h"
#include "esp_asrc_types.h"

static const char *TAG = "ASRC_EXAMPLE";

extern const uint8_t thetest16_1_start[] asm("_binary_thetest16_1_pcm_start");
extern const uint8_t thetest16_1_end[] asm("_binary_thetest16_1_pcm_end");

/**
 * @brief  ASRC read context structure
 */
typedef struct {
    const uint8_t  *data_ptr;   /*!< Current read pointer */
    uint32_t        remaining;  /*!< Remaining bytes to read */
} asrc_read_ctx_t;

/**
 * @brief  ASRC write context structure
 */
typedef struct {
    uint8_t  *data_ptr;    /*!< Current write pointer */
    uint32_t  total_size;  /*!< Total bytes written */
} asrc_write_ctx_t;

static uint32_t asrc_read_data(asrc_read_ctx_t *ctx, uint8_t *buf, uint32_t buf_size)
{
    uint32_t read_size = (ctx->remaining < buf_size) ? ctx->remaining : buf_size;
    if (read_size > 0) {
        memcpy(buf, ctx->data_ptr, read_size);
        ctx->data_ptr += read_size;
        ctx->remaining -= read_size;
    }
    return read_size;
}

static uint32_t asrc_write_data(asrc_write_ctx_t *ctx, uint8_t *buf, uint32_t buf_size)
{
    (void)buf;
    ctx->total_size += buf_size;
    return buf_size;
}

void app_main(void)
{
    esp_asrc_handle_t asrc_handle = NULL;
    uint8_t *in_buf = NULL;
    uint8_t *out_buf = NULL;
    esp_asrc_err_t ret;
    float weight[] = {1.0, 1.0};
    uint16_t in_sample_bytes, out_sample_bytes;
    uint32_t in_samples = 480;
    uint32_t out_samples = 0;
    esp_asrc_cfg_t asrc_cfg = {
        .src_info = {
            .sample_rate = 16000,
            .channel = 1,
            .bits_per_sample = 16,
        },
        .dest_info = {
            .sample_rate = 48000,
            .channel = 2,
            .bits_per_sample = 16,
        },
        .weight = weight,
        .weight_len = 2,
        .perf_type = ESP_ASRC_PERF_TYPE_AUTO,
        .complexity = 3,  /* Apply only when using software ASRC */
        .timeout_ms = 1000,
    };
    // Create ASRC handle
    ret = esp_asrc_open(&asrc_cfg, &asrc_handle);
    if (ret != ESP_ASRC_ERR_OK) {
        ESP_LOGE(TAG, "Failed to open ASRC: %d", ret);
        return;
    }
    // Create input and output buffer (internal RAM: use_psram = 0)
    esp_asrc_get_bytes_per_sample(asrc_handle, &in_sample_bytes, &out_sample_bytes);
    esp_asrc_get_out_sample_num(asrc_handle, in_samples, &out_samples);
    uint32_t in_buf_size = in_samples * in_sample_bytes;
    uint32_t out_buf_size = out_samples * out_sample_bytes;
    esp_asrc_buffer_alignment_t buf_al = {0};
    ret = esp_asrc_get_buffer_alignment(&buf_al);
    if (ret != ESP_ASRC_ERR_OK) {
        ESP_LOGE(TAG, "Failed to get buffer alignment: %d", ret);
        goto cleanup;
    }
    uint32_t allocated_in_size = 0;
    uint32_t allocated_out_size = 0;
    in_buf = (uint8_t *)esp_asrc_align_alloc(in_buf_size, buf_al.inbuf_addr_align, buf_al.inbuf_size_align,
                                             &allocated_in_size);
    out_buf = (uint8_t *)esp_asrc_align_alloc(out_buf_size, buf_al.outbuf_addr_align, buf_al.outbuf_size_align,
                                              &allocated_out_size);
    if (!in_buf || !out_buf) {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        goto cleanup;
    }
    // ASRC process
    asrc_read_ctx_t read_ctx = {
        .data_ptr = thetest16_1_start,
        .remaining = thetest16_1_end - thetest16_1_start,
    };
    asrc_write_ctx_t write_ctx = {
        .data_ptr = NULL,
        .total_size = 0,
    };
    uint32_t read_size;
    while ((read_size = asrc_read_data(&read_ctx, in_buf, in_buf_size)) > 0) {
        uint32_t process_samples = read_size / in_sample_bytes;
        uint32_t actual_out_samples = allocated_out_size / out_sample_bytes;
        ret = esp_asrc_process(asrc_handle, in_buf, process_samples, out_buf, &actual_out_samples);
        if (ret != ESP_ASRC_ERR_OK) {
            ESP_LOGE(TAG, "ASRC process failed: %d", ret);
            goto cleanup;
        }
        uint32_t out_bytes = actual_out_samples * out_sample_bytes;
        asrc_write_data(&write_ctx, out_buf, out_bytes);
    }
cleanup:
    if (in_buf) {
        free(in_buf);
    }
    if (out_buf) {
        free(out_buf);
    }
    if (asrc_handle) {
        esp_asrc_close(asrc_handle);
    }
    ESP_LOGI(TAG, "ASRC process finished");
}
