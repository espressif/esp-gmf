/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "unity.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_asrc_types.h"
#include "esp_asrc.h"
#include "asrc_quality_test.h"

#define TAG               "ASRC_SW_TEST"
#define SINE_DURATION_MS  700
#define SINE_FREQ_HZ      1000.0f
#define SINE_AMPLITUDE_DB (-3.0f)

static uint32_t sample_rate[]     = {8000, 11025, 16000, 44100, 48000};
static uint8_t  channel[]         = {1, 2, 4};
static uint8_t  bits_per_sample[] = {8, 16, 24, 32};

static bool asrc_sw_test_with_quality_check(const uint8_t *in_data, uint32_t in_data_len,
                                            esp_asrc_aud_info_t *src, esp_asrc_aud_info_t *dst,
                                            esp_asrc_perf_type_t perf_type)
{
    ESP_LOGI(TAG, "=== SW Test: %" PRIu32 "Hz,%dch,%dbit -> %" PRIu32 "Hz,%dch,%dbit ===",
             src->sample_rate, src->channel, src->bits_per_sample,
             dst->sample_rate, dst->channel, dst->bits_per_sample);

    esp_asrc_cfg_t cfg = {
        .perf_type = perf_type,
        .complexity = 3,
        .weight = NULL,
        .weight_len = 0,
    };
    memcpy(&cfg.src_info, src, sizeof(esp_asrc_aud_info_t));
    memcpy(&cfg.dest_info, dst, sizeof(esp_asrc_aud_info_t));

    esp_asrc_handle_t handle = NULL;
    if (esp_asrc_open(&cfg, &handle) != ESP_ASRC_ERR_OK || handle == NULL) {
        ESP_LOGE(TAG, "Failed to open ASRC");
        return false;
    }

    uint16_t in_bytes_per_sample, out_bytes_per_sample;
    esp_asrc_get_bytes_per_sample(handle, &in_bytes_per_sample, &out_bytes_per_sample);

    uint32_t in_sample_num = 1024;
    uint32_t out_sample_num = 0;
    esp_asrc_get_out_sample_num(handle, in_sample_num, &out_sample_num);

    int in_chunk_bytes = in_sample_num * in_bytes_per_sample;
    int out_chunk_bytes = out_sample_num * out_bytes_per_sample;

    esp_asrc_buffer_alignment_t align = {0};
    if (esp_asrc_get_buffer_alignment(&align) != ESP_ASRC_ERR_OK) {
        ESP_LOGE(TAG, "esp_asrc_get_buffer_alignment failed");
        esp_asrc_close(handle);
        return false;
    }
    uint32_t allocated = 0;
    uint8_t *in_buf = (uint8_t *)esp_asrc_align_alloc((uint32_t)in_chunk_bytes, align.inbuf_addr_align,
                                                    align.inbuf_size_align, &allocated);
    allocated = 0;
    uint8_t *out_buf = (uint8_t *)esp_asrc_align_alloc((uint32_t)out_chunk_bytes, align.outbuf_addr_align,
                                                     align.outbuf_size_align, &allocated);
    if (!in_buf || !out_buf) {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        esp_asrc_close(handle);
        free(in_buf);
        free(out_buf);
        return false;
    }

    audio_stats_accumulator_t orig_acc = {0};
    audio_stats_accumulator_t proc_acc = {0};

    while (in_data_len > 0) {
        int read_size = in_data_len > (uint32_t)in_chunk_bytes ? in_chunk_bytes : (int)in_data_len;
        memcpy(in_buf, in_data, read_size);
        if (read_size < in_chunk_bytes) {
            memset(in_buf + read_size, 0, in_chunk_bytes - read_size);
        }
        in_data += read_size;
        in_data_len -= read_size;

        uint32_t outnum = out_sample_num;
        accumulate_audio_stats(&orig_acc, in_buf, in_chunk_bytes, src);
        esp_asrc_process(handle, in_buf, in_sample_num, out_buf, &outnum);
        accumulate_audio_stats(&proc_acc, out_buf, outnum * out_bytes_per_sample, dst);
    }

    bool is_pass = analyze_audio_quality(&orig_acc, &proc_acc);
    TEST_ASSERT_EQUAL(true, is_pass);

    esp_asrc_close(handle);
    free(in_buf);
    free(out_buf);
    return is_pass;
}

TEST_CASE("ASRC SW Basic Function Test", "[esp_asrc]")
{
    esp_asrc_aud_info_t dest = {0};
    for (int b = 1; b < sizeof(bits_per_sample) / sizeof(bits_per_sample[0]); b++) {
        for (int m = 0; m < sizeof(sample_rate) / sizeof(sample_rate[0]); m++) {
            for (int n = 0; n < sizeof(channel) / sizeof(channel[0]); n++) {
                esp_asrc_aud_info_t src = {
                    .sample_rate = sample_rate[m],
                    .channel = channel[n],
                    .bits_per_sample = bits_per_sample[b],
                };
                uint32_t input_size = (SINE_DURATION_MS * src.sample_rate / 1000) * src.channel * (src.bits_per_sample >> 3);
                uint8_t *input_buffer = (uint8_t *)calloc(input_size, 1);
                if (!input_buffer) {
                    ESP_LOGE(TAG, "Failed to alloc sine buffer");
                    continue;
                }
                asrc_generate_sine_signal(input_buffer, SINE_DURATION_MS, src.sample_rate,
                                          SINE_AMPLITUDE_DB, src.bits_per_sample, src.channel, SINE_FREQ_HZ);
                for (int i = 0; i < sizeof(sample_rate) / sizeof(sample_rate[0]); i++) {
                    for (int j = 0; j < sizeof(channel) / sizeof(channel[0]); j++) {
                        for (int k = 0; k < sizeof(bits_per_sample) / sizeof(bits_per_sample[0]); k++) {
                            dest.sample_rate = sample_rate[i];
                            dest.channel = channel[j];
                            dest.bits_per_sample = bits_per_sample[k];
                            if (!asrc_sw_test_with_quality_check(input_buffer, input_size, &src, &dest,
                                                                 ESP_ASRC_PERF_TYPE_SW_SPEED)) {
                                ESP_LOGE(TAG, "Quality test failed");
                            }
                        }
                    }
                }
                free(input_buffer);
                input_buffer = NULL;
            }
        }
    }
}
