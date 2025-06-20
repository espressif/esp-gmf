/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

/**
 * @file esp_gmf_app_unit_test.c
 * @brief  GMF Application Unit Test Utilities
 *
 *         This file provides comprehensive unit testing utilities for ESP-GMF applications,
 *         offering enhanced memory leak detection, test setup/teardown functionality, and
 *         component-specific testing support.
 *
 *         Key Features:
 *         - Multi-level memory leak detection (warning/critical thresholds)
 *         - Component-specific leak categorization (General/LWIP/NVS)
 *         - Flexible leak checking modes (default/custom/disabled)
 *         - Heap corruption detection and reporting
 *         - TCP/IP stack initialization for network tests
 *         - Unity test framework integration
 *         - Entry point for the test application
 *
 *         Usage:
 *         - setUp() and tearDown() are automatically called before/after each test
 *         - Call esp_gmf_app_test_case_uses_tcpip() in tests that require network functionality
 *         - Use test annotations like [leaks] or [leaks=1024] to control leak checking
 *         - Call esp_gmf_app_test_main() to start the test application
 *
 *         Memory Leak Detection:
 *         The utility tracks memory usage before and after test execution, comparing
 *         against configurable thresholds. Different components can have separate
 *         leak thresholds to accommodate component-specific memory allocation patterns.
 *
 * @note  This module is designed to work in conjunction with ESP-IDF's Unity
 *        test framework and provides GMF-specific testing enhancements.
 *
 * @see Unity Test Framework documentation
 * @see ESP-IDF Unit Test App (tools/unit-test-app/components/test_utils)
 */

#include <stdio.h>
#include "string.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "unity.h"
#include "unity_test_runner.h"
#include "esp_newlib.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/sockets.h"
#include "esp_gmf_app_unit_test.h"

#ifdef CONFIG_HEAP_TRACING
#include "esp_heap_trace.h"
#endif  /* CONFIG_HEAP_TRACING */

/**
 * @brief  Memory leak threshold levels for unit testing
 *
 *         This enumeration defines different severity levels for memory leak detection.
 *         Used to categorize and handle memory leaks during unit test execution.
 */
typedef enum {
    ESP_GMF_APP_LEAK_TYPE_WARNING = 0,  /**< Warning level - minor leaks that should be monitored */
    ESP_GMF_APP_LEAK_TYPE_CRITICAL,     /**< Critical level - serious leaks that cause test failure */
    ESP_GMF_APP_LEAK_TYPE_MAX,          /**< Maximum number of leak threshold levels */
} esp_gmf_app_leak_type_t;

/**
 * @brief  Component-specific memory leak categories
 *
 *         This enumeration categorizes memory leaks by ESP-IDF components to enable
 *         component-specific leak thresholds and better debugging capabilities.
 */
typedef enum {
    ESP_GMF_APP_COMP_LEAK_GENERAL = 0,  /**< General memory leaks not specific to any component */
    ESP_GMF_APP_COMP_LEAK_LWIP,         /**< LWIP stack related memory leaks */
    ESP_GMF_APP_COMP_LEAK_NVS,          /**< NVS (Non-Volatile Storage) related memory leaks */
    ESP_GMF_APP_COMP_LEAK_ALL,          /**< Aggregate of all component leak levels */
} esp_gmf_app_comp_leak_t;

/**
 * @brief  Memory leak checking modes for unit tests
 *
 *         This enumeration defines how memory leak detection should be performed
 *         for individual test cases, allowing flexible leak checking strategies.
 */
typedef enum {
    ESP_GMF_APP_NO_LEAK_CHECK,       /**< Disable leak checking for this test */
    ESP_GMF_APP_DEFAULT_LEAK_CHECK,  /**< Use default configured leak thresholds */
    ESP_GMF_APP_SPECIAL_LEAK_CHECK   /**< Use custom leak threshold specified in test */
} esp_gmf_app_leak_check_type_t;

static size_t test_unity_leak_level[ESP_GMF_APP_LEAK_TYPE_MAX][ESP_GMF_APP_COMP_LEAK_ALL] = {0};
static size_t before_free_8bit;
static size_t before_free_32bit;

static void gmf_app_unity_task(void *pvParameters)
{
    // Delay a bit to let the main task and any other startup tasks be deleted
    vTaskDelay(pdMS_TO_TICKS(50));
    // Start running unity (prints test menu and doesn't return)
    unity_run_menu();
}

static esp_err_t gmf_app_set_leak_level(size_t leak_level, esp_gmf_app_leak_type_t type_of_leak, esp_gmf_app_comp_leak_t component)
{
    if (type_of_leak >= ESP_GMF_APP_LEAK_TYPE_MAX || component >= ESP_GMF_APP_COMP_LEAK_ALL) {
        return ESP_ERR_INVALID_ARG;
    }
    test_unity_leak_level[type_of_leak][component] = leak_level;
    return ESP_OK;
}

static size_t gmf_app_get_leak_level(esp_gmf_app_leak_type_t type_of_leak, esp_gmf_app_comp_leak_t component)
{
    size_t leak_level = 0;
    if (type_of_leak >= ESP_GMF_APP_LEAK_TYPE_MAX || component > ESP_GMF_APP_COMP_LEAK_ALL) {
        leak_level = 0;
    } else {
        if (component == ESP_GMF_APP_COMP_LEAK_ALL) {
            for (int comp = 0; comp < ESP_GMF_APP_COMP_LEAK_ALL; ++comp) {
                leak_level += test_unity_leak_level[type_of_leak][comp];
            }
        } else {
            leak_level = test_unity_leak_level[type_of_leak][component];
        }
    }
    return leak_level;
}

#ifdef CONFIG_HEAP_TRACING
static void gmf_app_setup_heap_record(void)
{
    const size_t num_heap_records = CONFIG_UNITY_HEAP_TRACE_RECORDS;
    static heap_trace_record_t *record_buffer;
    if (!record_buffer) {
        record_buffer = heap_caps_malloc_prefer(sizeof(heap_trace_record_t) * num_heap_records, 2, MALLOC_CAP_SPIRAM, MALLOC_CAP_INTERNAL);
        assert(record_buffer);
        heap_trace_init_standalone(record_buffer, num_heap_records);
    }
}
#endif  /* CONFIG_HEAP_TRACING */

static void gmf_app_record_free_mem(void)
{
    before_free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    before_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
}

static esp_gmf_app_leak_check_type_t gmf_app_leak_check_required(size_t *threshold)
{
    if (Unity.CurrentDetail1 != NULL) {
        const char *leaks = "[leaks";
        const int len_leaks = strlen(leaks);
        const char *sub_leaks = strstr(Unity.CurrentDetail1, leaks);
        if (sub_leaks != NULL) {
            if (sub_leaks[len_leaks] == ']') {
                return ESP_GMF_APP_NO_LEAK_CHECK;
            } else if (sub_leaks[len_leaks] == '=') {
                *threshold = strtol(&sub_leaks[len_leaks + 1], NULL, 10);
                return ESP_GMF_APP_SPECIAL_LEAK_CHECK;
            }
        }
    }
    return ESP_GMF_APP_DEFAULT_LEAK_CHECK;
}

static void gmf_app_check_leak(size_t before_free,
                               size_t after_free,
                               const char *type,
                               size_t warn_threshold,
                               size_t critical_threshold)
{
    int free_delta = (int)after_free - (int)before_free;
    printf("MALLOC_CAP_%s usage: Free memory delta: %d Leak threshold: -%u \n",
           type,
           free_delta,
           critical_threshold);

    if (free_delta > 0) {
        return;  // free memory went up somehow
    }

    size_t leaked = (size_t)(free_delta * -1);
    if (leaked <= warn_threshold) {
        return;
    } else {
        printf("The test leaked more memory than warn threshold: Leaked: %d Warn threshold: -%u \n",
               leaked,
               warn_threshold);
    }

    printf("MALLOC_CAP_%s %s leak: Before %u bytes free, After %u bytes free (delta %u)\n",
           type,
           leaked <= critical_threshold ? "potential" : "critical",
           before_free, after_free, leaked);
    fflush(stdout);
    TEST_ASSERT_MESSAGE(leaked <= critical_threshold, "The test leaked too much memory");
}

/**
 * @brief  Evaluate memory leaks after test execution
 */
static void gmf_app_finish_and_evaluate_leaks(size_t warn_threshold, size_t critical_threshold)
{
    size_t after_free_8bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t after_free_32bit = heap_caps_get_free_size(MALLOC_CAP_32BIT);
    gmf_app_check_leak(before_free_8bit, after_free_8bit, "8BIT", warn_threshold, critical_threshold);
    gmf_app_check_leak(before_free_32bit, after_free_32bit, "32BIT", warn_threshold, critical_threshold);
}

void setUp(void)
{
// If heap tracing is enabled in kconfig, leak trace the test
#ifdef CONFIG_HEAP_TRACING
    gmf_app_setup_heap_record();
#endif  /* CONFIG_HEAP_TRACING */

    printf("%s", "");  /* sneakily lazy-allocate the reent structure for this test task */

#ifdef CONFIG_HEAP_TRACING
    heap_trace_start(HEAP_TRACE_LEAKS);
#endif  /* CONFIG_HEAP_TRACING */
    gmf_app_record_free_mem();
    gmf_app_set_leak_level(CONFIG_UNITY_CRITICAL_LEAK_LEVEL_GENERAL, ESP_GMF_APP_LEAK_TYPE_CRITICAL, ESP_GMF_APP_COMP_LEAK_GENERAL);
    gmf_app_set_leak_level(CONFIG_UNITY_WARN_LEAK_LEVEL_GENERAL, ESP_GMF_APP_LEAK_TYPE_WARNING, ESP_GMF_APP_COMP_LEAK_GENERAL);
    gmf_app_set_leak_level(0, ESP_GMF_APP_LEAK_TYPE_CRITICAL, ESP_GMF_APP_COMP_LEAK_LWIP);
}

void tearDown(void)
{
    /* some FreeRTOS stuff is cleaned up by idle task */
    vTaskDelay(5);

    /* clean up some of the newlib's lazy allocations */
    esp_reent_cleanup();

    /* We want the teardown to have this file in the printout if TEST_ASSERT fails */
    const char *real_testfile = Unity.TestFile;
    Unity.TestFile = __FILE__;

    /* check if unit test has caused heap corruption in any heap */
    TEST_ASSERT_MESSAGE(heap_caps_check_integrity(MALLOC_CAP_INVALID, true), "The test has corrupted the heap");

    /* check for leaks */
#ifdef CONFIG_HEAP_TRACING
    heap_trace_stop();
    heap_trace_dump();
#endif  /* CONFIG_HEAP_TRACING */

    size_t leak_threshold_critical = 0;
    size_t leak_threshold_warning = 0;
    esp_gmf_app_leak_check_type_t check_type = gmf_app_leak_check_required(&leak_threshold_critical);

    // In the "special case", only one level can be passed directly from the test case.
    // Hence, we set both warning and critical leak levels to that same value here
    leak_threshold_warning = leak_threshold_critical;

    if (check_type == ESP_GMF_APP_NO_LEAK_CHECK) {
        // do not check
    } else if (check_type == ESP_GMF_APP_SPECIAL_LEAK_CHECK) {
        gmf_app_finish_and_evaluate_leaks(leak_threshold_warning, leak_threshold_critical);
    } else if (check_type == ESP_GMF_APP_DEFAULT_LEAK_CHECK) {
        gmf_app_finish_and_evaluate_leaks(gmf_app_get_leak_level(ESP_GMF_APP_LEAK_TYPE_WARNING, ESP_GMF_APP_COMP_LEAK_ALL),
                                          gmf_app_get_leak_level(ESP_GMF_APP_LEAK_TYPE_CRITICAL, ESP_GMF_APP_COMP_LEAK_ALL));
    } else {
        assert(false);  // coding error
    }

    Unity.TestFile = real_testfile;  // go back to the real filename
}

void esp_gmf_app_test_case_uses_tcpip(void)
{
    // Can be called more than once, does nothing on subsequent calls
    esp_netif_init();

    // Allocate all sockets then free them
    // (First time each socket is allocated some one-time allocations happen.)
    int sockets[CONFIG_LWIP_MAX_SOCKETS];
    for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; i++) {
        int type = (i % 2 == 0) ? SOCK_DGRAM : SOCK_STREAM;
        int family = (i % 3 == 0) ? PF_INET6 : PF_INET;
        sockets[i] = socket(family, type, IPPROTO_IP);
    }
    for (int i = 0; i < CONFIG_LWIP_MAX_SOCKETS; i++) {
        close(sockets[i]);
    }

    // Allow LWIP tasks to finish initialising themselves
    vTaskDelay(25 / portTICK_PERIOD_MS);

    printf("Note: esp_netif_init() has been called. Until next reset, TCP/IP task will periodicially allocate memory and consume CPU time.\n");

    // Reset the leak checker as LWIP allocates a lot of memory on first run
    gmf_app_record_free_mem();
    gmf_app_set_leak_level(0, ESP_GMF_APP_LEAK_TYPE_CRITICAL, ESP_GMF_APP_COMP_LEAK_GENERAL);
    gmf_app_set_leak_level(CONFIG_UNITY_CRITICAL_LEAK_LEVEL_LWIP, ESP_GMF_APP_LEAK_TYPE_CRITICAL, ESP_GMF_APP_COMP_LEAK_LWIP);
}

void esp_gmf_app_test_main(void)
{
    xTaskCreatePinnedToCore(gmf_app_unity_task, "unityTask", CONFIG_UNITY_FREERTOS_STACK_SIZE, NULL,
                            CONFIG_UNITY_FREERTOS_PRIORITY, NULL, CONFIG_UNITY_FREERTOS_CPU);
}
