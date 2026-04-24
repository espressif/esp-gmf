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
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "unity.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_cache.h"
#include "esp_asrc.h"
#include "esp_asrc_types.h"
#include "asrc_quality_test.h"

#define TAG           "ASRC_HW_TEST"
#define ASRC_DONE_BIT BIT0
// #define CPU_LOAD_TEST
#ifdef CPU_LOAD_TEST
double   sum_enc = 0;
double   sum_dec = 0;
uint64_t start;
uint64_t stop;
#endif  /* CPU_LOAD_TEST */

extern const uint8_t t_start[] asm("_binary_test_16000_2_16_pcm_start");
extern const uint8_t t_end[] asm("_binary_test_16000_2_16_pcm_end");
extern const uint8_t t1_start[] asm("_binary_test_16000_2_8_pcm_start");
extern const uint8_t t1_end[] asm("_binary_test_16000_2_8_pcm_end");
extern const uint8_t t2_start[] asm("_binary_test_16000_2_24_pcm_start");
extern const uint8_t t2_end[] asm("_binary_test_16000_2_24_pcm_end");
extern const uint8_t t3_start[] asm("_binary_test_16000_2_32_pcm_start");
extern const uint8_t t3_end[] asm("_binary_test_16000_2_32_pcm_end");

static uint32_t sample_rate[]     = {8000, 16000, 22050, 24000, 32000, 44100, 48000};
static uint8_t  bits_per_sample[] = {8, 16, 24, 32};
static uint8_t  channel[]         = {1, 2};
typedef struct {
    esp_asrc_cfg_t     *asrc_info;
    int                 sample_num;
    uint8_t            *src_data;
    uint32_t            src_data_len;
    uint8_t            *cmp_data;
    EventGroupHandle_t  event_group;
} asrc_test_task_arg_t;

void psram_stress_task(void *arg)
{
    uint8_t *stress_buf = (uint8_t *)arg;
    while (1) {
        for (int i = 0; i < 512 * 1024; ++i) {
            stress_buf[i] = (uint8_t)(i & 0xFF);
        }
        esp_cache_msync(stress_buf, 512 * 1024, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
        volatile uint32_t sum = 0;
        for (int i = 0; i < 512 * 1024; ++i) {
            sum += stress_buf[i];
        }
        esp_cache_msync(stress_buf, 512 * 1024, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void asrc_test_task(void *arg)
{
    asrc_test_task_arg_t *asrc_info = (asrc_test_task_arg_t *)arg;
    esp_asrc_cfg_t *asrc_cfg = asrc_info->asrc_info;
    esp_asrc_handle_t asrc_hd = NULL;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_open(asrc_cfg, &asrc_hd));
    int32_t sample_num = asrc_info->sample_num;
    uint32_t out_samples = 0;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_get_out_sample_num(asrc_hd, sample_num, &out_samples));
    uint16_t in_bytes_per_sample = 0;
    uint16_t out_bytes_per_sample = 0;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_get_bytes_per_sample(asrc_hd, &in_bytes_per_sample, &out_bytes_per_sample));
    uint32_t in_bytes_cnt = sample_num * in_bytes_per_sample;
    uint32_t out_bytes_cnt = out_samples * out_bytes_per_sample;
    esp_asrc_buffer_alignment_t psram_align = {0};
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_get_buffer_alignment(&psram_align));
    uint32_t allocated_size = 0;
    uint8_t *inbuf = esp_asrc_align_alloc(in_bytes_cnt, psram_align.inbuf_addr_align, psram_align.inbuf_size_align,
                                         &allocated_size);
    TEST_ASSERT_NOT_EQUAL(inbuf, NULL);
    in_bytes_cnt = allocated_size;
    uint8_t *outbuf = esp_asrc_align_alloc(out_bytes_cnt, psram_align.outbuf_addr_align, psram_align.outbuf_size_align,
                                         &allocated_size);
    TEST_ASSERT_NOT_EQUAL(outbuf, NULL);
    out_bytes_cnt = allocated_size;
    uint32_t out_samples_temp = out_bytes_cnt / out_bytes_per_sample;
    while (asrc_info->src_data_len > 0) {
        if (asrc_info->src_data_len > in_bytes_cnt) {
            memcpy(inbuf, asrc_info->src_data, in_bytes_cnt);
            asrc_info->src_data += in_bytes_cnt;
            asrc_info->src_data_len -= in_bytes_cnt;
        } else {
            break;
        }
        out_samples = out_samples_temp;
        TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_process(asrc_hd, inbuf, sample_num, outbuf, &out_samples));
        out_samples = out_samples * out_bytes_per_sample;
        if (asrc_info->cmp_data) {
            TEST_ASSERT_EQUAL(0, memcmp(outbuf, asrc_info->cmp_data, out_samples));
            asrc_info->cmp_data += out_samples;
        }
    }
    esp_asrc_close(asrc_hd);
    free(inbuf);
    free(outbuf);
    // Send done event to notify psram_stress_task to exit
    xEventGroupSetBits(asrc_info->event_group, ASRC_DONE_BIT);
    ESP_LOGI(TAG, "asrc_test_task completed, sent done event");
    vTaskDelete(NULL);
}

static bool asrc_test_with_quality_check(const uint8_t *in_data, uint32_t in_data_len, esp_asrc_aud_info_t *src,
                                         esp_asrc_aud_info_t *dst, esp_asrc_perf_type_t perf_type)
{
    ESP_LOGI(TAG, "=== Quality Test: %" PRIu32 "Hz,%dch,%dbit -> %" PRIu32 "Hz,%dch,%dbit ===",
             src->sample_rate, src->channel, src->bits_per_sample,
             dst->sample_rate, dst->channel, dst->bits_per_sample);

    esp_asrc_handle_t handle;
    float weight[2] = {0.0};
    float *weight_tmp = NULL;
    if (src->channel == 2 && dst->channel == 1) {
        weight[0] = 1.0;
        weight_tmp = weight;
    } else if (src->channel == 1 && dst->channel == 2) {
        weight[0] = 1.0;
        weight[1] = 1.0;
        weight_tmp = weight;
    }
    esp_asrc_cfg_t cfg = {
        .perf_type = perf_type,
        .complexity = 1,
        .timeout_ms = 300,
        .weight = weight_tmp,
        .weight_len = src->channel * dst->channel,
    };
    memcpy(&cfg.src_info, src, sizeof(esp_asrc_aud_info_t));
    memcpy(&cfg.dest_info, dst, sizeof(esp_asrc_aud_info_t));
    if (esp_asrc_open(&cfg, &handle) != ESP_ASRC_ERR_OK || handle == NULL) {
        ESP_LOGE(TAG, "ERROR: Failed to open ASRC");
        return false;
    }
    uint16_t in_bytes_per_sample, out_bytes_per_sample;
    esp_asrc_get_bytes_per_sample(handle, &in_bytes_per_sample, &out_bytes_per_sample);
    uint32_t in_sample_num = 200;
    uint32_t out_sample_num = 0;
    esp_asrc_get_out_sample_num(handle, in_sample_num, &out_sample_num);
    int in_buf_size = in_sample_num * in_bytes_per_sample;
    int out_buf_size = out_sample_num * out_bytes_per_sample;
    esp_asrc_buffer_alignment_t psram_align = {0};
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_get_buffer_alignment(&psram_align));
    uint32_t allocated_size = 0;
    uint8_t *in_buf = esp_asrc_align_alloc((uint32_t)in_buf_size, psram_align.inbuf_addr_align,
                                          psram_align.inbuf_size_align, &allocated_size);
    in_buf_size = (int)allocated_size;
    uint8_t *out_buf = esp_asrc_align_alloc((uint32_t)out_buf_size, psram_align.outbuf_addr_align,
                                          psram_align.outbuf_size_align, &allocated_size);
    out_buf_size = allocated_size;
    out_sample_num = out_buf_size / out_bytes_per_sample;
    if (!in_buf || !out_buf) {
        ESP_LOGE(TAG, "ERROR: Cannot allocate conversion buffers");
        esp_asrc_close(handle);
        return false;
    }
    int32_t frame = 0;
    audio_stats_accumulator_t orig_acc = {0};
    audio_stats_accumulator_t proc_acc = {0};
    while (in_data_len > 0) {
        frame++;
        int read_size = in_data_len > in_buf_size ? in_buf_size : in_data_len;
        memcpy(in_buf, in_data, read_size);
        if (read_size < in_buf_size) {
            memset(in_buf + read_size, 0, in_buf_size - read_size);
        }
        in_data += read_size;
        in_data_len -= read_size;
        uint32_t read_sample_num = in_buf_size / in_bytes_per_sample;
        uint32_t outnum = out_sample_num;
#ifdef CPU_LOAD_TEST
        start = esp_cpu_get_cycle_count();
#endif  /* CPU_LOAD_TEST */
        esp_asrc_process(handle, in_buf, read_sample_num, out_buf, &outnum);
#ifdef CPU_LOAD_TEST
        stop = esp_cpu_get_cycle_count();
        sum_dec += ((double)(stop - start)) / 64.0 / 1000.0;
#endif  /* CPU_LOAD_TEST */
        accumulate_audio_stats(&orig_acc, in_buf, read_sample_num * in_bytes_per_sample, src);
        accumulate_audio_stats(&proc_acc, out_buf, outnum * out_bytes_per_sample, dst);
    }
    bool is_pass = analyze_audio_quality(&orig_acc, &proc_acc);
    TEST_ASSERT_EQUAL(is_pass, true);
#ifdef CPU_LOAD_TEST
    int file_time = frame * in_sample_num / src->sample_rate * 1000;
    printf("dec %.4f %.4f %d\n", sum_dec / file_time * 100.0, sum_dec, file_time);
    sum_dec = 0.0;
#endif  /* CPU_LOAD_TEST */
    esp_asrc_close(handle);
    free(in_buf);
    free(out_buf);
    return true;
}

TEST_CASE("ASRC HW Function With Cache Stress Test", "[esp_asrc]")
{
    uint8_t *buf = (uint8_t *)t_start;
    int32_t len = t_end - t_start;
    uint8_t *cmpbuf = NULL;
    // Create event group for task synchronization
    EventGroupHandle_t event_group = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(event_group);
    uint8_t *stress_buf = heap_caps_aligned_calloc(32, 1, 512 * 1024, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    TEST_ASSERT_NOT_EQUAL(stress_buf, NULL);
    // Create psram stress task
    TaskHandle_t stress_task_handle = NULL;
    xTaskCreatePinnedToCore(psram_stress_task, "core0_stress", 8192, stress_buf, 6, &stress_task_handle, 0);
    float weight1[2] = {1.0, 0.0};
    esp_asrc_cfg_t asrc_cfg = {
        .src_info = {
            .sample_rate = 16000,
            .channel = 2,
            .bits_per_sample = 16,
        },
        .dest_info = {
            .sample_rate = 48000,
            .channel = 1,
            .bits_per_sample = 16,
        },
        .weight = weight1,
        .weight_len = 2,
        .perf_type = ESP_ASRC_PERF_TYPE_HW_ONLY,
        .timeout_ms = 300,
    };
    asrc_test_task_arg_t asrc_info = {
        .asrc_info = &asrc_cfg,
        .sample_num = 500,
        .src_data = buf,
        .src_data_len = len,
        .cmp_data = cmpbuf,
        .event_group = event_group,
    };
    // Create asrc test task
    TaskHandle_t asrc_task_handle = NULL;
    xTaskCreatePinnedToCore(asrc_test_task, "asrc_test_task", 8192, &asrc_info, 7, &asrc_task_handle, 0);
    // Wait for asrc test to complete and send done event
    xEventGroupWaitBits(event_group, ASRC_DONE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    vTaskDelete(stress_task_handle);
    heap_caps_free(stress_buf);
    // Clean up event group
    vEventGroupDelete(event_group);
    ESP_LOGI(TAG, "ASRC HW function test completed successfully");
    vTaskDelay(pdMS_TO_TICKS(1000));
}

TEST_CASE("ASRC Multi Channel Test", "[esp_asrc]")
{
    uint8_t *buffer1 = (uint8_t *)t_start;
    uint32_t len1 = t_end - t_start;
    uint8_t *cmp_buf1 = NULL;
    uint8_t *buffer2 = (uint8_t *)t_start;
    uint32_t len2 = t_end - t_start;
    uint8_t *cmp_buf2 = NULL;
    uint8_t *buffer3 = (uint8_t *)t_start;
    uint32_t len3 = t_end - t_start;
    EventGroupHandle_t event_group1 = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(event_group1);
    EventGroupHandle_t event_group2 = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(event_group2);
    EventGroupHandle_t event_group3 = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL(event_group3);
    float weight1[2] = {1.0, 0.0};
    esp_asrc_cfg_t asrc_cfg1 = {
        .src_info = {
            .sample_rate = 16000,
            .channel = 2,
            .bits_per_sample = 16,
        },
        .dest_info = {
            .sample_rate = 48000,
            .channel = 1,
            .bits_per_sample = 16,
        },
        .weight = weight1,
        .weight_len = 2,
        .perf_type = ESP_ASRC_PERF_TYPE_AUTO,
        .complexity = 1,
        .timeout_ms = 300,
    };
    asrc_test_task_arg_t asrc_info1 = {
        .asrc_info = &asrc_cfg1,
        .sample_num = 200,
        .src_data = buffer1,
        .src_data_len = len1,
        .cmp_data = cmp_buf1,
        .event_group = event_group1,
    };
    float weight2[2] = {0.0, 1.0};
    esp_asrc_cfg_t asrc_cfg2 = {
        .src_info = {
            .sample_rate = 16000,
            .channel = 2,
            .bits_per_sample = 16,
        },
        .dest_info = {
            .sample_rate = 48000,
            .channel = 1,
            .bits_per_sample = 16,
        },
        .weight = weight2,
        .weight_len = 2,
        .perf_type = ESP_ASRC_PERF_TYPE_AUTO,
        .complexity = 1,
        .timeout_ms = 300,
    };
    asrc_test_task_arg_t asrc_info2 = {
        .asrc_info = &asrc_cfg2,
        .sample_num = 2040,
        .src_data = buffer2,
        .src_data_len = len2,
        .cmp_data = cmp_buf2,
        .event_group = event_group2,
    };
    float weight3[2] = {0.5, 0.5};
    esp_asrc_cfg_t asrc_cfg3 = {
        .src_info = {
            .sample_rate = 16000,
            .channel = 2,
            .bits_per_sample = 16,
        },
        .dest_info = {
            .sample_rate = 48000,
            .channel = 1,
            .bits_per_sample = 16,
        },
        .weight = weight3,
        .weight_len = 2,
        .perf_type = ESP_ASRC_PERF_TYPE_AUTO,
        .complexity = 1,
        .timeout_ms = 300,
    };
    asrc_test_task_arg_t asrc_info3 = {
        .asrc_info = &asrc_cfg3,
        .sample_num = 800,
        .src_data = buffer3,
        .src_data_len = len3,
        .cmp_data = NULL,
        .event_group = event_group3,
    };
    xTaskCreatePinnedToCore(asrc_test_task, "asrc_test_task1", 8192, &asrc_info1, 7, NULL, 0);
    xTaskCreatePinnedToCore(asrc_test_task, "asrc_test_task2", 8192, &asrc_info2, 7, NULL, 0);
    xTaskCreatePinnedToCore(asrc_test_task, "asrc_test_task3", 8192, &asrc_info3, 7, NULL, 0);
    xEventGroupWaitBits(event_group1, ASRC_DONE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(event_group2, ASRC_DONE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    xEventGroupWaitBits(event_group3, ASRC_DONE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
    vEventGroupDelete(event_group1);
    vEventGroupDelete(event_group2);
    vEventGroupDelete(event_group3);
    vTaskDelay(pdMS_TO_TICKS(1000));
}

TEST_CASE("ASRC HW Basic Function Test", "[esp_asrc]")
{
    esp_asrc_aud_info_t dest = {0};
    for (int m = 0; m < sizeof(sample_rate) / sizeof(sample_rate[0]); m++) {
        for (int n = 0; n < sizeof(channel) / sizeof(channel[0]); n++) {
            esp_asrc_aud_info_t src = {
                .sample_rate = sample_rate[m],
                .channel = channel[n],
                .bits_per_sample = 16,
            };
            uint32_t num_samples = 1000 * sample_rate[m] / 1000;
            uint32_t input_size = num_samples * src.channel * (src.bits_per_sample >> 3);
            uint8_t *input_buffer = (uint8_t *)calloc(input_size, 1);
            asrc_generate_sine_signal(input_buffer, 1000, sample_rate[m], 0.0, src.bits_per_sample, src.channel, 1000.0);
            for (int i = 0; i < sizeof(sample_rate) / sizeof(sample_rate[0]); i++) {
                for (int j = 0; j < sizeof(channel) / sizeof(channel[0]); j++) {
                    for (int k = 0; k < sizeof(bits_per_sample) / sizeof(bits_per_sample[0]); k++) {
                        dest.sample_rate = sample_rate[i];
                        dest.channel = channel[j];
                        dest.bits_per_sample = bits_per_sample[k];
                        int ret = asrc_test_with_quality_check(input_buffer, input_size, &src, &dest, ESP_ASRC_PERF_TYPE_AUTO);
                        if (!ret) {
                            ESP_LOGE(TAG, "Quality test failed");
                            break;
                        }
                    }
                }
            }
            free(input_buffer);
            input_buffer = NULL;
        }
    }
}
