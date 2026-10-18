/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>
#include "unity.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_gmf_afe.h"
#include "esp_gmf_port.h"

#if defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4)

typedef struct {
    EventGroupHandle_t  events;
    size_t              callback_bytes;
    size_t              port_bytes;
    bool                invalid_pcm;
} afe_output_test_t;

static void afe_output_test_callback(const int16_t *data, size_t bytes, void *ctx)
{
    afe_output_test_t *test = ctx;
    test->invalid_pcm |= data == NULL || bytes == 0 || bytes % sizeof(int16_t) != 0;
    test->callback_bytes += bytes;
    xEventGroupSetBits(test->events, BIT0);
}

static esp_gmf_err_io_t afe_output_test_read(void *ctx, esp_gmf_payload_t *load, int wanted, int ticks)
{
    memset(load->buf, 0, wanted);
    load->valid_size = wanted;
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t afe_output_test_release_read(void *ctx, esp_gmf_payload_t *load, int ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t afe_output_test_acquire_write(void *ctx, esp_gmf_payload_t *load, int wanted, int ticks)
{
    return ESP_GMF_IO_OK;
}

static esp_gmf_err_io_t afe_output_test_release_write(void *ctx, esp_gmf_payload_t *load, int ticks)
{
    afe_output_test_t *test = ctx;
    test->port_bytes += load->valid_size;
    return ESP_GMF_IO_OK;
}

TEST_CASE("Test AFE immediate output and normal output after reopen", "[ESP_GMF_AFE][leaks=1400]")
{
    afe_output_test_t test = {.events = xEventGroupCreate()};
    TEST_ASSERT_NOT_NULL(test.events);
    afe_config_t *afe_cfg = afe_config_init("MR", NULL, AFE_TYPE_SR, AFE_MODE_HIGH_PERF);
    TEST_ASSERT_NOT_NULL(afe_cfg);
    afe_cfg->wakenet_init = false;
    afe_cfg->vad_init = false;
    afe_cfg->aec_init = false;
    esp_gmf_afe_manager_cfg_t manager_cfg = DEFAULT_GMF_AFE_MANAGER_CFG(afe_cfg, NULL, NULL, NULL, NULL);
    esp_gmf_afe_manager_handle_t manager = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_create(&manager_cfg, &manager));

    esp_gmf_afe_cfg_t cfg = DEFAULT_GMF_AFE_CFG(manager, NULL, NULL, NULL);
    esp_gmf_element_handle_t element = NULL;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_init(&cfg, &element));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_afe_set_output_callback(NULL, afe_output_test_callback, &test));
    // The default delay cannot be silently discarded by selecting direct output.
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_afe_set_output_callback(element, afe_output_test_callback, &test));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_set_output_callback(element, NULL, NULL));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(element));

    cfg.delay_samples = 0;
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_init(&cfg, &element));
    esp_gmf_port_handle_t input = NEW_ESP_GMF_PORT_IN_BYTE(afe_output_test_read, afe_output_test_release_read,
                                                           NULL, &test, 1024, 100);
    esp_gmf_port_handle_t output = NEW_ESP_GMF_PORT_OUT_BYTE(afe_output_test_acquire_write, afe_output_test_release_write,
                                                             NULL, &test, 1024, 100);
    TEST_ASSERT_NOT_NULL(input);
    TEST_ASSERT_NOT_NULL(output);
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_in_port(element, input));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_element_register_out_port(element, output));

    // Reuse the same element: direct output, legacy output, then direct again.
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        bool direct = cycle != 1;
        test.callback_bytes = 0;
        test.port_bytes = 0;
        test.invalid_pcm = false;
        xEventGroupClearBits(test.events, BIT0);
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK,
                          esp_gmf_afe_set_output_callback(element, direct ? afe_output_test_callback : NULL, &test));
        TEST_ASSERT_EQUAL(ESP_GMF_JOB_ERR_OK, esp_gmf_element_process_open(element, NULL));
        TEST_ASSERT_EQUAL(ESP_GMF_ERR_INVALID_ARG, esp_gmf_afe_set_output_callback(element, NULL, NULL));
        for (unsigned frame = 0; frame < 48; ++frame) {
            esp_gmf_job_err_t ret = esp_gmf_element_process_running(element, NULL);
            TEST_ASSERT_TRUE(ret == ESP_GMF_JOB_ERR_OK || ret == ESP_GMF_JOB_ERR_CONTINUE);
        }
        EventBits_t bits = 0;
        if (direct) {
            bits = xEventGroupWaitBits(test.events, BIT0, false, true, pdMS_TO_TICKS(1000));
        }
        // The result callback lock is a barrier before inspecting callback-owned state.
        TEST_ASSERT_EQUAL(ESP_GMF_JOB_ERR_OK, esp_gmf_element_process_close(element, NULL));
        TEST_ASSERT_FALSE(test.invalid_pcm);
        if (direct) {
            TEST_ASSERT_BITS(BIT0, BIT0, bits);
            TEST_ASSERT_GREATER_THAN(0, test.callback_bytes);
            TEST_ASSERT_EQUAL(0, test.port_bytes);
        } else {
            TEST_ASSERT_EQUAL(0, test.callback_bytes);
            TEST_ASSERT_GREATER_THAN(0, test.port_bytes);
        }
    }

    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_obj_delete(element));
    TEST_ASSERT_EQUAL(ESP_GMF_ERR_OK, esp_gmf_afe_manager_destroy(manager));
    afe_config_free(afe_cfg);
    vEventGroupDelete(test.events);
}

#endif  /* defined(CONFIG_IDF_TARGET_ESP32) || defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32P4) */
