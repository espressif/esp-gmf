/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
 * SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
 *
 * See LICENSE file for details.
 */

#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include "esp_log.h"
#include "esp_board_device.h"
#include "esp_board_manager.h"
#include "dev_fs_spiffs.h"

static const char *TAG = "TEST_SPIFFS";

/* Helper function to read and verify file content */
static bool verify_file_content(const char *file_path, const char *expected_content, const char *test_name)
{
    char read_buf[128] = {0};
    FILE *f = fopen(file_path, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to read %s file: %s", test_name, strerror(errno));
        return false;
    }

    size_t bytes_read = fread(read_buf, 1, sizeof(read_buf) - 1, f);
    read_buf[bytes_read] = '\0';  // Ensure null termination
    fclose(f);

    ESP_LOGI(TAG, "Read from %s: '%s'", test_name, read_buf);

    if (strcmp(expected_content, read_buf) == 0) {
        ESP_LOGI(TAG, "✅ %s content verification passed", test_name);
        return true;
    } else {
        ESP_LOGE(TAG, "❌ %s content verification failed!", test_name);
        ESP_LOGE(TAG, "Expected: '%s'", expected_content);
        ESP_LOGE(TAG, "Got: '%s'", read_buf);
        return false;
    }
}

void test_spiffs(void)
{
    const char *test_str = "Hello SPIFFS!\n";
    const char *expected_test_content = "This is esp board manager";

    dev_fs_spiffs_config_t *cfg = NULL;
    if (esp_board_manager_get_device_config("fs_spiffs", (void **)&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS configuration");
        return;
    }

    esp_board_device_show("fs_spiffs");

    ESP_LOGI(TAG, "SPIFFS Configuration:");
    ESP_LOGI(TAG, "  Name: %s", cfg->name);
    ESP_LOGI(TAG, "  Base Path: %s", cfg->base_path);
    ESP_LOGI(TAG, "  Partition Label: %s", cfg->partition_label ? cfg->partition_label : "NULL");
    ESP_LOGI(TAG, "  Max Files: %d", cfg->max_files);
    ESP_LOGI(TAG, "  Format if mount failed: %s", cfg->format_if_mount_failed ? "true" : "false");

    /* Test reading existing test.txt file */
    char test_txt_path[256];
    snprintf(test_txt_path, sizeof(test_txt_path), "%s/test.txt", cfg->base_path);
    ESP_LOGI(TAG, "Testing read of existing file: %s", test_txt_path);
    verify_file_content(test_txt_path, expected_test_content, "test.txt");

    /* Test file write operation using configured base path */
    char test_file_path[256];
    snprintf(test_file_path, sizeof(test_file_path), "%s/hello.txt", cfg->base_path);
    FILE *f = fopen(test_file_path, "w");
    if (f) {
        fprintf(f, "%s", test_str);
        fclose(f);
        ESP_LOGI(TAG, "Test file written successfully");
    } else {
        ESP_LOGE(TAG, "Failed to create test file: %s, path: %s", strerror(errno), test_file_path);
        goto cleanup;
    }

    /* Test file read operation using configured base path */
    verify_file_content(test_file_path, test_str, "hello.txt");

    /* List directory contents using configured base path */
    ESP_LOGI(TAG, "SPIFFS root directory contents:");
    DIR *dir = opendir(cfg->base_path);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            char fullpath[300];
            snprintf(fullpath, sizeof(fullpath), "%s/%s", cfg->base_path, entry->d_name);

            // Use file operations to get actual file size instead of stat
            FILE *f = fopen(fullpath, "rb");
            if (f) {
                // Seek to end to get file size
                fseek(f, 0, SEEK_END);
                long file_size = ftell(f);
                fclose(f);

                if (file_size >= 0) {
                    ESP_LOGI(TAG, "%s - %ld bytes", entry->d_name, file_size);
                } else {
                    ESP_LOGW(TAG, "%s - Unable to determine file size", entry->d_name);
                }
            } else {
                ESP_LOGI(TAG, "%s - Unable to open file", entry->d_name);
            }
        }
        closedir(dir);
    } else {
        ESP_LOGE(TAG, "Failed to open directory: %s", strerror(errno));
    }

cleanup:
    ESP_LOGI(TAG, "SPIFFS filesystem deinitialized");
}
