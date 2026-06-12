/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "unity.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_asrc.h"
#include "esp_asrc_types.h"
#include "sdkconfig.h"

static const char *TAG = "ASRC_BRANCH_UT";

#define BRANCH_LOG(tc, desc) ESP_LOGI(TAG, "---- %s : %s ----", (tc), (desc))

static esp_asrc_err_t plan_smoke_process(const esp_asrc_cfg_t *cfg, uint32_t in_samples,
                                         uint32_t *out_samples_out)
{
    esp_asrc_cfg_t cfg_copy = *cfg;
    esp_asrc_handle_t h = NULL;
    esp_asrc_err_t e = esp_asrc_open(&cfg_copy, &h);
    if (e != ESP_ASRC_ERR_OK || h == NULL) {
        return e;
    }
    uint32_t need = 0;
    e = esp_asrc_get_out_sample_num(h, in_samples, &need);
    if (e != ESP_ASRC_ERR_OK) {
        esp_asrc_close(h);
        return e;
    }
    uint16_t ib = 0, ob = 0;
    esp_asrc_get_bytes_per_sample(h, &ib, &ob);
    uint32_t in_bytes = (uint32_t)in_samples * ib;
    uint32_t out_bytes = need * ob;
    esp_asrc_buffer_alignment_t psram_al = {0};
    e = esp_asrc_get_buffer_alignment(&psram_al);
    if (e != ESP_ASRC_ERR_OK) {
        esp_asrc_close(h);
        return e;
    }
    uint32_t allocated_size = 0;
    uint8_t *in_buf = (uint8_t *)esp_asrc_align_alloc(in_bytes, psram_al.inbuf_addr_align, psram_al.inbuf_size_align,
                                                      &allocated_size);
    if (!in_buf) {
        esp_asrc_close(h);
        return ESP_ASRC_ERR_MEM_LACK;
    }
    allocated_size = 0;
    uint8_t *out_buf = (uint8_t *)esp_asrc_align_alloc(out_bytes, psram_al.outbuf_addr_align, psram_al.outbuf_size_align,
                                                       &allocated_size);
    if (!out_buf) {
        free(in_buf);
        esp_asrc_close(h);
        return ESP_ASRC_ERR_MEM_LACK;
    }
    memset(in_buf, 0, (size_t)in_bytes);
    uint32_t out_n = allocated_size / ob;
    e = esp_asrc_process(h, in_buf, in_samples, out_buf, &out_n);
    if (out_samples_out) {
        *out_samples_out = out_n;
    }
    free(in_buf);
    free(out_buf);
    esp_asrc_close(h);
    return e;
}

static void open_branch_null_args(void)
{
    BRANCH_LOG("TC-O-01", "NULL cfg / NULL handle pointer");
    esp_asrc_handle_t h = (esp_asrc_handle_t)1;
    esp_asrc_cfg_t cfg = {0};
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER, esp_asrc_open(NULL, &h));
    TEST_ASSERT_NULL(h);
    h = NULL;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER, esp_asrc_open(&cfg, NULL));
}

static void open_branch_invalid_perf_type(void)
{
    BRANCH_LOG("TC-O-02", "invalid perf_type enum value");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 48000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = (esp_asrc_perf_type_t)4,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = (esp_asrc_handle_t)1;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER, esp_asrc_open(&cfg, &h));
    TEST_ASSERT_NULL(h);
}

static void open_branch_bypass_identical(void)
{
    BRANCH_LOG("TC-O-03", "bypass path when src == dest format");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_open(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);
    esp_asrc_close(h);
}

#if !CONFIG_SOC_ASRC_SUPPORTED
static void open_branch_hw_only_not_support(void)
{
    BRANCH_LOG("TC-O-04", "HW_ONLY on non-H4 target => NOT_SUPPORT");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 48000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_HW_ONLY,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = (esp_asrc_handle_t)1;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_NOT_SUPPORT, esp_asrc_open(&cfg, &h));
    TEST_ASSERT_NULL(h);
}
#else   /* CONFIG_SOC_ASRC_SUPPORTED */
static void open_branch_hw_only_smoke(void)
{
    BRANCH_LOG("TC-O-04b", "HW_ONLY smoke open on H4 (16k->48k mono s16)");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 48000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_HW_ONLY,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_open(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);
    esp_asrc_close(h);
}

static void open_branch_hw_timeout_zero(void)
{
    BRANCH_LOG("TC-O-04c", "HW_ONLY: timeout_ms==0 => esp_asrc_open returns ESP_ASRC_ERR_INVALID_PARAMETER");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 48000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_HW_ONLY,
        .complexity = 1,
        .timeout_ms = 0,
    };
    esp_asrc_handle_t h = (esp_asrc_handle_t)1;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER, esp_asrc_open(&cfg, &h));
    TEST_ASSERT_NULL(h);
}
#endif  /* !CONFIG_SOC_ASRC_SUPPORTED */

static void open_branch_weight_matrix_valid(void)
{
    BRANCH_LOG("TC-O-05a", "weight_len == src_ch * dest_ch (1ch -> 2ch)");
    float w[2] = {1.0f, 1.0f};
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 2, .bits_per_sample = 16},
        .weight = w,
        .weight_len = 2,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_open(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);
    esp_asrc_close(h);
}

static void open_branch_weight_len_mismatch(void)
{
    BRANCH_LOG("TC-O-05b", "weight != NULL but weight_len != src_ch * dest_ch");
    float w[2] = {1.0f, 1.0f};
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 2, .bits_per_sample = 16},
        .weight = w,
        .weight_len = 99,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = NULL;
    esp_asrc_err_t r = esp_asrc_open(&cfg, &h);
    TEST_ASSERT_NULL(h);
    TEST_ASSERT_NOT_EQUAL(ESP_ASRC_ERR_OK, r);
}

static void open_branch_invalid_src_bits(void)
{
    BRANCH_LOG("TC-EP-01", "unsupported src bits_per_sample (7)");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 8000, .channel = 1, .bits_per_sample = 7},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = (void *)1;
    esp_asrc_err_t r = esp_asrc_open(&cfg, &h);
    TEST_ASSERT_NOT_EQUAL(ESP_ASRC_ERR_OK, r);
    TEST_ASSERT_NULL(h);
}

static void open_branch_src_rate_zero(void)
{
    BRANCH_LOG("TC-EP-02", "src sample_rate == 0");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 0, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = (void *)1;
    esp_asrc_err_t r = esp_asrc_open(&cfg, &h);
    TEST_ASSERT_NOT_EQUAL(ESP_ASRC_ERR_OK, r);
    TEST_ASSERT_NULL(h);
}

static void open_branch_src_channel_zero(void)
{
    BRANCH_LOG("TC-EP-03", "src channel == 0");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 8000, .channel = 0, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = (void *)1;
    esp_asrc_err_t r = esp_asrc_open(&cfg, &h);
    TEST_ASSERT_NOT_EQUAL(ESP_ASRC_ERR_OK, r);
    TEST_ASSERT_NULL(h);
}

static void open_branch_dest_channel_zero(void)
{
    BRANCH_LOG("TC-EP-04", "dest channel == 0");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 8000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 0, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = (void *)1;
    esp_asrc_err_t r = esp_asrc_open(&cfg, &h);
    TEST_ASSERT_NOT_EQUAL(ESP_ASRC_ERR_OK, r);
    TEST_ASSERT_NULL(h);
}

static void open_branch_invalid_dest_bits(void)
{
    BRANCH_LOG("TC-EP-05", "unsupported dest bits_per_sample (12)");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 8000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 12},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = (void *)1;
    esp_asrc_err_t r = esp_asrc_open(&cfg, &h);
    TEST_ASSERT_NOT_EQUAL(ESP_ASRC_ERR_OK, r);
    TEST_ASSERT_NULL(h);
}

static void open_branch_reopen_loop(void)
{
    BRANCH_LOG("TC-ST-02", "open-close-reopen with the same config");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 48000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 2,
        .timeout_ms = 1000,
    };
    for (int i = 0; i < 3; i++) {
        esp_asrc_handle_t h = NULL;
        TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_open(&cfg, &h));
        TEST_ASSERT_NOT_NULL(h);
        esp_asrc_close(h);
    }
}

static void get_out_sample_num_branch_null_handle(void)
{
    BRANCH_LOG("TC-G-01 / TC-ST-01", "NULL handle");
    uint32_t out_cnt = 0;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER,
                      esp_asrc_get_out_sample_num(NULL, 64, &out_cnt));
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER,
                      esp_asrc_get_out_sample_num(NULL, 1, &out_cnt));
}

static void get_out_sample_num_branch_null_out(void)
{
    BRANCH_LOG("TC-G-02", "NULL out_samples_cnt on a valid handle");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_open(&cfg, &h));
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER,
                      esp_asrc_get_out_sample_num(h, 64, NULL));
    esp_asrc_close(h);
}

static void get_bytes_per_sample_branch_null_handle(void)
{
    BRANCH_LOG("TC-G-04", "NULL handle");
    uint16_t ib = 0, ob = 0;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER,
                      esp_asrc_get_bytes_per_sample(NULL, &ib, &ob));
}

static void get_bytes_per_sample_branch_both_null_outputs(void)
{
    BRANCH_LOG("TC-G-05", "both output pointers NULL are allowed (optional)");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_open(&cfg, &h));
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_get_bytes_per_sample(h, NULL, NULL));
    esp_asrc_close(h);
}

static void get_bytes_per_sample_branch_non_bypass_values(void)
{
    BRANCH_LOG("TC-G-06", "non-bypass path: expect 2 bytes in, 6 bytes out");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 48000, .channel = 2, .bits_per_sample = 24},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 2,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_open(&cfg, &h));
    uint16_t ib = 0, ob = 0;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_get_bytes_per_sample(h, &ib, &ob));
    TEST_ASSERT_EQUAL(2, ib);  /* 16bit * 1ch */
    TEST_ASSERT_EQUAL(6, ob);  /* 24bit * 2ch */
    esp_asrc_close(h);
}

static void alloc_mem_branch_null_allocated_size(void)
{
    BRANCH_LOG("TC-M-01", "NULL allocated_size returns NULL");
    void *p = esp_asrc_align_alloc(64, 1, 1, NULL);
    TEST_ASSERT_NULL(p);
}

static void alloc_mem_branch_round_up_with_out_alignment(void)
{
    BRANCH_LOG("TC-M-02", "rounds size up when size_align > 1");
    esp_asrc_buffer_alignment_t al = {0};
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_get_buffer_alignment(&al));
    uint32_t allocated_size = 0;
    uint32_t req = 100;
    void *p = esp_asrc_align_alloc(req, al.outbuf_addr_align, al.outbuf_size_align, &allocated_size);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_GREATER_OR_EQUAL(req, allocated_size);
    free(p);
}

static void alloc_mem_branch_no_rounding_when_size_align_is_1(void)
{
    BRANCH_LOG("TC-M-03", "no rounding when size_align is 1");
    uint32_t sz = 0;
    void *p = esp_asrc_align_alloc(256, 1, 1, &sz);
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL(256u, sz);
    free(p);
}

static void get_buffer_alignment_branch_null(void)
{
    BRANCH_LOG("TC-BA-01", "NULL buffer_alignment pointer");
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER, esp_asrc_get_buffer_alignment(NULL));
}

static void get_buffer_alignment_branch_values(void)
{
    BRANCH_LOG("TC-BA-02", "alignment values follow current implementation");
    esp_asrc_buffer_alignment_t a = {0};
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_get_buffer_alignment(&a));
    TEST_ASSERT_EQUAL(1u, a.inbuf_addr_align);
    TEST_ASSERT_EQUAL(1u, a.inbuf_size_align);
    TEST_ASSERT_GREATER_OR_EQUAL(1u, a.outbuf_addr_align);
    TEST_ASSERT_EQUAL(a.outbuf_addr_align, a.outbuf_size_align);
    TEST_ASSERT_TRUE((a.outbuf_addr_align & (a.outbuf_addr_align - 1)) == 0);
}

static esp_asrc_handle_t open_bypass_handle_or_fail(void)
{
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_open(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);
    return h;
}

static void process_branch_null_handle(void)
{
    BRANCH_LOG("TC-P-01", "NULL handle");
    uint8_t inb[4], outb[4];
    uint32_t n = 1;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER,
                      esp_asrc_process(NULL, inb, 1, outb, &n));
}

static void process_branch_output_cap_too_small(void)
{
    BRANCH_LOG("TC-P-02", "bypass: output capacity smaller than input samples");
    esp_asrc_handle_t h = open_bypass_handle_or_fail();
    uint8_t inb[320];
    uint8_t outb[320];
    memset(inb, 0, sizeof(inb));
    uint32_t out_samples = 5;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER,
                      esp_asrc_process(h, inb, 100, outb, &out_samples));
    esp_asrc_close(h);
}

static void process_branch_null_in_buf(void)
{
    BRANCH_LOG("TC-P-04a", "bypass: NULL input buffer");
    esp_asrc_handle_t h = open_bypass_handle_or_fail();
    uint8_t outb[64];
    uint32_t n = 10;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER,
                      esp_asrc_process(h, NULL, 10, outb, &n));
    esp_asrc_close(h);
}

static void process_branch_null_out_buf(void)
{
    BRANCH_LOG("TC-P-04b", "bypass: NULL output buffer");
    esp_asrc_handle_t h = open_bypass_handle_or_fail();
    uint8_t inb[64];
    uint32_t n = 10;
    memset(inb, 0, sizeof(inb));
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER,
                      esp_asrc_process(h, inb, 10, NULL, &n));
    esp_asrc_close(h);
}

static void process_branch_null_out_sample_num_ptr(void)
{
    BRANCH_LOG("TC-P-04c", "bypass: NULL out_sample_num pointer");
    esp_asrc_handle_t h = open_bypass_handle_or_fail();
    uint8_t inb[64], outb[64];
    memset(inb, 0, sizeof(inb));
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_INVALID_PARAMETER,
                      esp_asrc_process(h, inb, 10, outb, NULL));
    esp_asrc_close(h);
}

static void process_branch_in_samples_zero(void)
{
    BRANCH_LOG("TC-BVA-01", "bypass: in_samples_num == 0");
    esp_asrc_handle_t h = open_bypass_handle_or_fail();
    uint8_t inb[4], outb[4];
    uint32_t n = 100;
    esp_asrc_err_t pr = esp_asrc_process(h, inb, 0, outb, &n);
    esp_asrc_close(h);
    TEST_ASSERT_TRUE(pr == ESP_ASRC_ERR_OK || pr == ESP_ASRC_ERR_FAIL);
}

static void process_branch_out_cap_equals_in_samples(void)
{
    BRANCH_LOG("TC-BVA-02", "bypass: output capacity == input sample count (equal-boundary)");
    esp_asrc_handle_t h = open_bypass_handle_or_fail();
    uint8_t inb[16], outb[16];
    for (int i = 0; i < (int)sizeof(inb); i++) {
        inb[i] = (uint8_t)(i + 1);
    }
    memset(outb, 0, sizeof(outb));
    uint32_t out_n = 8;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_process(h, inb, 8, outb, &out_n));
    TEST_ASSERT_EQUAL(8u, out_n);
    TEST_ASSERT_EQUAL(0, memcmp(inb, outb, sizeof(inb)));
    esp_asrc_close(h);
}

static void process_branch_resample_smoke_8k_to_16k(void)
{
    BRANCH_LOG("TC-A-01smoke", "SW_SPEED resample small block 8k->16k mono s16");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 8000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 2,
        .timeout_ms = 1000,
    };
    esp_asrc_handle_t h = NULL;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_open(&cfg, &h));
    TEST_ASSERT_NOT_NULL(h);
    uint32_t need_out = 0;
    const uint32_t in_samples = 128;
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_get_out_sample_num(h, in_samples, &need_out));
    uint16_t in_b = 0, out_b = 0;
    esp_asrc_get_bytes_per_sample(h, &in_b, &out_b);
    esp_asrc_buffer_alignment_t ial = {0};
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_get_buffer_alignment(&ial));
    uint32_t in_alloc = 0;
    uint32_t out_alloc = 0;
    uint8_t *in_buf = (uint8_t *)esp_asrc_align_alloc(in_samples * in_b, ial.inbuf_addr_align, ial.inbuf_size_align,
                                                      &in_alloc);
    uint8_t *out_buf = (uint8_t *)esp_asrc_align_alloc(need_out * out_b, ial.outbuf_addr_align, ial.outbuf_size_align,
                                                       &out_alloc);
    TEST_ASSERT_NOT_NULL(in_buf);
    TEST_ASSERT_NOT_NULL(out_buf);
    uint32_t out_n = need_out;
    esp_asrc_err_t pr = esp_asrc_process(h, in_buf, in_samples, out_buf, &out_n);
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, pr);
    TEST_ASSERT_GREATER_THAN(0u, out_n);
    free(in_buf);
    free(out_buf);
    esp_asrc_close(h);
}

static void process_branch_change_rate_only(void)
{
    BRANCH_LOG("TC-A-02a", "change sample rate only (16k->48k, 1ch s16)");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 48000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 2,
        .timeout_ms = 1000,
    };
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, plan_smoke_process(&cfg, 64, NULL));
}

static void process_branch_change_channel_only(void)
{
    BRANCH_LOG("TC-A-02b", "change channel count only (16k mono->16k stereo)");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 2, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_err_t e = plan_smoke_process(&cfg, 64, NULL);
    TEST_ASSERT_TRUE(e == ESP_ASRC_ERR_OK || e == ESP_ASRC_ERR_FAIL);
}

static void process_branch_change_bits_only(void)
{
    BRANCH_LOG("TC-A-02c", "change bits_per_sample only (16bit->8bit)");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 8},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, plan_smoke_process(&cfg, 64, NULL));
}

static void process_branch_complexity_1_and_3(void)
{
    BRANCH_LOG("TC-CMP-01", "SW resample with complexity 1 vs 3 both succeed");
    for (int c = 1; c <= 3; c += 2) {
        esp_asrc_cfg_t cfg = {
            .src_info = {.sample_rate = 8000, .channel = 1, .bits_per_sample = 16},
            .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
            .weight = NULL,
            .weight_len = 0,
            .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
            .complexity = (uint8_t)c,
            .timeout_ms = 1000,
        };
        TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, plan_smoke_process(&cfg, 128, NULL));
    }
}

static void process_branch_perf_sw_memory(void)
{
    BRANCH_LOG("TC-PERF-01", "SW_MEMORY opens and processes small block");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 8000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_SW_MEMORY,
        .complexity = 2,
        .timeout_ms = 1000,
    };
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, plan_smoke_process(&cfg, 64, NULL));
}

static void process_branch_perf_auto(void)
{
    BRANCH_LOG("TC-PERF-02", "AUTO opens for 8k->16k mono s16 (HW or SW per chip)");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 8000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_AUTO,
        .complexity = 2,
        .timeout_ms = 1000,
    };
    TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, plan_smoke_process(&cfg, 64, NULL));
}

static void process_branch_auto_same_rate(void)
{
    BRANCH_LOG("TC-DT-01", "AUTO with src_rate == dest_rate goes through SW path");
    esp_asrc_cfg_t cfg = {
        .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
        .dest_info = {.sample_rate = 16000, .channel = 2, .bits_per_sample = 16},
        .weight = NULL,
        .weight_len = 0,
        .perf_type = ESP_ASRC_PERF_TYPE_AUTO,
        .complexity = 1,
        .timeout_ms = 1000,
    };
    esp_asrc_err_t e = plan_smoke_process(&cfg, 64, NULL);
    TEST_ASSERT_TRUE(e == ESP_ASRC_ERR_OK || e == ESP_ASRC_ERR_FAIL);
}

static void close_branch_null_safe(void)
{
    BRANCH_LOG("TC-P-03", "esp_asrc_close(NULL) must be no-op / crash-free");
    esp_asrc_close(NULL);
}

static void close_branch_open_close_leak_loop(void)
{
    BRANCH_LOG("TC-N-03", "open/close loop keeps heap free size stable");
    size_t before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    for (int i = 0; i < 5; i++) {
        esp_asrc_cfg_t cfg = {
            .src_info = {.sample_rate = 16000, .channel = 1, .bits_per_sample = 16},
            .dest_info = {.sample_rate = 48000, .channel = 1, .bits_per_sample = 16},
            .weight = NULL,
            .weight_len = 0,
            .perf_type = ESP_ASRC_PERF_TYPE_SW_SPEED,
            .complexity = 2,
            .timeout_ms = 1000,
        };
        esp_asrc_handle_t h = NULL;
        TEST_ASSERT_EQUAL(ESP_ASRC_ERR_OK, esp_asrc_open(&cfg, &h));
        esp_asrc_close(h);
    }
    size_t after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    int diff = (int)(after - before);
    ESP_LOGI(TAG, "TC-N-03 heap diff (internal): %d", diff);
    TEST_ASSERT_LESS_OR_EQUAL(1024, abs(diff));
}

TEST_CASE("ASRC Branch Test", "[esp_asrc][leaks=400]")
{
    /* esp_asrc_open */
    open_branch_null_args();
    open_branch_invalid_perf_type();
    open_branch_bypass_identical();
#if !CONFIG_SOC_ASRC_SUPPORTED
    open_branch_hw_only_not_support();
#else
    open_branch_hw_only_smoke();
    open_branch_hw_timeout_zero();
#endif  /* !CONFIG_SOC_ASRC_SUPPORTED */
    open_branch_weight_matrix_valid();
    open_branch_weight_len_mismatch();
    open_branch_invalid_src_bits();
    open_branch_src_rate_zero();
    open_branch_src_channel_zero();
    open_branch_dest_channel_zero();
    open_branch_invalid_dest_bits();
    open_branch_reopen_loop();

    /* esp_asrc_get_out_sample_num */
    get_out_sample_num_branch_null_handle();
    get_out_sample_num_branch_null_out();

    /* esp_asrc_get_bytes_per_sample */
    get_bytes_per_sample_branch_null_handle();
    get_bytes_per_sample_branch_both_null_outputs();
    get_bytes_per_sample_branch_non_bypass_values();

    /* esp_asrc_get_buffer_alignment */
    get_buffer_alignment_branch_null();
    get_buffer_alignment_branch_values();

    /* esp_asrc_align_alloc */
    alloc_mem_branch_null_allocated_size();
    alloc_mem_branch_round_up_with_out_alignment();
    alloc_mem_branch_no_rounding_when_size_align_is_1();

    /* esp_asrc_process */
    /* Pure parameter-rejection / BVA branches on bypass handle */
    process_branch_null_handle();
    process_branch_output_cap_too_small();
    process_branch_null_in_buf();
    process_branch_null_out_buf();
    process_branch_null_out_sample_num_ptr();
    process_branch_in_samples_zero();
    process_branch_out_cap_equals_in_samples();

    /* End-to-end resample / single-dimension-change smokes */
    process_branch_resample_smoke_8k_to_16k();
    process_branch_change_rate_only();
    process_branch_change_channel_only();
    process_branch_change_bits_only();

    /* Decision-table coverage for perf_type and complexity */
    process_branch_complexity_1_and_3();
    process_branch_perf_sw_memory();
    process_branch_perf_auto();
    process_branch_auto_same_rate();

    /* esp_asrc_close */
    close_branch_null_safe();
    close_branch_open_close_leak_loop();
}
