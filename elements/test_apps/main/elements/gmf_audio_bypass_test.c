/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <string.h>
#include <stdint.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_element.h"
#include "esp_gmf_port.h"
#include "esp_gmf_info.h"
#include "esp_gmf_alc.h"
#include "esp_gmf_delay.h"
#include "esp_gmf_drc.h"
#include "esp_gmf_eq.h"
#include "esp_gmf_fade.h"
#include "esp_gmf_howl.h"
#include "esp_gmf_mbc.h"
#include "esp_gmf_reverb.h"
#include "esp_gmf_sonic.h"
#include "esp_gmf_audio_element.h"

static const char *TAG = "AUD_BYPASS_TEST";

#define BYPASS_TEST_BUF_MAX  (16 * 1024)

typedef struct {
    uint8_t *buf;
    int      buf_length;
    int      valid_size;
    bool     is_done;
} bypass_io_ctx_t;

typedef struct {
    const char *name;
    esp_gmf_err_t (*init)(esp_gmf_element_handle_t *handle, uint32_t *sr, uint8_t *ch, uint8_t *bits);
    void (*prepare)(esp_gmf_element_handle_t handle, uint8_t channels);
    bool  expect_process_changes;
} bypass_el_case_t;

static esp_gmf_err_io_t bypass_in_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    bypass_io_ctx_t *ctx = (bypass_io_ctx_t *)handle;
    load->buf = ctx->buf;
    load->buf_length = ctx->buf_length;
    load->valid_size = ctx->valid_size;
    load->is_done = ctx->is_done;
    load->pts = 0;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t bypass_out_acquire(void *handle, esp_gmf_payload_t *load, uint32_t wanted_size, int wait_ticks)
{
    bypass_io_ctx_t *ctx = (bypass_io_ctx_t *)handle;
    /* Shared ports pass the input payload here; keep that buffer for in-place passthrough. */
    if (load->buf == NULL) {
        load->buf = ctx->buf;
        load->buf_length = ctx->buf_length;
    }
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t bypass_port_release(void *handle, esp_gmf_payload_t *load, int wait_ticks)
{
    bypass_io_ctx_t *ctx = (bypass_io_ctx_t *)handle;
    if (ctx && load) {
        ctx->valid_size = load->valid_size;
    }
    return ESP_GMF_IO_OK;
}

static void fill_ramp(uint8_t *buf, int bytes)
{
    int16_t *samples = (int16_t *)buf;
    int n = bytes / (int)sizeof(int16_t);
    for (int i = 0; i < n; i++) {
        samples[i] = (int16_t)(1200 + i * 13);
    }
}

static void report_sound_info(esp_gmf_element_handle_t handle, uint32_t sr, uint8_t ch, uint8_t bits)
{
    esp_gmf_info_sound_t sound_info = {
        .sample_rates = sr,
        .channels = ch,
        .bits = bits,
    };
    esp_gmf_event_pkt_t sound_event = {
        .from = handle,
        .type = ESP_GMF_EVT_TYPE_REPORT_INFO,
        .sub = ESP_GMF_INFO_SOUND,
        .payload = &sound_info,
        .payload_size = sizeof(sound_info),
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, ESP_GMF_ELEMENT_GET(handle)->ops.event_receiver(&sound_event, handle));
}

static void run_one_process(esp_gmf_element_handle_t handle)
{
    esp_gmf_job_err_t job = ESP_GMF_JOB_ERR_CONTINUE;
    int tries = 0;
    while ((job == ESP_GMF_JOB_ERR_CONTINUE) && (tries++ < 8)) {
        job = esp_gmf_element_process_running(handle, NULL);
    }
    TEST_ASSERT_TRUE(job == ESP_GMF_JOB_ERR_OK || job == ESP_GMF_JOB_ERR_DONE || job == ESP_GMF_JOB_ERR_TRUNCATE);
}

static void test_element_module_bypass(const bypass_el_case_t *c, bool shared)
{
    esp_gmf_element_handle_t handle = NULL;
    uint32_t sr = 0;
    uint8_t ch = 0;
    uint8_t bits = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, c->init(&handle, &sr, &ch, &bits));
    TEST_ASSERT_NOT_NULL(handle);

    uint8_t *in_buf = (uint8_t *)esp_gmf_oal_malloc_align(16, BYPASS_TEST_BUF_MAX);
    uint8_t *out_buf = (uint8_t *)esp_gmf_oal_malloc_align(16, BYPASS_TEST_BUF_MAX);
    uint8_t *ref_buf = (uint8_t *)esp_gmf_oal_malloc_align(16, BYPASS_TEST_BUF_MAX);
    TEST_ASSERT_NOT_NULL(in_buf);
    TEST_ASSERT_NOT_NULL(out_buf);
    TEST_ASSERT_NOT_NULL(ref_buf);

    bypass_io_ctx_t in_ctx = {
        .buf = in_buf,
        .buf_length = BYPASS_TEST_BUF_MAX,
    };
    bypass_io_ctx_t out_ctx = {
        .buf = out_buf,
        .buf_length = BYPASS_TEST_BUF_MAX,
    };

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(bypass_in_acquire, bypass_port_release,
                                                              NULL, &in_ctx, BYPASS_TEST_BUF_MAX, ESP_GMF_MAX_DELAY);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(bypass_out_acquire, bypass_port_release,
                                                                NULL, &out_ctx, BYPASS_TEST_BUF_MAX, ESP_GMF_MAX_DELAY);
    TEST_ASSERT_NOT_NULL(in_port);
    TEST_ASSERT_NOT_NULL(out_port);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_in_port(handle, in_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(handle, out_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_port_enable_payload_share(in_port, shared));

    report_sound_info(handle, sr, ch, bits);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_open(handle, NULL));
    if (c->prepare) {
        c->prepare(handle, ch);
    }
    bool enable = true;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_bypass(handle, &enable));
    TEST_ASSERT_FALSE(enable);

    int frame_bytes = ESP_GMF_ELEMENT_GET(handle)->in_attr.data_size;
    TEST_ASSERT_TRUE(frame_bytes > 0);
    TEST_ASSERT_TRUE(frame_bytes <= BYPASS_TEST_BUF_MAX);
    fill_ramp(in_buf, frame_bytes);
    memcpy(ref_buf, in_buf, frame_bytes);
    in_ctx.valid_size = frame_bytes;
    in_ctx.is_done = false;
    memset(out_buf, 0xA5, BYPASS_TEST_BUF_MAX);
    out_ctx.valid_size = 0;

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_set_bypass(handle, true));
    enable = false;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_bypass(handle, &enable));
    TEST_ASSERT_TRUE(enable);
    run_one_process(handle);

    const uint8_t *bypass_out = shared ? in_buf : out_buf;
    int bypass_out_size = shared ? in_ctx.valid_size : out_ctx.valid_size;
    TEST_ASSERT_EQUAL(frame_bytes, bypass_out_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref_buf, bypass_out, frame_bytes);

    /* Sonic changes duration, so shared in-place processing is not a valid
     * contrast against bypass. Still check bypass passthrough on the shared path. */
    if (c->expect_process_changes && !(shared && strcmp(c->name, "aud_sonic") == 0)) {
        fill_ramp(in_buf, frame_bytes);
        memcpy(ref_buf, in_buf, frame_bytes);
        in_ctx.valid_size = frame_bytes;
        memset(out_buf, 0x5A, BYPASS_TEST_BUF_MAX);
        out_ctx.valid_size = 0;
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_set_bypass(handle, false));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_get_bypass(handle, &enable));
        TEST_ASSERT_FALSE(enable);
        run_one_process(handle);
        const uint8_t *proc_out = shared ? in_buf : out_buf;
        int proc_out_size = shared ? in_ctx.valid_size : out_ctx.valid_size;
        TEST_ASSERT_TRUE((proc_out_size != frame_bytes) || (memcmp(ref_buf, proc_out, frame_bytes) != 0));
    }

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_close(handle, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));
    esp_gmf_oal_free(in_buf);
    esp_gmf_oal_free(out_buf);
    esp_gmf_oal_free(ref_buf);
    ESP_LOGI(TAG, "%s bypass %s path passed", c->name, shared ? "shared" : "unshared");
}

static esp_gmf_err_t init_alc(esp_gmf_element_handle_t *handle, uint32_t *sr, uint8_t *ch, uint8_t *bits)
{
    esp_ae_alc_cfg_t cfg = DEFAULT_ESP_GMF_ALC_CONFIG();
    *sr = cfg.sample_rate;
    *ch = cfg.channel;
    *bits = cfg.bits_per_sample;
    return esp_gmf_alc_init(&cfg, handle);
}

static void prepare_alc(esp_gmf_element_handle_t handle, uint8_t channels)
{
    for (uint8_t i = 0; i < channels; i++) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_alc_set_gain(handle, i, 20));
    }
}

static esp_gmf_err_t init_delay(esp_gmf_element_handle_t *handle, uint32_t *sr, uint8_t *ch, uint8_t *bits)
{
    esp_ae_delay_cfg_t cfg = DEFAULT_ESP_GMF_DELAY_CONFIG();
    *sr = cfg.sample_rate;
    *ch = cfg.channel;
    *bits = cfg.bits_per_sample;
    return esp_gmf_delay_init(&cfg, handle);
}

static esp_gmf_err_t init_drc(esp_gmf_element_handle_t *handle, uint32_t *sr, uint8_t *ch, uint8_t *bits)
{
    esp_ae_drc_cfg_t cfg = DEFAULT_ESP_GMF_DRC_CONFIG();
    *sr = cfg.sample_rate;
    *ch = cfg.channel;
    *bits = cfg.bits_per_sample;
    return esp_gmf_drc_init(&cfg, handle);
}

static esp_gmf_err_t init_eq(esp_gmf_element_handle_t *handle, uint32_t *sr, uint8_t *ch, uint8_t *bits)
{
    esp_ae_eq_cfg_t cfg = DEFAULT_ESP_GMF_EQ_CONFIG();
    *sr = cfg.sample_rate;
    *ch = cfg.channel;
    *bits = cfg.bits_per_sample;
    return esp_gmf_eq_init(&cfg, handle);
}

static void prepare_eq(esp_gmf_element_handle_t handle, uint8_t channels)
{
    (void)channels;
    esp_ae_eq_filter_para_t para = {
        .filter_type = ESP_AE_EQ_FILTER_LOW_SHELF,
        .fc = 200,
        .q = 0.7f,
        .gain = 12.0f,
    };
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_eq_set_para(handle, 0, &para));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_eq_enable_filter(handle, 0, true));
}

static esp_gmf_err_t init_fade(esp_gmf_element_handle_t *handle, uint32_t *sr, uint8_t *ch, uint8_t *bits)
{
    esp_ae_fade_cfg_t cfg = DEFAULT_ESP_GMF_FADE_CONFIG();
    *sr = cfg.sample_rate;
    *ch = cfg.channel;
    *bits = cfg.bits_per_sample;
    return esp_gmf_fade_init(&cfg, handle);
}

static esp_gmf_err_t init_howl(esp_gmf_element_handle_t *handle, uint32_t *sr, uint8_t *ch, uint8_t *bits)
{
    esp_ae_howl_cfg_t cfg = DEFAULT_ESP_GMF_HOWL_CONFIG();
    *sr = cfg.sample_rate;
    *ch = cfg.channel;
    *bits = cfg.bits_per_sample;
    return esp_gmf_howl_init(&cfg, handle);
}

static esp_gmf_err_t init_mbc(esp_gmf_element_handle_t *handle, uint32_t *sr, uint8_t *ch, uint8_t *bits)
{
    esp_ae_mbc_config_t cfg = DEFAULT_ESP_GMF_MBC_CONFIG();
    *sr = cfg.sample_rate;
    *ch = cfg.channel;
    *bits = cfg.bits_per_sample;
    return esp_gmf_mbc_init(&cfg, handle);
}

static void prepare_mbc(esp_gmf_element_handle_t handle, uint8_t channels)
{
    (void)channels;
    esp_ae_mbc_para_t para = {0};
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_mbc_get_para(handle, 0, &para));
    para.makeup_gain = 10.0f;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_mbc_set_para(handle, 0, &para));
}

static esp_gmf_err_t init_reverb(esp_gmf_element_handle_t *handle, uint32_t *sr, uint8_t *ch, uint8_t *bits)
{
    esp_ae_reverb_cfg_t cfg = DEFAULT_ESP_GMF_REVERB_CONFIG();
    *sr = cfg.sample_rate;
    *ch = cfg.channel;
    *bits = cfg.bits_per_sample;
    return esp_gmf_reverb_init(&cfg, handle);
}

static esp_gmf_err_t init_sonic(esp_gmf_element_handle_t *handle, uint32_t *sr, uint8_t *ch, uint8_t *bits)
{
    esp_ae_sonic_cfg_t cfg = DEFAULT_ESP_GMF_SONIC_CONFIG();
    *sr = cfg.sample_rate;
    *ch = cfg.channel;
    *bits = cfg.bits_per_sample;
    return esp_gmf_sonic_init(&cfg, handle);
}

static void prepare_sonic(esp_gmf_element_handle_t handle, uint8_t channels)
{
    (void)channels;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_sonic_set_speed(handle, 1.5f));
}

TEST_CASE("Audio element module bypass process", "[ESP_GMF_AUDIO][ESP_GMF_IF_CHECK][leaks=1400]")
{
    const bypass_el_case_t cases[] = {
        {"aud_alc", init_alc, prepare_alc, true},
        {"aud_delay", init_delay, NULL, true},
        {"aud_drc", init_drc, NULL, false},
        {"aud_eq", init_eq, prepare_eq, true},
        {"aud_fade", init_fade, NULL, true},
        {"aud_howl", init_howl, NULL, false},
        {"aud_mbc", init_mbc, prepare_mbc, true},
        {"aud_reverb", init_reverb, NULL, true},
        {"aud_sonic", init_sonic, prepare_sonic, true},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        test_element_module_bypass(&cases[i], false);
        if (strcmp(cases[i].name, "aud_howl") != 0) {
            test_element_module_bypass(&cases[i], true);
        }
    }
}

TEST_CASE("Sonic module bypass after leftover TRUNCATE", "[ESP_GMF_AUDIO][ESP_GMF_IF_CHECK][leaks=1400]")
{
    esp_gmf_element_handle_t handle = NULL;
    uint32_t sr = 0;
    uint8_t ch = 0;
    uint8_t bits = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, init_sonic(&handle, &sr, &ch, &bits));
    TEST_ASSERT_NOT_NULL(handle);

    uint8_t *in_buf = (uint8_t *)esp_gmf_oal_malloc_align(16, BYPASS_TEST_BUF_MAX);
    uint8_t *out_buf = (uint8_t *)esp_gmf_oal_malloc_align(16, BYPASS_TEST_BUF_MAX);
    uint8_t *ref_buf = (uint8_t *)esp_gmf_oal_malloc_align(16, BYPASS_TEST_BUF_MAX);
    TEST_ASSERT_NOT_NULL(in_buf);
    TEST_ASSERT_NOT_NULL(out_buf);
    TEST_ASSERT_NOT_NULL(ref_buf);

    bypass_io_ctx_t in_ctx = {
        .buf = in_buf,
        .buf_length = BYPASS_TEST_BUF_MAX,
    };
    bypass_io_ctx_t out_ctx = {
        .buf = out_buf,
        .buf_length = BYPASS_TEST_BUF_MAX,
    };
    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BLOCK(bypass_in_acquire, bypass_port_release,
                                                              NULL, &in_ctx, BYPASS_TEST_BUF_MAX, ESP_GMF_MAX_DELAY);
    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BLOCK(bypass_out_acquire, bypass_port_release,
                                                                NULL, &out_ctx, BYPASS_TEST_BUF_MAX, ESP_GMF_MAX_DELAY);
    TEST_ASSERT_NOT_NULL(in_port);
    TEST_ASSERT_NOT_NULL(out_port);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_in_port(handle, in_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(handle, out_port));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_port_enable_payload_share(in_port, false));

    report_sound_info(handle, sr, ch, bits);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_open(handle, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_sonic_set_speed(handle, 2.0f));

    /* Default in_attr (768 B) is smaller than sonic's 10 ms output request, so speed 2.0
     * never fills the output buffer and never returns TRUNCATE. Feed enough input that one
     * process can satisfy needed_num and leave leftover AE output. */
    int bytes_per_sample = (bits >> 3) * ch;
    int needed_out_bytes = 10 * (int)sr * bytes_per_sample / 1000;
    int frame_bytes = needed_out_bytes * 4;
    if (frame_bytes > BYPASS_TEST_BUF_MAX) {
        frame_bytes = BYPASS_TEST_BUF_MAX;
    }
    ESP_GMF_ELEMENT_GET(handle)->in_attr.data_size = frame_bytes;
    TEST_ASSERT_TRUE(frame_bytes > 0);
    TEST_ASSERT_TRUE(frame_bytes <= BYPASS_TEST_BUF_MAX);
    fill_ramp(in_buf, frame_bytes);
    in_ctx.valid_size = frame_bytes;
    in_ctx.is_done = false;

    bool got_truncate = false;
    for (int i = 0; i < 32; i++) {
        memset(out_buf, 0xA5, BYPASS_TEST_BUF_MAX);
        out_ctx.valid_size = 0;
        esp_gmf_job_err_t job = esp_gmf_element_process_running(handle, NULL);
        if (job == ESP_GMF_JOB_ERR_TRUNCATE) {
            got_truncate = true;
            break;
        }
        TEST_ASSERT_TRUE(job == ESP_GMF_JOB_ERR_OK || job == ESP_GMF_JOB_ERR_CONTINUE);
    }
    TEST_ASSERT_TRUE(got_truncate);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_set_bypass(handle, true));
    for (int i = 0; i < 32; i++) {
        memset(out_buf, 0xA5, BYPASS_TEST_BUF_MAX);
        out_ctx.valid_size = 0;
        esp_gmf_job_err_t job = esp_gmf_element_process_running(handle, NULL);
        if (job != ESP_GMF_JOB_ERR_TRUNCATE) {
            TEST_ASSERT_TRUE(job == ESP_GMF_JOB_ERR_OK || job == ESP_GMF_JOB_ERR_CONTINUE || job == ESP_GMF_JOB_ERR_DONE);
            break;
        }
    }

    fill_ramp(in_buf, frame_bytes);
    memcpy(ref_buf, in_buf, frame_bytes);
    in_ctx.valid_size = frame_bytes;
    memset(out_buf, 0x5A, BYPASS_TEST_BUF_MAX);
    out_ctx.valid_size = 0;
    run_one_process(handle);
    TEST_ASSERT_EQUAL(frame_bytes, out_ctx.valid_size);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(ref_buf, out_buf, frame_bytes);

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_process_close(handle, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(handle));
    esp_gmf_oal_free(in_buf);
    esp_gmf_oal_free(out_buf);
    esp_gmf_oal_free(ref_buf);
}
