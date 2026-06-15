/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include <stdint.h>
#include "unity.h"
#include "esp_log.h"
#include "esp_gmf_audio_element.h"
#include "esp_gmf_element.h"
#include "esp_gmf_oal_mem.h"
#include "esp_gmf_payload.h"
#include "esp_gmf_pipeline.h"
#include "esp_gmf_port.h"

static const char *TAG = "TEST_GMF_PORT_ALIGN";

#define TEST_ELEMENT1_IN_SIZE   (1025)
#define TEST_ELEMENT1_OUT_SIZE  (1025)
#define TEST_ELEMENT2_SIZE      (512)
#define TEST_ADDR_ALIGN         (16)
#define TEST_SIZE_ALIGN         (64)

typedef struct {
    esp_gmf_audio_element_t  parent;
    bool                     check_align;
} port_align_test_el_t;

typedef struct {
    const char  *tag;
    uint32_t     in_size;
    uint32_t     out_size;
    uint8_t      in_addr_align;
    uint8_t      in_size_align;
    uint8_t      out_addr_align;
    uint8_t      out_size_align;
    bool         check_align;
} port_align_test_el_cfg_t;

static esp_gmf_err_io_t port_align_test_release(void *handle, esp_gmf_payload_t *load, int wait_ticks)
{
    (void)handle;
    (void)load;
    (void)wait_ticks;
    return ESP_GMF_IO_OK;
}

static bool port_align_payload_check(const char *tag, const char *port_name, esp_gmf_payload_t *load,
                                     uint8_t addr_align, uint8_t size_align)
{
    if ((load == NULL) || (load->buf == NULL)) {
        ESP_LOGE(TAG, "%s %s payload is invalid, load:%p", tag, port_name, load);
        return false;
    }
    if ((addr_align > 1) && (((uintptr_t)load->buf & (addr_align - 1)) != 0)) {
        ESP_LOGE(TAG, "%s %s payload address is not aligned, buf:%p, align:%u", tag, port_name, load->buf, (unsigned)addr_align);
        return false;
    }
    if ((size_align > 1) && ((load->buf_length & (size_align - 1)) != 0)) {
        ESP_LOGE(TAG, "%s %s payload size is not aligned, len:%u, align:%u", tag, port_name, (unsigned)load->buf_length, (unsigned)size_align);
        return false;
    }
    return true;
}

static esp_gmf_job_err_t port_align_test_process(void *self, void *para)
{
    (void)para;
    esp_gmf_element_t *el = ESP_GMF_ELEMENT_GET(self);
    port_align_test_el_t *test_el = (port_align_test_el_t *)self;
    esp_gmf_payload_t *in_load = NULL;
    esp_gmf_payload_t *out_load = NULL;
    esp_gmf_job_err_t job_ret = ESP_GMF_JOB_ERR_OK;

    esp_gmf_err_io_t io_ret = esp_gmf_port_acquire_in(el->in, &in_load, el->in_attr.data_size, ESP_GMF_MAX_DELAY);
    if (io_ret < ESP_GMF_IO_OK) {
        ESP_LOGE(TAG, "%s acquire in failed, ret:%d", OBJ_GET_TAG(el), io_ret);
        return ESP_GMF_JOB_ERR_FAIL;
    }
    if (test_el->check_align
        && (port_align_payload_check(OBJ_GET_TAG(el), "in", in_load,
                                     el->in_attr.port.buf_addr_aligned, el->in_attr.port.buf_size_aligned) == false)) {
        job_ret = ESP_GMF_JOB_ERR_FAIL;
        goto PROCESS_EXIT;
    }

    io_ret = esp_gmf_port_acquire_out(el->out, &out_load, el->out_attr.data_size, ESP_GMF_MAX_DELAY);
    if (io_ret < ESP_GMF_IO_OK) {
        ESP_LOGE(TAG, "%s acquire out failed, ret:%d", OBJ_GET_TAG(el), io_ret);
        job_ret = ESP_GMF_JOB_ERR_FAIL;
        goto PROCESS_EXIT;
    }

    if (test_el->check_align
        && (port_align_payload_check(OBJ_GET_TAG(el), "out", out_load,
                                     el->out_attr.port.buf_addr_aligned, el->out_attr.port.buf_size_aligned) == false)) {
        job_ret = ESP_GMF_JOB_ERR_FAIL;
        goto PROCESS_EXIT;
    }

    out_load->valid_size = el->out_attr.data_size;
    out_load->is_done = in_load->is_done;

PROCESS_EXIT:
    if (out_load) {
        esp_gmf_port_release_out(el->out, out_load, ESP_GMF_MAX_DELAY);
    }
    if (in_load) {
        esp_gmf_port_release_in(el->in, in_load, ESP_GMF_MAX_DELAY);
    }
    return job_ret;
}

static esp_gmf_err_t port_align_test_destroy(esp_gmf_obj_handle_t obj)
{
    port_align_test_el_t *el = (port_align_test_el_t *)obj;
    esp_gmf_audio_el_deinit(el);
    esp_gmf_oal_free(el);
    return ESP_GMF_ERR_OK;
}

static esp_gmf_err_t port_align_test_el_init(const char *tag, uint32_t in_size, uint32_t out_size,
                                             uint8_t in_addr_align, uint8_t in_size_align,
                                             uint8_t out_addr_align, uint8_t out_size_align,
                                             bool check_align, esp_gmf_element_handle_t *handle)
{
    TEST_ASSERT_NOT_NULL(handle);
    *handle = NULL;

    port_align_test_el_t *test_el = esp_gmf_oal_calloc(1, sizeof(port_align_test_el_t));
    TEST_ASSERT_NOT_NULL(test_el);

    esp_gmf_obj_t *obj = (esp_gmf_obj_t *)test_el;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_set_tag(obj, tag));
    obj->del_obj = port_align_test_destroy;
    test_el->check_align = check_align;

    esp_gmf_element_cfg_t el_cfg = {0};
    ESP_GMF_ELEMENT_IN_PORT_ATTR_SET(el_cfg.in_attr, ESP_GMF_EL_PORT_CAP_SINGLE, in_addr_align, in_size_align,
                                     ESP_GMF_PORT_TYPE_BYTE, in_size);
    ESP_GMF_ELEMENT_OUT_PORT_ATTR_SET(el_cfg.out_attr, ESP_GMF_EL_PORT_CAP_SINGLE, out_addr_align, out_size_align,
                                      ESP_GMF_PORT_TYPE_BYTE, out_size);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_audio_el_init(test_el, &el_cfg));
    ESP_GMF_ELEMENT_GET(test_el)->ops.process = port_align_test_process;

    esp_gmf_port_handle_t in_port = NEW_ESP_GMF_PORT_IN_BYTE(NULL, NULL, NULL, NULL, in_size, ESP_GMF_MAX_DELAY);
    TEST_ASSERT_NOT_NULL(in_port);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_in_port(test_el, in_port));

    esp_gmf_port_handle_t out_port = NEW_ESP_GMF_PORT_OUT_BYTE(NULL, port_align_test_release, NULL, NULL, out_size, ESP_GMF_MAX_DELAY);
    TEST_ASSERT_NOT_NULL(out_port);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(test_el, out_port));

    *handle = test_el;
    return ESP_GMF_ERR_OK;
}

static void port_align_test_preload_payload(esp_gmf_port_handle_t port, uint8_t addr_align, uint8_t size_align, uint32_t size)
{
    esp_gmf_payload_t *load = NULL;

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_payload_new(&load));
    TEST_ASSERT_NOT_NULL(load);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_payload_realloc_buffer_with_separate_alignment(load, addr_align, size_align, size));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_port_set_payload(port, load));
}

static void port_align_test_run_pipeline(const port_align_test_el_cfg_t el_cfgs[3])
{
    esp_gmf_pipeline_handle_t pipeline = NULL;
    esp_gmf_element_handle_t elements[3] = {NULL};

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, port_align_test_el_init(el_cfgs[i].tag, el_cfgs[i].in_size, el_cfgs[i].out_size,
                                                                 el_cfgs[i].in_addr_align, el_cfgs[i].in_size_align,
                                                                 el_cfgs[i].out_addr_align, el_cfgs[i].out_size_align,
                                                                 el_cfgs[i].check_align, &elements[i]));
    }

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_create(&pipeline));
    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_register_el(pipeline, elements[i]));
    }

    port_align_test_preload_payload(ESP_GMF_ELEMENT_GET(elements[0])->out, 1, 1, TEST_ELEMENT1_OUT_SIZE);
    port_align_test_preload_payload(ESP_GMF_ELEMENT_GET(elements[1])->out, 1, 1, TEST_ELEMENT1_OUT_SIZE);

    for (int i = 0; i < 3; i++) {
        TEST_ASSERT_EQUAL(ESP_GMF_JOB_ERR_OK, esp_gmf_element_process_running(elements[i], NULL));
    }

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_pipeline_destroy(pipeline));
}

TEST_CASE("GMF port realloc keeps middle element payload alignment", "[ELEMENT_PORT]")
{
    esp_log_level_set("*", ESP_LOG_INFO);

    const port_align_test_el_cfg_t el_cfgs[3] = {
        {"element1_head", TEST_ELEMENT1_IN_SIZE, TEST_ELEMENT1_OUT_SIZE, 1, 1, 1, 1, false},
        {"element2", TEST_ELEMENT2_SIZE, TEST_ELEMENT2_SIZE,
         TEST_ADDR_ALIGN, TEST_SIZE_ALIGN, TEST_ADDR_ALIGN, TEST_SIZE_ALIGN, true},
        {"element1_tail", TEST_ELEMENT1_IN_SIZE, TEST_ELEMENT1_OUT_SIZE, 1, 1, 1, 1, false},
    };

    port_align_test_run_pipeline(el_cfgs);
}

TEST_CASE("GMF port realloc handles aligned head out and middle ports", "[ELEMENT_PORT]")
{
    esp_log_level_set("*", ESP_LOG_INFO);

    const port_align_test_el_cfg_t el_cfgs[3] = {
        {"element1_head", TEST_ELEMENT1_IN_SIZE, TEST_ELEMENT1_OUT_SIZE,
         1, 1, TEST_ADDR_ALIGN, TEST_SIZE_ALIGN, true},
        {"element2", TEST_ELEMENT2_SIZE, TEST_ELEMENT2_SIZE,
         TEST_ADDR_ALIGN, TEST_SIZE_ALIGN, TEST_ADDR_ALIGN, TEST_SIZE_ALIGN, true},
        {"element1_tail", TEST_ELEMENT1_IN_SIZE, TEST_ELEMENT1_OUT_SIZE, 1, 1, 1, 1, false},
    };

    port_align_test_run_pipeline(el_cfgs);
}

TEST_CASE("GMF port realloc handles aligned tail input and output", "[ELEMENT_PORT]")
{
    esp_log_level_set("*", ESP_LOG_INFO);

    const port_align_test_el_cfg_t el_cfgs[3] = {
        {"element1_head", TEST_ELEMENT1_IN_SIZE, TEST_ELEMENT1_OUT_SIZE, 1, 1, 1, 1, false},
        {"element2", TEST_ELEMENT2_SIZE, TEST_ELEMENT2_SIZE, 1, 1, 1, 1, false},
        {"element1_tail", TEST_ELEMENT1_IN_SIZE, TEST_ELEMENT1_OUT_SIZE,
         TEST_ADDR_ALIGN, TEST_SIZE_ALIGN, TEST_ADDR_ALIGN, TEST_SIZE_ALIGN, true},
    };

    port_align_test_run_pipeline(el_cfgs);
}
